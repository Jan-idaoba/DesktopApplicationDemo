
// main.cpp - 简易命名管道 JSON 协议测试客户端（Windows）
// 构建：Visual Studio / MSVC，控制台应用（/std:c++17）
// 用法：PipeClient.exe \\.\pipe\MyPipe
// 若不传参数，默认管道名为 \\.\pipe\PipeSrv

#include <windows.h>
#include <string>
#include <vector>
#include <iostream>
#include <sstream>
#include <thread>
#include <atomic>
#include <chrono>
#include <iomanip>
#include <cstring>

static uint64_t NowMs() {
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER uli;
    uli.LowPart = ft.dwLowDateTime;
    uli.HighPart = ft.dwHighDateTime;
    return static_cast<uint64_t>(uli.QuadPart / 10000ULL);
}

static void Log(const std::string& s) {
#ifdef _DEBUG
    OutputDebugStringA(s.c_str());
    OutputDebugStringA("\n");
#endif
    std::cout << s << std::endl;
}

// =============== 帧读写（4字节小端长度前缀 + payload） =================

bool WriteFrame(HANDLE hPipe, const std::string& payload) {
    if (hPipe == INVALID_HANDLE_VALUE) return false;
    uint32_t len = static_cast<uint32_t>(payload.size());
    std::vector<uint8_t> buf(4 + len);
    // 小端主机：直接写入即可（更稳妥用 memcpy）
    std::memcpy(buf.data(), &len, sizeof(uint32_t));
    if (len > 0) {
        std::memcpy(buf.data() + 4, payload.data(), len);
    }
    DWORD written = 0;
    BOOL ok = WriteFile(hPipe, buf.data(), (DWORD)buf.size(), &written, NULL);
	DWORD err = GetLastError();
    if (!ok || written != buf.size()) {
        Log("WriteFrame failed");
        return false;
    }
    return true;
}

bool ReadExact(HANDLE hPipe, void* out, DWORD need) {
    uint8_t* p = static_cast<uint8_t*>(out);
    DWORD total = 0;
    while (total < need) {
        DWORD got = 0;
        BOOL ok = ReadFile(hPipe, p + total, need - total, &got, NULL);
        if (!ok) {
            DWORD err = GetLastError();
            if (err == ERROR_MORE_DATA) {
                // 管道分段，继续读
            }
            else {
                Log("ReadExact failed, err=" + std::to_string(err));
                return false;
            }
        }
        if (got == 0) {
            // 对端关闭
            return false;
        }
        total += got;
    }
    return true;
}

bool ReadFrame(HANDLE hPipe, std::string& outPayload) {
    outPayload.clear();
    uint32_t len = 0;
    if (!ReadExact(hPipe, &len, sizeof(uint32_t))) {
        return false;
    }
    // 安全上限（与服务端一致或更小）
    const uint32_t MAX_FRAME = 10 * 1024 * 1024; // 10MB
    if (len > MAX_FRAME) {
        Log("Frame too large, len=" + std::to_string(len));
        return false;
    }
    if (len == 0) {
        // 空 payload 也视为成功
        return true;
    }
    std::string payload;
    payload.resize(len);
    if (!ReadExact(hPipe, payload.data(), len)) {
        return false;
    }
    outPayload.swap(payload);
    return true;
}

// =============== 简易 UUID（演示用，不保证唯一性） =================
static std::string MakeMsgId(const char* prefix) {
    std::ostringstream oss;
    oss << prefix << "-" << NowMs();
    return oss.str();
}

// =============== 构造各类 JSON（示例字符串拼接） =================
static std::string Escape(const std::string& s) {
    // 简单转义，演示用；生产环境请使用 JSON 库
    std::ostringstream oss;
    for (char c : s) {
        switch (c) {
        case '\\': oss << "\\\\"; break;
        case '"':  oss << "\\\""; break;
        case '\n': oss << "\\n"; break;
        case '\r': oss << "\\r"; break;
        case '\t': oss << "\\t"; break;
        default: oss << c; break;
        }
    }
    return oss.str();
}

