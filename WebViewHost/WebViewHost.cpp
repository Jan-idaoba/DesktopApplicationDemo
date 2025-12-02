//// =============================================================
//// 文件名: WebViewHost.cpp
//// 描述: Windows 标准窗口程序 (Win32)，嵌入 WebView2。
////       1. 初始化 WebView2 环境。
////       2. 连接 BackendService 的命名管道。
////       3. 实现双向通信桥梁。
//// 
//// 依赖: 
////   1. NuGet package: Microsoft.Web.WebView2
////   2. Linker: shlwapi.lib, user32.lib, kernel32.lib
//// =============================================================

#include <windows.h>
#include <stdlib.h>
#include <string>
#include <tchar.h>
#include <wrl.h>
#include <wil/com.h>
#include "WebView2.h"
#include <thread>
#include <atomic>
#include <queue>              // 消息队列
#include <mutex>              // 互斥锁
#include <condition_variable> // 条件变量
#include <cstdio>             // 用于 swprintf_s 调试输出

using namespace Microsoft::WRL;

// 全局变量
HINSTANCE hInst;
HWND hWndMain;
ComPtr<ICoreWebView2Controller> webviewController;
ComPtr<ICoreWebView2> webview;
HANDLE hPipe = INVALID_HANDLE_VALUE;
std::atomic<bool> isRunning(true);

// 线程同步对象 (用于发送队列)
std::queue<std::string> sendQueue;
std::mutex queueMutex;
std::condition_variable queueCv;

// 自定义消息
#define WM_PIPE_MESSAGE (WM_USER + 1)
const std::wstring PIPE_NAME = L"\\\\.\\pipe\\WebView2VuePipe";

// ---------------------------------------------------------
// 管道连接与通信 (底层)
// ---------------------------------------------------------

// 连接到 Service 的管道。如果已连接，直接返回 true。
bool ConnectToService() 
{
    if (hPipe != INVALID_HANDLE_VALUE) return true;

    // 尝试连接(最多等待 5 秒)
    const int MAX_RETRIES = 50;
    for (int i = 0; i < MAX_RETRIES; i++) {
        hPipe = CreateFile(
            PIPE_NAME.c_str(),
            GENERIC_READ | GENERIC_WRITE,
            0, NULL, OPEN_EXISTING,
            FILE_FLAG_OVERLAPPED,  // ★ 启用异步 I/O
            NULL
        );

        if (hPipe != INVALID_HANDLE_VALUE) {
            DWORD mode = PIPE_READMODE_MESSAGE;
            SetNamedPipeHandleState(hPipe, &mode, NULL, NULL);

            OutputDebugStringW(L"[Pipe Host] Connected to service!\n");
            return true;
        }

        DWORD error = GetLastError();
        if (error == ERROR_PIPE_BUSY) {
            // 管道忙,等待可用
            if (!WaitNamedPipe(PIPE_NAME.c_str(), 100)) {
                continue;
            }
        }
        else if (error != ERROR_FILE_NOT_FOUND) {
            wchar_t msg[256];
            swprintf_s(msg, L"[Pipe Host] CreateFile error: %lu\n", error);
            OutputDebugStringW(msg);
            return false;
        }

        Sleep(100);
    }
    return false;
}

