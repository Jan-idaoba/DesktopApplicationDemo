#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <queue>
#include <unordered_map>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <functional>
#include <memory>
#include <chrono>

// 消息结构：4字节长度前缀 + 实际数据
struct PipeMessage
{
    std::string              clientId;
    std::vector<uint8_t>     payload;
    uint64_t                 timestampMs;
};

// 客户端上下文
struct ClientContext
{
    HANDLE                   hPipe = INVALID_HANDLE_VALUE;

    OVERLAPPED               ovRead{};
    OVERLAPPED               ovWrite{};
    HANDLE                   hReadEvent = nullptr;
    HANDLE                   hWriteEvent = nullptr;

    std::string              clientId;

    std::queue<PipeMessage>  sendQueue;
    std::mutex               sendMutex;
    std::condition_variable  sendCv;

    std::atomic<bool>        running{ true };
};

class PipeServer
{
public:
    using MessageHandler = std::function<void(const PipeMessage&)>;

    PipeServer(const std::wstring& pipeName,
        size_t maxInstances = 20,
        size_t bufferSize = 4096);
    ~PipeServer();

    PipeServer(const PipeServer&) = delete;
    PipeServer& operator=(const PipeServer&) = delete;

    bool Start();
    void Stop();

    bool SendToClient(const std::string& clientId, const std::vector<uint8_t>& payload);
    bool SendJsonToClient(const std::string& clientId, const std::string& jsonUtf8);

    size_t Broadcast(const std::vector<uint8_t>& payload);
    size_t BroadcastJson(const std::string& jsonUtf8);

    bool TryPopReceived(PipeMessage& msg);
    bool WaitAndPopReceived(PipeMessage& msg);

    void SetMessageHandler(MessageHandler handler);

    std::vector<std::string> ListClients() const;
    size_t GetClientCount() const;
    void DisconnectClient(const std::string& clientId);

private:
    HANDLE CreatePipeInstance();
    void   AcceptLoop();
    void   HandleClientCommunication(std::shared_ptr<ClientContext> ctx);
    void   HandleClientRead(std::shared_ptr<ClientContext> ctx);
    void   HandleClientWrite(std::shared_ptr<ClientContext> ctx);
    void   ProcessReceivedMessage(std::shared_ptr<ClientContext> ctx, const std::vector<uint8_t>& payload);
    void   CloseClient(std::shared_ptr<ClientContext> ctx);
    void   BindClientId(std::shared_ptr<ClientContext> ctx, const std::string& clientId);
    void   EnqueueReceived(const PipeMessage& msg);

private:
    std::wstring            m_pipeName;
    size_t                  m_maxInstances;
    size_t                  m_bufferSize;

    std::atomic<bool>       m_running{ false };
    std::thread             m_acceptThread;

    mutable std::mutex      m_clientsMutex;
    std::unordered_map<std::string, std::shared_ptr<ClientContext>> m_clients;

    std::queue<PipeMessage> m_receiveData;
    mutable std::mutex      m_recvMutex;
    std::condition_variable m_recvCv;

    MessageHandler          m_handler = nullptr;
};