static std::string MakeHelloJson(const std::string& clientIdHint) {
    std::ostringstream oss;
    oss << R"({"ver":"1.0","type":"Hello",)"
        << R"("msgId":")" << Escape(MakeMsgId("uuid-hello")) << R"(",)"
        << R"("timestamp":)" << NowMs() << R"(,)"
        << R"("payload":{)"
        << R"("appName":"WinConsoleTest",)"
        << R"("appVersion":"0.1.0",)"
        << R"("clientIdHint":")" << Escape(clientIdHint) << R"(",)"
        << R"("pid":)" << GetCurrentProcessId() << R"(,)"
        << R"("machine":{)"
        << R"("host":"N/A","os":"Windows","user":"N/A","locale":"zh-CN"},)"
        << R"("capabilities":{"canReceiveNotify":true,"supportsCompression":false,"supportsAuth":true}"
        })"
        << "}";
    return oss.str();
}

static std::string MakeAuthJson(const std::string& clientId, const std::string& token) {
    std::ostringstream oss;
    oss << R"({"ver":"1.0","type":"Auth",)"
        << R"("msgId":")" << Escape(MakeMsgId("uuid-auth")) << R"(",)"
        << R"("clientId":")" << Escape(clientId) << R"(",)"
        << R"("payload":{"method":"Token","token":")" << Escape(token) << R"("}})";
    return oss.str();
}

static std::string MakeHeartbeatJson(const std::string& clientId, int64_t seq) {
    std::ostringstream oss;
    oss << R"({"ver":"1.0","type":"Heartbeat",)"
        << R"("msgId":")" << Escape(MakeMsgId("uuid-hb")) << R"(",)"
        << R"("clientId":")" << Escape(clientId) << R"(",)"
        << R"("timestamp":)" << NowMs() << R"(,)"
        << R"("payload":{"seq":)" << seq << R"(}})";
    return oss.str();
}

static std::string MakeRequestJson(const std::string& clientId,
    const std::string& action,
    const std::string& paramsJson /*原样放入*/) {
    std::ostringstream oss;
    oss << R"({"ver":"1.0","type":"Request",)"
        << R"("msgId":")" << Escape(MakeMsgId("uuid-req")) << R"(",)"
        << R"("clientId":")" << Escape(clientId) << R"(",)"
        << R"("payload":{"action":")" << Escape(action) << R"(","params":)" << paramsJson << "}}";
    return oss.str();
}

static std::string MakeGoodbyeJson(const std::string& clientId, const std::string& reason) {
    std::ostringstream oss;
    oss << R"({"ver":"1.0","type":"Goodbye",)"
        << R"("msgId":")" << Escape(MakeMsgId("uuid-bye")) << R"(",)"
        << R"("clientId":")" << Escape(clientId) << R"(",)"
        << R"("payload":{"reason":")" << Escape(reason) << R"("}})";
    return oss.str();
}

// =============== 辅助打印 =================
static void PrintHex(const std::string& s, size_t maxBytes = 64) {
    std::ostringstream oss;
    oss << "HEX(" << s.size() << "): ";
    /*size_t n = std::min(s.size(), maxBytes);
    for (size_t i = 0; i < n; ++i) {
        oss << std::hex << std::setw(2) << std::setfill('0')
            << (static_cast<unsigned int>(static_cast<unsigned char>(s[i]))) << " ";
    }
    if (s.size() > n) oss << "...";*/
    Log(oss.str());
}

// =============== 主程序 =================

