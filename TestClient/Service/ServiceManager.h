#pragma once
#include "..\Service\ServiceBase.h"
#include "..\PipeServer\PipeServer.h"
#include <thread>
#include <atomic>
#include <functional>

// ==============================
// ServiceManager：业务管理类
// - 构造时初始化 PipeServer
// - 启动后在内部线程里 WaitAndPopReceived 处理消息
// - 处理完成后使用 SendToClient 回复
// ==============================
class ServiceManager : public ServiceBase
{
public:
    using RequestHandler = std::function<std::vector<uint8_t>(const PipeMessage&)>;

    explicit ServiceManager(const std::wstring& pipeName,
        size_t maxInstances = 20,
        size_t bufferSize = 4096);
    ~ServiceManager();
    ServiceManager(const ServiceManager&) = delete;
    ServiceManager& operator=(const ServiceManager&) = delete;

public:
    void SetRequestHandler(RequestHandler handler);
    PipeServer& Server() { return m_PipeServer; }
    std::vector<uint8_t> RequestHandle(const PipeMessage& Message);

public:
	void OnStart(DWORD argc, LPWSTR* argv) override;
	void OnStop() override;
	void OnPause() override;
	void OnContinue() override;
	void OnShutdown() override;
	void OnError(const std::wstring& function, DWORD error) override;

private:
    void WorkerLoop();

private:
    PipeServer            m_PipeServer;
    std::atomic<bool>     m_running{ false };
    std::thread           m_worker;

    RequestHandler        m_handler;
};
