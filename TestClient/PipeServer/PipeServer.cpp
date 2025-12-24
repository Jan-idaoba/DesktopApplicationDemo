#include "PipeServer.h"
#include <iostream>

static uint64_t NowMs() {
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER uli;
    uli.LowPart = ft.dwLowDateTime;
    uli.HighPart = ft.dwHighDateTime;
    return static_cast<uint64_t>(uli.QuadPart / 10000ULL);
}

static void Log(const char* s) {
#ifdef _DEBUG
    OutputDebugStringA(s);
#endif
    std::cout << s << std::endl;
}

static HANDLE MakeAutoResetEvent() {
    return CreateEvent(NULL, FALSE, FALSE, NULL);
}

PipeServer::PipeServer(const std::wstring& pipeName, size_t maxInstances, size_t bufferSize)
    : m_pipeName(pipeName)
    , m_maxInstances(maxInstances)
    , m_bufferSize(bufferSize)
{

}

PipeServer::~PipeServer()
{
    Stop();
}

bool PipeServer::Start()
{
    if (m_running.load())
        return true;
    m_running = true;
    m_acceptThread = std::thread(&PipeServer::AcceptLoop, this);
    return true;
}

void PipeServer::Stop()
{
    if (!m_running.exchange(false))
        return;

    if (m_acceptThread.joinable())
        m_acceptThread.join();

    std::vector<std::shared_ptr<ClientContext>> toClose;
    {
        std::lock_guard<std::mutex> lock(m_clientsMutex);
        for (auto& pair : m_clients) {
            toClose.push_back(pair.second);
        }
    }

    for (auto& ctx : toClose) {
        CloseClient(ctx);
    }

    m_recvCv.notify_all();
}

bool PipeServer::SendToClient(const std::string& clientId, const std::vector<uint8_t>& payload)
{
    std::shared_ptr<ClientContext> ctx;
    {
        std::lock_guard<std::mutex> lk(m_clientsMutex);
        auto it = m_clients.find(clientId);
        if (it == m_clients.end())
            return false;
        ctx = it->second;
    }

    if (!ctx)
        return false;

    {
        std::lock_guard<std::mutex> lk(ctx->sendMutex);
        PipeMessage msg;
        msg.clientId = clientId;
        msg.payload = payload;
        msg.timestampMs = NowMs();
        ctx->sendQueue.push(std::move(msg));
    }

    ctx->sendCv.notify_one();
    return true;
}

bool PipeServer::SendJsonToClient(const std::string& clientId, const std::string& jsonUtf8)
{
    return SendToClient(clientId, std::vector<uint8_t>(jsonUtf8.begin(), jsonUtf8.end()));
}

size_t PipeServer::Broadcast(const std::vector<uint8_t>& payload)
{
    size_t cnt = 0;
    std::vector<std::shared_ptr<ClientContext>> clients;

    {
        std::lock_guard<std::mutex> lk(m_clientsMutex);
        for (auto& kv : m_clients) {
            if (kv.second) {
                clients.push_back(kv.second);
            }
        }
    }

    for (auto& ctx : clients)
    {
        {
            std::lock_guard<std::mutex> lk(ctx->sendMutex);
            PipeMessage msg;
            msg.clientId = ctx->clientId;
            msg.payload = payload;
            msg.timestampMs = NowMs();
            ctx->sendQueue.push(std::move(msg));
        }
        ctx->sendCv.notify_one();
        cnt++;
    }

    return cnt;
}

size_t PipeServer::BroadcastJson(const std::string& jsonUtf8)
{
    return Broadcast(std::vector<uint8_t>(jsonUtf8.begin(), jsonUtf8.end()));
}

bool PipeServer::TryPopReceived(PipeMessage& msg)
{
    std::lock_guard<std::mutex> lk(m_recvMutex);  
    if (m_receiveData.empty())
        return false;
    msg = std::move(m_receiveData.front());
    m_receiveData.pop();
    return true;
}

bool PipeServer::WaitAndPopReceived(PipeMessage& msg)
{
    std::unique_lock<std::mutex> lk(m_recvMutex);
    m_recvCv.wait(lk, [&] {
        return !m_receiveData.empty() || !m_running.load();
        });
    if (!m_running.load() && m_receiveData.empty()) {
        return false;
    }
    msg = std::move(m_receiveData.front());
    m_receiveData.pop();
    return true;
}

void PipeServer::SetMessageHandler(MessageHandler handler)
{
    m_handler = std::move(handler);
}

std::vector<std::string> PipeServer::ListClients() const
{
    std::vector<std::string> ids;
    std::lock_guard<std::mutex> lk(m_clientsMutex);
    ids.reserve(m_clients.size());
    for (auto& kv : m_clients)
        ids.push_back(kv.first);
    return ids;
}