int main(int argc, char* argv[]) {
    std::wstring pipeName = LR"(\\.\pipe\WebView2VuePipe)"; // 默认值
    if (argc >= 2) {
        // 将窄字符串参数转换为宽字符串
        int wlen = MultiByteToWideChar(CP_UTF8, 0, argv[1], -1, nullptr, 0);
        std::wstring w(wlen, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, argv[1], -1, &w[0], wlen);
        // 去掉结尾的 '\0'
        if (!w.empty() && w.back() == L'\0') w.pop_back();
        pipeName = w;
    }

    Log("Connecting to pipe: " + std::string("UTF16.."));
    HANDLE hPipe = CreateFileW(
        pipeName.c_str(),                // 管道名
        GENERIC_READ | GENERIC_WRITE,    // 读写
        0,                               // 不共享
        NULL,                            // 默认安全属性
        OPEN_EXISTING,                   // 必须已存在（服务端先 Start）
        FILE_ATTRIBUTE_NORMAL,           // 同步模式
        NULL
    );

    if (hPipe == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        Log("CreateFileW failed, err=" + std::to_string(err));
        return 1;
    }
    Log("Connected.");

    // 设置为字节读模式（服务端是 PIPE_READMODE_BYTE）
    DWORD mode = PIPE_READMODE_BYTE;
    if (!SetNamedPipeHandleState(hPipe, &mode, NULL, NULL)) {
        Log("SetNamedPipeHandleState failed");
    }

    // 1) 兼容首帧纯文本 ClientId（服务端以此 Bind）
    std::string clientId = "CLI-Console-001";
    Log("Sending plain ClientId to bind: " + clientId);
    if (!WriteFrame(hPipe, clientId)) {
        Log("Failed to send plain ClientId frame.");
        CloseHandle(hPipe);
        return 1;
    }

    // 2) 发送 Hello JSON
    std::string hello = MakeHelloJson(clientId);
    Log("Sending Hello JSON:\n" + hello);
    if (!WriteFrame(hPipe, hello)) {
        Log("Failed to send Hello.");
        CloseHandle(hPipe);
        return 1;
    }

    std::atomic<bool> running{ true };

    // 读取线程：打印服务端返回的所有帧
    std::thread reader([&] {
        while (running.load()) {
            std::string payload;
            if (!ReadFrame(hPipe, payload)) {
                Log("ReadFrame failed or pipe closed.");
                break;
            }
            Log("<< Received payload (" + std::to_string(payload.size()) + " bytes):");
            std::cout << payload << std::endl;
            PrintHex(payload);
        }
        running = false;
        });

    // 3) 可选：发送 Auth（如果你的服务端需要）
    std::string auth = MakeAuthJson(clientId, "dummy-token-123");
    Log("Sending Auth JSON:\n" + auth);
    WriteFrame(hPipe, auth);

    // 4) 心跳线程（每 10 秒）
    std::thread heartbeater([&] {
        int64_t seq = 1;
        while (running.load()) {
            std::this_thread::sleep_for(std::chrono::seconds(10));
            if (!running.load()) break;
            std::string hb = MakeHeartbeatJson(clientId, seq++);
            Log("Sending Heartbeat:\n" + hb);
            if (!WriteFrame(hPipe, hb)) {
                Log("Heartbeat send failed.");
                break;
            }
        }
        });

    // 5) 发送一个请求（例如 SetConfig）
    std::string params = R"({"logLevel":"Debug","enableFeatureX":true})";
    std::string req = MakeRequestJson(clientId, "SetConfig", params);
    Log("Sending Request:\n" + req);
    WriteFrame(hPipe, req);

    // 运行一段时间以观察交互
    std::this_thread::sleep_for(std::chrono::seconds(25));

    // 6) 发送 Goodbye
    std::string bye = MakeGoodbyeJson(clientId, "UserExit");
    Log("Sending Goodbye:\n" + bye);
    WriteFrame(hPipe, bye);

    // 收尾
    running = false;
    // 关掉 pipe 会让 reader 退出
    FlushFileBuffers(hPipe);
    CloseHandle(hPipe);

    if (reader.joinable()) reader.join();
    if (heartbeater.joinable()) heartbeater.join();

    Log("Client finished.");
    return 0;
}