// ---------------------------------------------------------
// 线程 1: 发送线程 (Consumer)
// 负责从队列取出消息并 WriteFile，阻塞不影响 UI
// ---------------------------------------------------------
void PipeWriterThread() 
{
    while (isRunning) {
        std::string msgToSend;

        {
            std::unique_lock<std::mutex> lock(queueMutex);
            queueCv.wait(lock, [] { return !sendQueue.empty() || !isRunning; });

            if (!isRunning) break;

            msgToSend = sendQueue.front();
            sendQueue.pop();
        }

        // 尝试发送(带重试)
        int retries = 3;
        while (retries-- > 0 && isRunning) {
            if (!ConnectToService()) {
                Sleep(500);
                continue;
            }

            DWORD bytesWritten;
            OVERLAPPED ov = { 0 };
            ov.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

            BOOL result = WriteFile(
                hPipe,
                msgToSend.c_str(),
                (DWORD)msgToSend.length(),
                &bytesWritten,
                &ov  // 使用 Overlapped I/O
            );

            if (!result) {
                DWORD error = GetLastError();
                if (error == ERROR_IO_PENDING) {
                    // 等待写入完成(最多 5 秒)
                    DWORD waitResult = WaitForSingleObject(ov.hEvent, 5000);
                    if (waitResult == WAIT_OBJECT_0) {
                        if (GetOverlappedResult(hPipe, &ov, &bytesWritten, FALSE)) {
                            OutputDebugStringW(L"[Pipe Host Writer] WriteFile success\n");
                            CloseHandle(ov.hEvent);
                            break;  // 成功,退出重试循环
                        }
                    }
                    else if (waitResult == WAIT_TIMEOUT) {
                        OutputDebugStringW(L"[Pipe Host Writer] WriteFile TIMEOUT\n");
                    }
                }

                // 写入失败,关闭管道重试
                wchar_t msg[256];
                swprintf_s(msg, L"[Pipe Host] WriteFile failed: %lu. Retrying...\n", GetLastError());
                OutputDebugStringW(msg);

                CloseHandle(ov.hEvent);
                CloseHandle(hPipe);
                hPipe = INVALID_HANDLE_VALUE;
                Sleep(500);
            }
            else {
                // 同步完成
                OutputDebugStringW(L"[Pipe Host Writer] WriteFile completed synchronously\n");
                CloseHandle(ov.hEvent);
                break;
            }
        }

        if (retries < 0) {
            OutputDebugStringW(L"[Pipe Host] Failed to send message after retries. Message lost.\n");
        }
    }
}

// ---------------------------------------------------------
// 线程 2: 读取线程
// 负责 ReadFile 并通知 UI
// ---------------------------------------------------------
void PipeReaderThread() 
{
    char buffer[4096];
    DWORD bytesRead;

    while (isRunning) {
        if (!ConnectToService()) {
            Sleep(1000);
            continue;
        }

        OVERLAPPED ov = { 0 };
        ov.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

        BOOL result = ReadFile(hPipe, buffer, sizeof(buffer) - 1, &bytesRead, &ov);

        if (!result) {
            DWORD error = GetLastError();
            if (error == ERROR_IO_PENDING) {
                // 等待读取完成(带超时)
                DWORD waitResult = WaitForSingleObject(ov.hEvent, 10000);
                if (waitResult == WAIT_OBJECT_0) {
                    if (GetOverlappedResult(hPipe, &ov, &bytesRead, FALSE)) {
                        buffer[bytesRead] = '\0';
                        std::string* msg = new std::string(buffer);
                        PostMessage(hWndMain, WM_PIPE_MESSAGE, 0, (LPARAM)msg);
                    }
                }
                else {
                    OutputDebugStringW(L"[Pipe Host Reader] ReadFile timeout\n");
                }
            }
            else {
                // 读取错误,断开重连
                CloseHandle(hPipe);
                hPipe = INVALID_HANDLE_VALUE;
            }
        }
        else {
            // 同步完成
            buffer[bytesRead] = '\0';
            std::string* msg = new std::string(buffer);
            PostMessage(hWndMain, WM_PIPE_MESSAGE, 0, (LPARAM)msg);
        }

        CloseHandle(ov.hEvent);
    }
}

// ---------------------------------------------------------
// UI 线程调用的发送函数 (Producer)
// 此函数运行在 UI 线程，负责快速将消息放入队列
// ---------------------------------------------------------
void SendToServiceAsync(const std::wstring& message) 
{
    // 宽字符转 UTF-8
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, message.c_str(), (int)message.length(), NULL, 0, NULL, NULL);
    std::string strTo(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, message.c_str(), (int)message.length(), &strTo[0], size_needed, NULL, NULL);

    // 将消息推入队列
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        sendQueue.push(strTo);
    }
    // 通知发送线程 (PipeWriterThread) 有新任务了
    queueCv.notify_one();
}

// ---------------------------------------------------------
// WebView2 初始化
// ---------------------------------------------------------