size_t PipeServer::GetClientCount() const
{
    std::lock_guard<std::mutex> lk(m_clientsMutex);
    return m_clients.size();
}

void PipeServer::DisconnectClient(const std::string& clientId)
{
    std::shared_ptr<ClientContext> ctx;
    {
        std::lock_guard<std::mutex> lk(m_clientsMutex);
        auto it = m_clients.find(clientId);
        if (it == m_clients.end()) return;
        ctx = it->second;
        m_clients.erase(it);
    }

    if (ctx)
        CloseClient(ctx);
}

HANDLE PipeServer::CreatePipeInstance()
{
    HANDLE hPipe = CreateNamedPipeW(
        m_pipeName.c_str(),
        PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
        static_cast<DWORD>(m_maxInstances),
        static_cast<DWORD>(m_bufferSize),
        static_cast<DWORD>(m_bufferSize),
        0,
        NULL);

    return hPipe;
}

void PipeServer::AcceptLoop()
{
    while (m_running.load())
    {
        HANDLE hPipe = CreatePipeInstance();
        if (hPipe == INVALID_HANDLE_VALUE) {
            Log("CreatePipeInstance failed");
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        BOOL bConnected = ConnectNamedPipe(hPipe, NULL);
        if (!bConnected) {
            DWORD err = GetLastError();
            if (err != ERROR_PIPE_CONNECTED) {
                CloseHandle(hPipe);
                continue;
            }
        }

        auto ctx = std::make_shared<ClientContext>();
        ctx->hPipe = hPipe;
        ctx->hReadEvent = MakeAutoResetEvent();
        ctx->hWriteEvent = MakeAutoResetEvent();
        ctx->ovRead.hEvent = ctx->hReadEvent;
        ctx->ovWrite.hEvent = ctx->hWriteEvent;

        std::thread t(&PipeServer::HandleClientCommunication, this, ctx);
        t.detach();
    }
}

void PipeServer::HandleClientCommunication(std::shared_ptr<ClientContext> ctx)
{
    if (!ctx || ctx->hPipe == INVALID_HANDLE_VALUE) {
        return;
    }

    Log("Client connected, starting communication");

    // 启动写线程
    std::thread writeThread([this, ctx]() {
        HandleClientWrite(ctx);
        });

    // 当前线程处理读
    HandleClientRead(ctx);

    // 等待写线程结束
    if (writeThread.joinable()) {
        writeThread.join();
    }

    CloseClient(ctx);
    Log("Client communication ended");
}

void PipeServer::HandleClientRead(std::shared_ptr<ClientContext> ctx)
{
    const size_t BUFFER_SIZE = 8192;
    std::vector<uint8_t> buffer(BUFFER_SIZE);
    std::vector<uint8_t> messageBuffer;

    while (ctx->running.load() && m_running.load())
    {
        DWORD bytesRead = 0;
        BOOL success = ReadFile(
            ctx->hPipe,
            buffer.data(),
            static_cast<DWORD>(buffer.size()),
            &bytesRead,
            &ctx->ovRead
        );

        if (!success) {
            DWORD err = GetLastError();
            if (err == ERROR_IO_PENDING) {
                // 等待异步完成
                DWORD waitResult = WaitForSingleObject(ctx->hReadEvent, 5000);
                if (waitResult == WAIT_OBJECT_0) {
                    if (!GetOverlappedResult(ctx->hPipe, &ctx->ovRead, &bytesRead, FALSE)) {
                        Log("GetOverlappedResult failed on read");
                        break;
                    }
                }
                else if (waitResult == WAIT_TIMEOUT) {
                    // 超时，继续循环
                    CancelIoEx(ctx->hPipe, &ctx->ovRead);
                    continue;
                }
                else {
                    Log("WaitForSingleObject failed");
                    break;
                }
            }
            else {
                Log("ReadFile failed");
                break;
            }
        }

        if (bytesRead == 0) {
            Log("Client disconnected (read 0 bytes)");
            break;
        }

        // 累积到消息缓冲区
        messageBuffer.insert(messageBuffer.end(), buffer.begin(), buffer.begin() + bytesRead);

        // 处理完整消息（简单协议：4字节长度 + 数据）
        while (messageBuffer.size() >= 4) {
            uint32_t msgLen = *reinterpret_cast<uint32_t*>(messageBuffer.data());

            // 防止恶意超大消息
            if (msgLen > 10 * 1024 * 1024) {  // 10MB 限制
                Log("Message too large, disconnecting client");
                ctx->running = false;
                break;
            }

            if (messageBuffer.size() < 4 + msgLen) {
                // 消息不完整，继续读取
                break;
            }

            // 提取完整消息
            std::vector<uint8_t> payload(
                messageBuffer.begin() + 4,
                messageBuffer.begin() + 4 + msgLen
            );

            // 处理消息
            ProcessReceivedMessage(ctx, payload);

            // 移除已处理的消息
            messageBuffer.erase(messageBuffer.begin(), messageBuffer.begin() + 4 + msgLen);
        }
    }

    ctx->running = false;
}

void PipeServer::HandleClientWrite(std::shared_ptr<ClientContext> ctx)
{
    while (ctx->running.load() && m_running.load())
    {
        PipeMessage msg;

        // 等待发送队列有数据
        {
            std::unique_lock<std::mutex> lk(ctx->sendMutex);
            ctx->sendCv.wait_for(lk, std::chrono::milliseconds(100), [&] {
                return !ctx->sendQueue.empty() || !ctx->running.load();
                });

            if (!ctx->running.load()) {
                break;
            }

            if (ctx->sendQueue.empty()) {
                continue;
            }

            msg = std::move(ctx->sendQueue.front());
            ctx->sendQueue.pop();
        }

        // 构造带长度前缀的消息
        uint32_t msgLen = static_cast<uint32_t>(msg.payload.size());
        std::vector<uint8_t> frame(4 + msgLen);
        *reinterpret_cast<uint32_t*>(frame.data()) = msgLen;
        std::copy(msg.payload.begin(), msg.payload.end(), frame.begin() + 4);

        // 异步写入
        DWORD bytesWritten = 0;
        BOOL success = WriteFile(
            ctx->hPipe,
            frame.data(),
            static_cast<DWORD>(frame.size()),
            &bytesWritten,
            &ctx->ovWrite
        );

        if (!success) {
            DWORD err = GetLastError();
            if (err == ERROR_IO_PENDING) {
                DWORD waitResult = WaitForSingleObject(ctx->hWriteEvent, 5000);
                if (waitResult == WAIT_OBJECT_0) {
                    if (!GetOverlappedResult(ctx->hPipe, &ctx->ovWrite, &bytesWritten, FALSE)) {
                        Log("GetOverlappedResult failed on write");
                        break;
                    }
                }
                else {
                    Log("Write timeout or error");
                    CancelIoEx(ctx->hPipe, &ctx->ovWrite);
                    break;
                }
            }
            else {
                Log("WriteFile failed");
                break;
            }
        }
    }

    ctx->running = false;
}

void PipeServer::ProcessReceivedMessage(std::shared_ptr<ClientContext> ctx, const std::vector<uint8_t>& payload)
{
    // 如果客户端还没有ID，尝试从第一条消息中提取（假设首条消息包含ID）
    if (ctx->clientId.empty()) {
        // 简单示例：假设第一条消息是纯文本ID
        std::string potentialId(payload.begin(), payload.end());
        if (!potentialId.empty() && potentialId.size() < 256) {
            BindClientId(ctx, potentialId);
            Log(("Client bound with ID: " + potentialId).c_str());
            return;  // 首条消息用于握手，不入队
        }
    }

    // 构造消息并入队
    PipeMessage msg;
    msg.clientId = ctx->clientId;
    msg.payload = payload;
    msg.timestampMs = NowMs();

    EnqueueReceived(msg);
}

void PipeServer::CloseClient(std::shared_ptr<ClientContext> ctx)
{
    if (!ctx)
        return;

    ctx->running = false;

    if (ctx->hPipe && ctx->hPipe != INVALID_HANDLE_VALUE) {
        CancelIoEx(ctx->hPipe, NULL);
        FlushFileBuffers(ctx->hPipe);
        DisconnectNamedPipe(ctx->hPipe);
        CloseHandle(ctx->hPipe);
        ctx->hPipe = INVALID_HANDLE_VALUE;
    }

    if (ctx->hReadEvent) {
        SetEvent(ctx->hReadEvent);
        CloseHandle(ctx->hReadEvent);
        ctx->hReadEvent = nullptr;
    }

    if (ctx->hWriteEvent) {
        SetEvent(ctx->hWriteEvent);
        CloseHandle(ctx->hWriteEvent);
        ctx->hWriteEvent = nullptr;
    }

    ctx->sendCv.notify_all();

    if (!ctx->clientId.empty()) {
        std::lock_guard<std::mutex> lk(m_clientsMutex);
        auto it = m_clients.find(ctx->clientId);
        if (it != m_clients.end() && it->second.get() == ctx.get()) {
            m_clients.erase(it);
        }
    }
}

void PipeServer::BindClientId(std::shared_ptr<ClientContext> ctx, const std::string& clientId)
{
    if (!ctx || clientId.empty())
        return;

    ctx->clientId = clientId;

    std::lock_guard<std::mutex> lk(m_clientsMutex);
    m_clients[clientId] = ctx;
}

void PipeServer::EnqueueReceived(const PipeMessage& msg)
{
    {
        std::lock_guard<std::mutex> lk(m_recvMutex);
        m_receiveData.push(msg);
    }
    m_recvCv.notify_one();

    if (m_handler) {
        m_handler(msg);
    }
}