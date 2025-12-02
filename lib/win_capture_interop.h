#pragma once
#include <winrt/Windows.Graphics.Capture.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.capture.h>
#include <iostream>
#include <system_error>
#include <string>

class CaptureInterop {
public:
    static winrt::Windows::Graphics::Capture::GraphicsCaptureItem CreateCaptureItemForWindow(HWND hwnd) {
        try {
            auto interop_factory = winrt::get_activation_factory<winrt::Windows::Graphics::Capture::GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
            winrt::Windows::Graphics::Capture::GraphicsCaptureItem item = { nullptr };
            HRESULT hr = interop_factory->CreateForWindow(hwnd, winrt::guid_of<ABI::Windows::Graphics::Capture::IGraphicsCaptureItem>(), winrt::put_abi(item));

            if (FAILED(hr)) {
                std::cout << "[ERROR] CreateForWindow failed with HRESULT: 0x" << std::hex << hr << std::dec << std::endl;
                throw std::system_error(hr, std::system_category());
            }
            return item;
        }
        catch (const winrt::hresult_error& e) {
            std::string message = winrt::to_string(e.message());
            std::cout << "[ERROR] WinRT error in CreateCaptureItemForWindow: " << message
                      << " (HRESULT: 0x" << std::hex << e.code() << std::dec << ")" << std::endl;
            throw;
        }
        catch (const std::exception& e) {
            std::cout << "[ERROR] Exception in CreateCaptureItemForWindow: " << e.what() << std::endl;
            throw;
        }
    }

    static winrt::Windows::Graphics::Capture::GraphicsCaptureItem CreateCaptureItemForMonitor(HMONITOR hmon) {
        // 检查 HMONITOR 是否有效
        if (hmon == nullptr) {
            std::cout << "[ERROR] Invalid HMONITOR (nullptr)" << std::endl;
            throw std::invalid_argument("Invalid HMONITOR");
        }

        try {
            // 使用更安全的方式获取 factory
            auto interop_factory = winrt::try_get_activation_factory<winrt::Windows::Graphics::Capture::GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
            if (!interop_factory) {
                std::cout << "[ERROR] Failed to get activation factory for IGraphicsCaptureItemInterop" << std::endl;
                throw std::runtime_error("Activation factory not available");
            }
            winrt::Windows::Graphics::Capture::GraphicsCaptureItem item = { nullptr };
            HRESULT hr = interop_factory->CreateForMonitor(hmon, winrt::guid_of<ABI::Windows::Graphics::Capture::IGraphicsCaptureItem>(), winrt::put_abi(item));
            if (FAILED(hr)) {
                std::cout << "[ERROR] CreateForMonitor failed with HRESULT: 0x" << std::hex << hr << std::dec << std::endl;

                switch (hr) {
                    case E_INVALIDARG:
                        std::cout << "[ERROR] E_INVALIDARG - Invalid monitor handle" << std::endl;
                        break;
                    case E_ACCESSDENIED:
                        std::cout << "[ERROR] E_ACCESSDENIED - Access denied. Check permissions." << std::endl;
                        break;
                    case REGDB_E_CLASSNOTREG:
                        std::cout << "[ERROR] REGDB_E_CLASSNOTREG - Class not registered. Windows version may not support this feature." << std::endl;
                        break;
                    default:
                        std::cout << "[ERROR] Unknown error" << std::endl;
                }

                throw std::system_error(hr, std::system_category());
            }

            if (!item) {
                std::cout << "[ERROR] CreateForMonitor returned null item" << std::endl;
                throw std::runtime_error("CreateForMonitor returned null item");
            }

            auto size = item.Size();
            return item;
        }
        catch (const winrt::hresult_error& e) {
            std::string message = winrt::to_string(e.message());
            std::cout << "[ERROR] WinRT error in CreateCaptureItemForMonitor: " << message
                      << " (HRESULT: 0x" << std::hex << e.code() << std::dec << ")" << std::endl;
            throw;
        }
        catch (const std::exception& e) {
            std::cout << "[ERROR] Exception in CreateCaptureItemForMonitor: " << e.what() << std::endl;
            throw;
        }
    }

    // 添加 Windows 版本检查
    static bool IsMonitorCaptureSupported() {
        OSVERSIONINFOEX osvi = { sizeof(osvi) };
        DWORDLONG conditionMask = 0;

        // Windows Graphics Capture Monitor API 需要 Windows 10 版本 2004 (20H1) 或更高版本
        osvi.dwMajorVersion = 10;
        osvi.dwMinorVersion = 0;
        osvi.dwBuildNumber = 19041;  // 2004 版本

        VER_SET_CONDITION(conditionMask, VER_MAJORVERSION, VER_GREATER_EQUAL);
        VER_SET_CONDITION(conditionMask, VER_MINORVERSION, VER_GREATER_EQUAL);
        VER_SET_CONDITION(conditionMask, VER_BUILDNUMBER, VER_GREATER_EQUAL);

        return VerifyVersionInfo(&osvi, VER_MAJORVERSION | VER_MINORVERSION | VER_BUILDNUMBER, conditionMask) != FALSE;
    }

    // 安全的显示器捕获方法
    static winrt::Windows::Graphics::Capture::GraphicsCaptureItem CreateCaptureItemForMonitorSafe(HMONITOR hmon) {
        // 检查 Windows 版本兼容性
        if (!IsMonitorCaptureSupported()) {
            std::cout << "[ERROR] Monitor capture not supported on this Windows version" << std::endl;
            throw std::runtime_error("Monitor capture requires Windows 10 version 2004 or later");
        }

        // 检查显示器句柄有效性 - 使用 MONITORINFOEX 获取设备名称
        MONITORINFOEX monitorInfo = { sizeof(monitorInfo) };
        if (!GetMonitorInfo(hmon, &monitorInfo)) {
            DWORD error = GetLastError();
            std::cout << "[ERROR] Invalid monitor handle. GetMonitorInfo failed with error: " << error << std::endl;
            throw std::runtime_error("Invalid monitor handle");
        }

        try {
            return CreateCaptureItemForMonitor(hmon);
        }
        catch (...) {
            // 回退方案：使用桌面窗口捕获
            return CreateCaptureItemForDesktopWindow();
        }
    }

private:
    static winrt::Windows::Graphics::Capture::GraphicsCaptureItem CreateCaptureItemForDesktopWindow() {
        // 使用桌面窗口作为回退
        HWND desktopWindow = GetDesktopWindow();
        if (!desktopWindow || !IsWindow(desktopWindow)) {
            throw std::runtime_error("Cannot get valid desktop window");
        }
        return CreateCaptureItemForWindow(desktopWindow);
    }
};