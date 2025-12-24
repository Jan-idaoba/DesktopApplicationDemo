#include "ServiceManager.h"

ServiceManager::ServiceManager(const std::wstring& pipeName, size_t maxInstances, size_t bufferSize)
	: m_server(pipeName, maxInstances, bufferSize)
	, ServiceBase(L"AAAService", L"AAA Service", TRUE, TRUE, FALSE)
{

}

ServiceManager::~ServiceManager()
{

}

bool ServiceManager::Start()
{
	return false;
}

void ServiceManager::Stop()
{

}

void ServiceManager::SetRequestHandler(RequestHandler handler)
{

}

void ServiceManager::OnStart(DWORD argc, LPWSTR* argv)
{

}

void ServiceManager::OnStop()
{

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

}
