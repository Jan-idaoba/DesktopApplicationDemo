#pragma once
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
class ServiceManager
{
public:
    // 业务处理器：输入收到的消息，返回待发的字节（或空表示不回复）
    using RequestHandler = std::function<std::vector<uint8_t>(const PipeMessage&)>;

    explicit ServiceManager(const std::wstring& pipeName,
        size_t maxInstances = 20,
        size_t bufferSize = 4096);

    ~ServiceManager();

    ServiceManager(const ServiceManager&) = delete;
    ServiceManager& operator=(const ServiceManager&) = delete;

    // 启动/停止
    bool Start();
    void Stop();

    // 设置业务处理器（可在 Start 前或运行时设置/更换）
    void SetRequestHandler(RequestHandler handler);

    // 直接访问底层 PipeServer（如需广播/查询在线 client）
    PipeServer& Server() { return m_server; }

private:
    // 业务线程：循环取消息 -> 处理 -> 回发
    void WorkerLoop();

private:
    PipeServer            m_server;
    std::atomic<bool>     m_running{ false };
    std::thread           m_worker;

    RequestHandler        m_handler; // 业务处理（可空）
};
