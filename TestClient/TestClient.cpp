#include "framework.h"
#include "TestClient.h"
#include "Log\Logger.h"
#include "Log\LogMacros.h"

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
                     _In_opt_ HINSTANCE hPrevInstance,
                     _In_ LPWSTR    lpCmdLine,
                     _In_ int       nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    Logger::Init("logs");
    LOG_INFO("Application started");

    // 检查命令行参数
    if (wcsstr(lpCmdLine, L"-debug") != nullptr)
    {
        // 以调试模式运行
        LOG_INFO("Running in debug mode");


        //SerialPortMonitor monitor;
        //monitor.Initialize();
        //ServiceNS::COMService service;
        //monitor.SetServiceObject(&service);
        //service.OnStart(0, nullptr);
    }
    else
    {
        //SerialPortMonitor monitor;
        //monitor.Initialize();
        //ServiceNS::COMService service;
        //monitor.SetServiceObject(&service);
        //ServiceNS::ServiceBase::Run(service);
    }
    return 0;
}