void InitializeWebView(HWND hWnd) 
{
    CreateCoreWebView2EnvironmentWithOptions(nullptr, nullptr, nullptr,
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [hWnd](HRESULT result, ICoreWebView2Environment* env) -> HRESULT {
                env->CreateCoreWebView2Controller(hWnd, Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                    [hWnd](HRESULT result, ICoreWebView2Controller* controller) -> HRESULT {
                        if (controller != nullptr) {
                            webviewController = controller;
                            webviewController->get_CoreWebView2(&webview);
                        }

                        RECT bounds;
                        GetClientRect(hWnd, &bounds);
                        webviewController->put_Bounds(bounds);

                        // 收到前端消息 (此回调在 UI 线程)
                        webview->add_WebMessageReceived(
                            Callback<ICoreWebView2WebMessageReceivedEventHandler>(
                                [](ICoreWebView2* sender, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
                                    LPWSTR pwStr;
                                    args->TryGetWebMessageAsString(&pwStr);
                                    if (pwStr) {
                                        // 调用异步发送函数，快速返回
                                        SendToServiceAsync(pwStr);
                                        CoTaskMemFree(pwStr);
                                    }
                                    return S_OK;
                                }).Get(), nullptr);

                        // 获取并加载 index.html (确保文件与 EXE 在同目录)
                        WCHAR path[MAX_PATH];
                        GetModuleFileName(NULL, path, MAX_PATH);
                        std::wstring exePath(path);
                        std::wstring htmlPath = exePath.substr(0, exePath.find_last_of(L"\\/")) + L"\\index.html";
                        webview->Navigate(htmlPath.c_str());

                        return S_OK;
                    }).Get());
                return S_OK;
            }).Get());
}

// ---------------------------------------------------------
// 窗口过程 (运行在主 UI 线程)
// ---------------------------------------------------------

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) 
{
    switch (message) {
    case WM_SIZE:
        if (webviewController != nullptr) {
            RECT bounds;
            GetClientRect(hWnd, &bounds);
            webviewController->put_Bounds(bounds);
        }
        break;

    case WM_PIPE_MESSAGE: // 处理来自 PipeReaderThread 的消息
    {
        std::string* msgPtr = (std::string*)lParam;
        if (msgPtr && webview) {
            // UTF-8 转 宽字符 (供 WebView2 使用)
            int len = MultiByteToWideChar(CP_UTF8, 0, msgPtr->c_str(), -1, NULL, 0);
            std::wstring wMsg(len, 0);
            MultiByteToWideChar(CP_UTF8, 0, msgPtr->c_str(), -1, &wMsg[0], len);

            // 发送给 Vue 前端
            webview->PostWebMessageAsString(wMsg.c_str());
            delete msgPtr; // 释放内存
        }
    }
    break;

    case WM_DESTROY:
        // 停止所有线程
        isRunning = false;
        queueCv.notify_all(); // 唤醒发送线程以便它退出
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

// ---------------------------------------------------------
// 入口点
// ---------------------------------------------------------

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow) 
{
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    hInst = hInstance;
    WNDCLASSEXW wcex = { 0 };
	wcex.cbSize = sizeof(WNDCLASSEX);
	wcex.style = CS_HREDRAW | CS_VREDRAW;
	wcex.lpfnWndProc = WndProc;
	wcex.hInstance = hInstance;
	wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
	wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
	wcex.lpszClassName = L"WebView2VueHost";
    RegisterClassExW(&wcex);
    hWndMain = CreateWindowW(L"WebView2VueHost", L"Native Vue App (Async Pipe)", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, 0, 1024, 768, nullptr, nullptr, hInstance, nullptr);

    if (!hWndMain) 
        return FALSE;

    ShowWindow(hWndMain, nCmdShow);
    UpdateWindow(hWndMain);

    InitializeWebView(hWndMain);

    // 启动读取线程
    std::thread reader(PipeReaderThread);
    reader.detach();

    // 启动发送线程 (新增)
    std::thread writer(PipeWriterThread);
    writer.detach();

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return (int)msg.wParam;
}