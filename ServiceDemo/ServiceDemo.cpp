//// =============================================================
//// 文件名: BackendService.cpp
//// 描述: 这是一个模拟的后台 C++ Service。
////       它创建一个命名管道服务器，等待 UI Host 连接。
////       收到消息后，它会模拟执行任务（如大小写转换），然后返回结果。
//// 编译: g++ BackendService.cpp -o BackendService.exe
////       或者在 Visual Studio 中创建一个控制台项目。
//// =============================================================
//
//#include <windows.h>
//#include <iostream>
//#include <string>
//#include <vector>
//
//// 管道名称，必须与 Host 端一致
//const std::wstring PIPE_NAME = L"\\\\.\\pipe\\WebView2VuePipe";
//const int BUFFER_SIZE = 4096;
//
//void ProcessClient(HANDLE hPipe) {
//    std::wcout << L"[Service] Client connected." << std::endl;
//
//    char buffer[BUFFER_SIZE];
//    DWORD bytesRead, bytesWritten;
//
//    while (true) {
//        // 1. 从管道读取数据 (来自 Host/Vue)
//        BOOL result = ReadFile(
//            hPipe,
//            buffer,
//            BUFFER_SIZE - 1, // 留一个字节给 null terminator
//            &bytesRead,
//            NULL
//        );
//
//        if (!result || bytesRead == 0) {
//            if (GetLastError() == ERROR_BROKEN_PIPE) {
//                std::wcout << L"[Service] Client disconnected." << std::endl;
//            }
//            else {
//                //std::cerr << L"[Service] ReadFile failed." << std::endl;
//            }
//            break;
//        }
//
//        buffer[bytesRead] = '\0';
//        std::string request(buffer);
//        std::cout << "[Service] Received request: " << request << std::endl;
//
//        // 2. 模拟业务逻辑处理 (例如：并在前面加上 "Processed: ")
//        std::string response = "Service Reply: " + request + " (ID: " + std::to_string(GetTickCount()) + ")";
//
//        // 模拟耗时操作
//        Sleep(500);
//
//        // 3. 将结果写回管道
//        result = WriteFile(
//            hPipe,
//            response.c_str(),
//            (DWORD)response.length(),
//            &bytesWritten,
//            NULL
//        );
//
//        if (!result) {
//            //std::cerr << L"[Service] WriteFile failed." << std::endl;
//            break;
//        }
//        std::cout << "[Service] Sent response." << std::endl;
//    }
//
//    FlushFileBuffers(hPipe);
//    DisconnectNamedPipe(hPipe);
//    CloseHandle(hPipe);
//}
//
//int main() {
//    std::wcout << L"[Service] Starting Named Pipe Server..." << std::endl;
//
//    while (true) {
//        // 创建命名管道实例
//        HANDLE hPipe = CreateNamedPipe(
//            PIPE_NAME.c_str(),
//            PIPE_ACCESS_DUPLEX,       // 双向读写
//            PIPE_TYPE_MESSAGE |       // 消息模式
//            PIPE_READMODE_MESSAGE |
//            PIPE_WAIT,                // 阻塞模式
//            1,                        // 最大实例数
//            BUFFER_SIZE,              // 输出缓冲区大小
//            BUFFER_SIZE,              // 输入缓冲区大小
//            0,                        // 默认超时
//            NULL                      // 默认安全属性
//        );
//
//        if (hPipe == INVALID_HANDLE_VALUE) {
//            //std::cerr << L"[Service] CreateNamedPipe failed. Error: " << GetLastError() << std::endl;
//            return 1;
//        }
//
//        std::wcout << L"[Service] Waiting for connection on " << PIPE_NAME << L"..." << std::endl;
//
//        // 等待客户端连接
//        bool connected = ConnectNamedPipe(hPipe, NULL) ? true : (GetLastError() == ERROR_PIPE_CONNECTED);
//
//        if (connected) {
//            ProcessClient(hPipe);
//        }
//        else {
//            CloseHandle(hPipe);
//        }
//    }
//
//    return 0;
//}


