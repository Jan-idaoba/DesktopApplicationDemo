// ServiceBase.h
#pragma once
#include <windows.h>
#include <string>

class ServiceBase 
{
public:
	ServiceBase(
		const std::wstring& serviceName,
		const std::wstring& serviceDesc = L"",
		BOOL canStop = TRUE,
		BOOL canShutdown = TRUE,
		BOOL canPauseContinue = FALSE
	);

	virtual ~ServiceBase();

	ServiceBase(const ServiceBase&) = delete;
	ServiceBase& operator=(const ServiceBase&) = delete;

public:
	static bool Run(ServiceBase& service);
	void Stop();

	bool IsRunning() const { return m_ServiceStatus.dwCurrentState == SERVICE_RUNNING; }
	bool IsStopped() const { return m_ServiceStatus.dwCurrentState == SERVICE_STOPPED; }
	bool IsPaused() const { return m_ServiceStatus.dwCurrentState == SERVICE_PAUSED; }

	const std::wstring& GetServiceName() const { return m_wstrName; }
	const std::wstring& GetServiceDescription() const { return m_wstrDesc; }
	void SetServiceDescription(const std::wstring& desc) { m_wstrDesc = desc; }

protected:
	virtual void OnStart(DWORD argc, LPWSTR* argv) = 0;
	virtual void OnStop() = 0;
	virtual void OnPause();
	virtual void OnContinue();
	virtual void OnShutdown();
	virtual void OnError(const std::wstring& function, DWORD error);

	void SetServiceStatus(DWORD currentState, DWORD exitCode = NO_ERROR, DWORD waitHint = 0);

private:
	static void WINAPI ServiceMain(DWORD argc, LPWSTR* argv);
	static DWORD WINAPI ServiceCtrlHandler(DWORD ctrl, DWORD eventType, LPVOID eventData, LPVOID context);

	void Start(DWORD argc, LPWSTR* argv);
	void Pause();
	void Continue();
	void Shutdown();

	void HandleError(const std::wstring& function);

private:
	static ServiceBase*		m_pService;
	SERVICE_STATUS          m_ServiceStatus;
	SERVICE_STATUS_HANDLE   m_hServiceStatusHandle;
	std::wstring            m_wstrName;
	std::wstring            m_wstrDesc;
	bool                    m_bStop;
	bool                    m_bShutdown;
	bool                    m_bPauseContinue;
};