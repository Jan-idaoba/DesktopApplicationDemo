// ServiceBase.cpp
#include "ServiceBase.h"
#include <stdexcept>
#include <system_error>
#include <fstream>
#include <iostream>
#include <sstream>
#include <ctime>
#include "..\Log\LogMacros.h"
#include "..\Common\Common.h"

ServiceBase* ServiceBase::m_pService = nullptr;
ServiceBase::ServiceBase(
	const std::wstring& serviceName,
	const std::wstring& serviceDesc,
	BOOL bStop,
	BOOL bShutdown,
	BOOL bPauseContinue)
	: m_wstrName(serviceName)
	, m_wstrDesc(serviceDesc)
	, m_bStop(bStop)
	, m_bPauseContinue(bPauseContinue)
	, m_bShutdown(bShutdown)
{
	m_ServiceStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
	m_ServiceStatus.dwCurrentState = SERVICE_START_PENDING;
	m_ServiceStatus.dwControlsAccepted = 0;
	if (m_bStop)
		m_ServiceStatus.dwControlsAccepted |= SERVICE_ACCEPT_STOP;
	if (m_bShutdown)
		m_ServiceStatus.dwControlsAccepted |= SERVICE_ACCEPT_SHUTDOWN;
	if (m_bPauseContinue)
		m_ServiceStatus.dwControlsAccepted |= SERVICE_ACCEPT_PAUSE_CONTINUE;

	m_ServiceStatus.dwWin32ExitCode = NO_ERROR;
	m_ServiceStatus.dwServiceSpecificExitCode = 0;
	m_ServiceStatus.dwCheckPoint = 0;
	m_ServiceStatus.dwWaitHint = 0;
	m_hServiceStatusHandle = nullptr;
}

ServiceBase::~ServiceBase()
{
	if (m_hServiceStatusHandle) {
		SetServiceStatus(SERVICE_STOPPED);
	}
}

bool ServiceBase::Run(ServiceBase& service)
{
	m_pService = &service;

	SERVICE_TABLE_ENTRY serviceTable[] =
	{
		{ const_cast<LPWSTR>(service.m_wstrName.c_str()), ServiceMain },
		{ nullptr, nullptr }
	};

	std::string startMsg = std::string("Starting service: ") + Common::Encoding::WideToUtf8(service.m_wstrName) + std::to_string(reinterpret_cast<uintptr_t>(&ServiceMain));
	LOG_INFO(startMsg.c_str());

	BOOL result = StartServiceCtrlDispatcher(serviceTable);
	if (result == FALSE)
	{
		DWORD error = GetLastError();
		LPVOID lpMsgBuf;
		FormatMessage(
			FORMAT_MESSAGE_ALLOCATE_BUFFER |
			FORMAT_MESSAGE_FROM_SYSTEM |
			FORMAT_MESSAGE_IGNORE_INSERTS,
			NULL,
			error,
			MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
			(LPTSTR)&lpMsgBuf,
			0, NULL);

		//std::string strMsg = std::string("StartServiceCtrlDispatcher failed. Error: ") + std::to_string(error) + std::string(" Details: ") + Common::Encoding::WideToUtf8(reinterpret_cast<LPTSTR>(lpMsgBuf));
		//LOG_INFO(strMsg.c_str());
		LocalFree(lpMsgBuf);

		return false;
	}

	return true;
}

void ServiceBase::SetServiceStatus(DWORD currentState, DWORD exitCode, DWORD waitHint)
{
	static DWORD checkPoint = 1;

	m_ServiceStatus.dwCurrentState = currentState;
	m_ServiceStatus.dwWin32ExitCode = exitCode;
	m_ServiceStatus.dwWaitHint = waitHint;

	if (currentState == SERVICE_START_PENDING) {
		m_ServiceStatus.dwControlsAccepted = 0;
	}
	else
	{
		m_ServiceStatus.dwControlsAccepted = 0;
		if (m_bStop && (currentState == SERVICE_RUNNING || currentState == SERVICE_PAUSED))
		{
			m_ServiceStatus.dwControlsAccepted |= SERVICE_ACCEPT_STOP;
		}
		if (m_bShutdown)
		{
			m_ServiceStatus.dwControlsAccepted |= SERVICE_ACCEPT_SHUTDOWN;
		}
		if (m_bPauseContinue && (currentState == SERVICE_RUNNING || currentState == SERVICE_PAUSED))
		{
			m_ServiceStatus.dwControlsAccepted |= SERVICE_ACCEPT_PAUSE_CONTINUE;
		}
	}

	if (currentState == SERVICE_RUNNING ||
		currentState == SERVICE_STOPPED ||
		currentState == SERVICE_PAUSED)
	{
		m_ServiceStatus.dwCheckPoint = 0;
	}
	else
	{
		m_ServiceStatus.dwCheckPoint = checkPoint++;
	}

	if (!::SetServiceStatus(m_hServiceStatusHandle, &m_ServiceStatus))
	{
		HandleError(L"SetServiceStatus");
	}
	else
	{
		std::wstringstream ss;
		ss << L"Service state changed to: " << currentState;
		//LOG_INFOW(ss.str());
	}
}

void ServiceBase::OnError(const std::wstring& function, DWORD error)
{
	std::wstringstream ss;
	ss << L"Error in " << function << L": " << error;
	//LOG_INFOW(ss.str());
}

void ServiceBase::HandleError(const std::wstring& function)
{
	DWORD error = GetLastError();
	OnError(function, error);
}