// =============================================================
// 文件名: BackendService.cpp (异步版本)
// 描述: 使用 Overlapped I/O 实现非阻塞管道通信
// 编译: 需要链接 Advapi32.lib
// =============================================================
#include <windows.h>
#include <iostream>
#include <string>
#include <vector>
#include <thread>

const std::wstring PIPE_NAME = L"\\\\.\\pipe\\WebView2VuePipe";
const int BUFFER_SIZE = 4096;

// 每个客户端连接的上下文
struct ClientContext {
    OVERLAPPED overlapped;
    HANDLE hPipe;
    char buffer[BUFFER_SIZE];
    DWORD bytesTransferred;
    bool isReading;
};

void ProcessRequest(ClientContext* ctx) {
    if (ctx->bytesTransferred == 0) return;

    ctx->buffer[ctx->bytesTransferred] = '\0';
    std::string request(ctx->buffer);
    std::cout << "[Service] Received: " << request << std::endl;

    // 模拟业务处理
    std::string response = "Service Reply: " + request +
        " (ID: " + std::to_string(GetTickCount()) + ")";

    // 异步写入响应
    DWORD bytesWritten;
    OVERLAPPED writeOv = { 0 };
    writeOv.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

    WriteFile(ctx->hPipe, response.c_str(), (DWORD)response.length(),
        &bytesWritten, &writeOv);

    // 等待写入完成
    WaitForSingleObject(writeOv.hEvent, INFINITE);
    CloseHandle(writeOv.hEvent);

    std::cout << "[Service] Sent response." << std::endl;
}

void HandleClient(HANDLE hPipe) {
    std::wcout << L"[Service] Client connected." << std::endl;

    ClientContext ctx = { 0 };
    ctx.hPipe = hPipe;
    ctx.isReading = true;
    ctx.overlapped.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

    while (true) {
        // 异步读取
        BOOL result = ReadFile(
            ctx.hPipe,
            ctx.buffer,
            BUFFER_SIZE - 1,
            &ctx.bytesTransferred,
            &ctx.overlapped
        );

        DWORD error = GetLastError();
        if (!result && error != ERROR_IO_PENDING) {
            if (error == ERROR_BROKEN_PIPE) {
                std::wcout << L"[Service] Client disconnected." << std::endl;
            }
            break;
        }

        // 等待读取完成
        if (!result) {
            DWORD waitResult = WaitForSingleObject(ctx.overlapped.hEvent, INFINITE);
            if (waitResult != WAIT_OBJECT_0) break;

            if (!GetOverlappedResult(ctx.hPipe, &ctx.overlapped, &ctx.bytesTransferred, FALSE)) {
                break;
            }
        }

        if (ctx.bytesTransferred > 0) {
            ProcessRequest(&ctx);
        }
    }

    CloseHandle(ctx.overlapped.hEvent);
    FlushFileBuffers(hPipe);
    DisconnectNamedPipe(hPipe);
    CloseHandle(hPipe);
}

int main() {
    std::wcout << L"[Service] Starting Named Pipe Server (Async Mode)..." << std::endl;

    while (true) {
        // 创建命名管道
        HANDLE hPipe = CreateNamedPipe(
            PIPE_NAME.c_str(),
            PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,  // 启用异步 I/O
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
            PIPE_UNLIMITED_INSTANCES,  // 允许多个实例
            BUFFER_SIZE,
            BUFFER_SIZE,
            0,
            NULL
        );

        if (hPipe == INVALID_HANDLE_VALUE) {
            std::wcerr << L"[Service] CreateNamedPipe failed. Error: "
                << GetLastError() << std::endl;
            return 1;
        }

        std::wcout << L"[Service] Waiting for connection..." << std::endl;

        // 等待客户端连接
        OVERLAPPED ov = { 0 };
        ov.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

        ConnectNamedPipe(hPipe, &ov);
        WaitForSingleObject(ov.hEvent, INFINITE);
        CloseHandle(ov.hEvent);

        // 为每个客户端创建单独的线程处理
        std::thread clientThread(HandleClient, hPipe);
        clientThread.detach();
    }

    return 0;
}