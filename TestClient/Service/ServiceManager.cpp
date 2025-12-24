#include "ServiceManager.h"
#include <nlohmann/json.hpp>

ServiceManager::ServiceManager(const std::wstring& pipeName, size_t maxInstances, size_t bufferSize)
	: m_PipeServer(pipeName, maxInstances, bufferSize)
	, ServiceBase(L"AAAService", L"AAA Service", TRUE, TRUE, FALSE)
{

}

ServiceManager::~ServiceManager()
{
	try {
		if (m_running.load()) {
			OnStop();
		}
	}
	catch (...) {}
}

void ServiceManager::SetRequestHandler(RequestHandler handler)
{

}

std::vector<uint8_t> ServiceManager::RequestHandle(const PipeMessage& Message)
{
	if (m_handler) {
		return m_handler(Message);
	}

	std::string s(Message.payload.begin(), Message.payload.end());

	auto ReceiveJaon = nlohmann::json::parse(s);
}

void ServiceManager::OnStart(DWORD argc, LPWSTR* argv)
{
	m_PipeServer.Start();
	m_running = true;
	m_worker = std::thread(&ServiceManager::WorkerLoop, this);
}

void ServiceManager::OnStop()
{
	m_PipeServer.Stop();
	m_running = false;
	if (m_worker.joinable())
	{
		m_worker.join();
	}
}

void ServiceManager::OnPause()
{

}

void ServiceManager::OnContinue()
{

}

void ServiceManager::OnShutdown()
{

}

void ServiceManager::OnError(const std::wstring& function, DWORD error)
{

}

void ServiceManager::WorkerLoop()
{
	while (m_running.load())
	{
		PipeMessage msg;
		if (!m_PipeServer.WaitAndPopReceived(msg))
		{
			break;
		}

		std::vector<uint8_t> response = RequestHandle(msg);
		if (!response.empty())
		{
			m_PipeServer.SendToClient(msg.clientId, response);
		}
	}
}