void WINAPI ServiceBase::ServiceMain(DWORD argc, LPWSTR* argv)
{
	try
	{
		m_pService->m_hServiceStatusHandle = RegisterServiceCtrlHandlerEx(
			m_pService->m_wstrName.c_str(),
			ServiceCtrlHandler,
			nullptr
		);

		if (m_pService->m_hServiceStatusHandle == nullptr) {
			throw std::runtime_error("RegisterServiceCtrlHandler failed");
		}

		//LOG_INFO("======= Service Main Function Start...... =======");

		// 设置初始状态
		m_pService->SetServiceStatus(SERVICE_START_PENDING, NO_ERROR, 3000);

		// 调用服务启动处理
		m_pService->OnStart(argc, argv);

		// 设置运行状态
		m_pService->SetServiceStatus(SERVICE_RUNNING);
	}
	catch (const std::exception& e)
	{
		//LOG_INFO("Service failed to start: " + std::string(e.what()));
		m_pService->SetServiceStatus(SERVICE_STOPPED, ERROR_SERVICE_SPECIFIC_ERROR);
	}
}

DWORD WINAPI ServiceBase::ServiceCtrlHandler(
	DWORD ctrl,
	DWORD eventType,
	LPVOID eventData,
	LPVOID context)
{
	switch (ctrl)
	{
	case SERVICE_CONTROL_STOP:
	{
		if (!m_pService->m_bStop)
		{
			//LOG_INFO("Stop control not accepted.");
			return ERROR_CALL_NOT_IMPLEMENTED;
		}
		if (m_pService->m_ServiceStatus.dwCurrentState != SERVICE_RUNNING &&
			m_pService->m_ServiceStatus.dwCurrentState != SERVICE_PAUSED)
		{
			//LOG_INFOW(L"Stop control invalid in current state: " + std::to_wstring(m_pService->m_ServiceStatus.dwCurrentState));
			return ERROR_INVALID_SERVICE_CONTROL;
		}

		//LOG_INFO("Processing stop command.");
		m_pService->Stop();
		return NO_ERROR;
	}

	case SERVICE_CONTROL_PAUSE:
	{
		if (!m_pService->m_bPauseContinue)
			return ERROR_CALL_NOT_IMPLEMENTED;
		if (m_pService->m_ServiceStatus.dwCurrentState != SERVICE_RUNNING)
			return ERROR_INVALID_SERVICE_CONTROL;

		//LOG_INFO("Received pause command.");
		m_pService->Pause();
		return NO_ERROR;
	}

	case SERVICE_CONTROL_CONTINUE:
	{
		if (!m_pService->m_bPauseContinue)
			return ERROR_CALL_NOT_IMPLEMENTED;
		if (m_pService->m_ServiceStatus.dwCurrentState != SERVICE_PAUSED)
			return ERROR_INVALID_SERVICE_CONTROL;

		//LOG_INFO("Received continue command.");
		m_pService->Continue();
		return NO_ERROR;
	}

	case SERVICE_CONTROL_INTERROGATE:
	{
		return NO_ERROR;
	}

	case SERVICE_CONTROL_SHUTDOWN:
	{
		if (!m_pService->m_bShutdown)
			return ERROR_CALL_NOT_IMPLEMENTED;

		//LOG_INFO("Received shutdown command.");
		m_pService->Shutdown();
		return NO_ERROR;
	}

	default:
		return ERROR_CALL_NOT_IMPLEMENTED;
	}

	return ERROR_CALL_NOT_IMPLEMENTED;
}

// 其他成员函数实现...
void ServiceBase::Stop()
{
	try
	{
		DWORD originalState = m_ServiceStatus.dwCurrentState;
		//LOG_INFO("Attempting to stop service from state: " + std::to_string(originalState));

		if (originalState != SERVICE_RUNNING && originalState != SERVICE_PAUSED)
		{
			//LOG_INFO("Cannot stop service: Invalid state");
			return;
		}

		SetServiceStatus(SERVICE_STOP_PENDING, NO_ERROR, 5000);
		OnStop();
		SetServiceStatus(SERVICE_STOPPED);
		//LOG_INFO("Service stopped successfully");
	}
	catch (const std::exception& e)
	{
		//LOG_INFO("Error during service stop: " + std::string(e.what()));
		SetServiceStatus(SERVICE_STOPPED);
	}
}

void ServiceBase::Start(DWORD argc, LPWSTR* argv)
{

}

void ServiceBase::Pause()
{
	//LOG_INFO("======= Pausing service... =======");
	SetServiceStatus(SERVICE_PAUSE_PENDING);
	OnPause();
	SetServiceStatus(SERVICE_PAUSED);
	//LOG_INFO("======= Service paused. =======");
}

void ServiceBase::Continue()
{
	//LOG_INFO("======= Resuming service... =======");
	SetServiceStatus(SERVICE_CONTINUE_PENDING);
	OnContinue();
	SetServiceStatus(SERVICE_RUNNING);
	//LOG_INFO("======= Service resumed. =======");
}

void ServiceBase::Shutdown()
{
	//LOG_INFO("======= Service shutting down... =======");
	OnShutdown();
	SetServiceStatus(SERVICE_STOPPED);
	//LOG_INFO("======= Service shut down. =======");
}

// 默认的虚函数实现
void ServiceBase::OnPause() {}
void ServiceBase::OnContinue() {}
void ServiceBase::OnShutdown() {}
