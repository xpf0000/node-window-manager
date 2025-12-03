#include <napi.h>
#include <string>
#include <windows.h>
#include <vector>
#include <iostream>
#include "win_capture_manager.h"
// 引入 DWM API 所需的头文件
#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib") // 编译时确保链接 dwmapi.lib

// 辅助函数和类型定义
struct Process {
    int pid;
    std::string path;
};

typedef int (__stdcall* lp_GetScaleFactorForMonitor) (HMONITOR, DEVICE_SCALE_FACTOR*);

template <typename T>
T getValueFromCallbackData(const Napi::CallbackInfo& info, unsigned handleIndex) {
    return reinterpret_cast<T>(info[handleIndex].As<Napi::Number>().Int64Value());
}

std::wstring get_wstring(const std::string str) {
    return std::wstring(str.begin(), str.end());
}

std::string toUtf8(const std::wstring& str) {
    std::string ret;
    int len = WideCharToMultiByte(CP_UTF8, 0, str.c_str(), str.length(), NULL, 0, NULL, NULL);
    if (len > 0) {
        ret.resize(len);
        WideCharToMultiByte(CP_UTF8, 0, str.c_str(), str.length(), &ret[0], len, NULL, NULL);
    }
    return ret;
}

Process getWindowProcess(HWND handle) {
    DWORD pid{ 0 };
    GetWindowThreadProcessId(handle, &pid);

    HANDLE pHandle{ OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, false, pid) };

    DWORD dwSize{ MAX_PATH };
    wchar_t exeName[MAX_PATH]{};

    QueryFullProcessImageNameW(pHandle, 0, exeName, &dwSize);

    CloseHandle(pHandle);

    auto wspath(exeName);
    auto path = toUtf8(wspath);

    return { static_cast<int>(pid), path };
}

HWND find_top_window(DWORD pid) {
    std::pair<HWND, DWORD> params = { 0, pid };

    BOOL bResult = EnumWindows(
        [](HWND hwnd, LPARAM lParam) -> BOOL {
            auto pParams = (std::pair<HWND, DWORD>*)(lParam);

            DWORD processId;
            if (GetWindowThreadProcessId(hwnd, &processId) && processId == pParams->second) {
                SetLastError(-1);
                pParams->first = hwnd;
                return FALSE;
            }

            return TRUE;
        },
        (LPARAM)&params);

    if (!bResult && GetLastError() == -1 && params.first) {
        return params.first;
    }

    return 0;
}

// 窗口管理函数
Napi::Number getProcessMainWindow(const Napi::CallbackInfo& info) {
    Napi::Env env{ info.Env() };

    unsigned long process_id = info[0].ToNumber().Uint32Value();

    auto handle = find_top_window(process_id);

    return Napi::Number::New(env, reinterpret_cast<int64_t>(handle));
}

Napi::Number createProcess(const Napi::CallbackInfo& info) {
    Napi::Env env{ info.Env() };

    auto path = info[0].ToString().Utf8Value();

    std::string cmd = "";

    if (info[1].IsString()) {
        cmd = info[1].ToString().Utf8Value();
    }

    STARTUPINFOA sInfo = { sizeof(sInfo) };
    PROCESS_INFORMATION processInfo;
    CreateProcessA(path.c_str(), &cmd[0], NULL, NULL, FALSE,
        CREATE_NEW_PROCESS_GROUP | CREATE_NEW_CONSOLE, NULL, NULL, &sInfo, &processInfo);

    return Napi::Number::New(env, processInfo.dwProcessId);
}

Napi::Number getActiveWindow(const Napi::CallbackInfo& info) {
    Napi::Env env{ info.Env() };

    auto handle = GetForegroundWindow();

    return Napi::Number::New(env, reinterpret_cast<int64_t>(handle));
}

std::vector<int64_t> _windows;

BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lparam) {
    _windows.push_back(reinterpret_cast<int64_t>(hwnd));
    return TRUE;
}

Napi::Array getWindows(const Napi::CallbackInfo& info) {
    Napi::Env env{ info.Env() };

    _windows.clear();
    EnumWindows(&EnumWindowsProc, NULL);

    auto arr = Napi::Array::New(env);
    auto i = 0;
    for (auto _win : _windows) {
        arr.Set(i++, Napi::Number::New(env, _win));
    }

    return arr;
}

std::vector<int64_t> _monitors;

BOOL CALLBACK EnumMonitorsProc(HMONITOR hMonitor, HDC hdcMonitor, LPRECT lprcMonitor, LPARAM dwData) {
    _monitors.push_back(reinterpret_cast<int64_t>(hMonitor));
    return TRUE;
}

Napi::Array getMonitors(const Napi::CallbackInfo& info) {
    Napi::Env env{ info.Env() };

    _monitors.clear();
    if (EnumDisplayMonitors(NULL, NULL, &EnumMonitorsProc, NULL)) {
        auto arr = Napi::Array::New(env);
        auto i = 0;

        for (auto _mon : _monitors) {
            arr.Set(i++, Napi::Number::New(env, _mon));
        }

        return arr;
    }

    return Napi::Array::New(env);
}

Napi::Number getMonitorFromWindow(const Napi::CallbackInfo& info) {
    Napi::Env env{ info.Env() };

    auto handle = getValueFromCallbackData<HWND>(info, 0);

    return Napi::Number::New(env, reinterpret_cast<int64_t>(MonitorFromWindow(handle, 0)));
}

Napi::Object initWindow(const Napi::CallbackInfo& info) {
    Napi::Env env{ info.Env() };

    auto handle{ getValueFromCallbackData<HWND>(info, 0) };
    auto process = getWindowProcess(handle);

    Napi::Object obj{ Napi::Object::New(env) };

    obj.Set("processId", process.pid);
    obj.Set("path", process.path);

    return obj;
}

Napi::Object getWindowBounds(const Napi::CallbackInfo& info) {
    Napi::Env env{ info.Env() };

    // 获取窗口句柄
    auto handle{ getValueFromCallbackData<HWND>(info, 0) };

    RECT rect{};
    // DWMWA_EXTENDED_FRAME_BOUNDS 的值是 9
    const int DWMWA_EXTENDED_FRAME_BOUNDS = 9;

    // 1. 尝试使用 DwmGetWindowAttribute 获取“扩展的框架边界”（不包含阴影）
    HRESULT hr = DwmGetWindowAttribute(
        handle,
        (DWORD)DWMWA_EXTENDED_FRAME_BOUNDS,
        &rect,
        sizeof(RECT)
    );

    // 2. 如果 DwmGetWindowAttribute 失败或不可用 (例如非 Vista+ 系统)，则回退到 GetWindowRect
    if (hr != S_OK) {
        GetWindowRect(handle, &rect);
    }

    // 3. 构建 Napi::Object 返回边界
    Napi::Object bounds{ Napi::Object::New(env) };

    bounds.Set("x", rect.left);
    bounds.Set("y", rect.top);
    bounds.Set("width", rect.right - rect.left);
    bounds.Set("height", rect.bottom - rect.top);

    return bounds;
}

Napi::String getWindowTitle(const Napi::CallbackInfo& info) {
    Napi::Env env{ info.Env() };

    auto handle{ getValueFromCallbackData<HWND>(info, 0) };

    int bufsize = GetWindowTextLengthW(handle) + 1;
    LPWSTR t = new WCHAR[bufsize];
    GetWindowTextW(handle, t, bufsize);

    std::wstring ws(t);
    std::string title = toUtf8(ws);
    title.insert(0, "--");

    delete[] t;

    return Napi::String::New(env, title);
}

Napi::String getWindowName(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    auto handle{ getValueFromCallbackData<HWND>(info, 0) };

    wchar_t name[256];

    GetWindowTextW(handle, name, sizeof(name) / sizeof(name[0]));

    std::wstring ws(name);
    std::string str(ws.begin(), ws.end());

    return Napi::String::New(env, str);
}

Napi::Number getWindowOpacity(const Napi::CallbackInfo& info) {
    Napi::Env env{ info.Env() };

    auto handle{ getValueFromCallbackData<HWND>(info, 0) };

    BYTE opacity{};
    GetLayeredWindowAttributes(handle, NULL, &opacity, NULL);

    return Napi::Number::New(env, static_cast<double>(opacity) / 255.);
}

Napi::Number getWindowOwner(const Napi::CallbackInfo& info) {
    Napi::Env env{ info.Env() };

    auto handle{ getValueFromCallbackData<HWND>(info, 0) };

    return Napi::Number::New(env, GetWindowLongPtrA(handle, GWLP_HWNDPARENT));
}

Napi::Number getMonitorScaleFactor(const Napi::CallbackInfo& info) {
    Napi::Env env{ info.Env() };

    HMODULE hShcore{ LoadLibraryA("SHcore.dll") };
    lp_GetScaleFactorForMonitor f{ (
        lp_GetScaleFactorForMonitor)GetProcAddress(hShcore, "GetScaleFactorForMonitor") };

    DEVICE_SCALE_FACTOR sf{};
    f(getValueFromCallbackData<HMONITOR>(info, 0), &sf);

    FreeLibrary(hShcore);

    return Napi::Number::New(env, static_cast<double>(sf) / 100.);
}

Napi::Boolean toggleWindowTransparency(const Napi::CallbackInfo& info) {
    Napi::Env env{ info.Env() };

    auto handle{ getValueFromCallbackData<HWND>(info, 0) };
    bool toggle{ info[1].As<Napi::Boolean>() };
    LONG_PTR style{ GetWindowLongPtrA(handle, GWL_EXSTYLE) };

    SetWindowLongPtrA(handle, GWL_EXSTYLE, ((toggle) ? (style | WS_EX_LAYERED) : (style & (~WS_EX_LAYERED))));

    return Napi::Boolean::New(env, true);
}

Napi::Boolean setWindowOpacity(const Napi::CallbackInfo& info) {
    Napi::Env env{ info.Env() };

    auto handle{ getValueFromCallbackData<HWND>(info, 0) };
    double opacity{ info[1].As<Napi::Number>().DoubleValue() };

    SetLayeredWindowAttributes(handle, NULL, opacity * 255., LWA_ALPHA);

    return Napi::Boolean::New(env, true);
}

Napi::Boolean setWindowBounds(const Napi::CallbackInfo& info) {
    Napi::Env env{ info.Env() };

    Napi::Object bounds{ info[1].As<Napi::Object>() };
    auto handle{ getValueFromCallbackData<HWND>(info, 0) };

    BOOL b{ MoveWindow(handle, bounds.Get("x").ToNumber(), bounds.Get("y").ToNumber(),
        bounds.Get("width").ToNumber(), bounds.Get("height").ToNumber(), true) };

    return Napi::Boolean::New(env, b);
}

Napi::Boolean setWindowParent(const Napi::CallbackInfo& info) {
    Napi::Env env{ info.Env() };

    auto handle{ getValueFromCallbackData<HWND>(info, 0) };
    auto newOwner{ getValueFromCallbackData<HWND>(info, 1) };

    RECT rect{};
    GetClientRect(newOwner, &rect);

    SetParent(handle, newOwner);
    SetWindowPos(handle, 0, rect.left, rect.top, rect.right, rect.bottom, 0);
    SetActiveWindow(handle);

    return Napi::Boolean::New(env, true);
}

Napi::Boolean showWindow(const Napi::CallbackInfo& info) {
    Napi::Env env{ info.Env() };

    auto handle{ getValueFromCallbackData<HWND>(info, 0) };
    std::string type{ info[1].As<Napi::String>() };

    DWORD flag{ 0 };

    if (type == "show")
        flag = SW_SHOW;
    else if (type == "hide")
        flag = SW_HIDE;
    else if (type == "minimize")
        flag = SW_MINIMIZE;
    else if (type == "restore")
        flag = SW_RESTORE;
    else if (type == "maximize")
        flag = SW_MAXIMIZE;

    return Napi::Boolean::New(env, ShowWindow(handle, flag));
}

Napi::Boolean bringWindowToTop(const Napi::CallbackInfo& info) {
    Napi::Env env{ info.Env() };
    auto handle{ getValueFromCallbackData<HWND>(info, 0) };
    BOOL b{ SetForegroundWindow(handle) };

    HWND hCurWnd = ::GetForegroundWindow();
    DWORD dwMyID = ::GetCurrentThreadId();
    DWORD dwCurID = ::GetWindowThreadProcessId(hCurWnd, NULL);
    ::AttachThreadInput(dwCurID, dwMyID, TRUE);
    ::SetWindowPos(handle, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE);
    ::SetWindowPos(handle, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE);
    ::SetForegroundWindow(handle);
    ::AttachThreadInput(dwCurID, dwMyID, FALSE);
    ::SetFocus(handle);
    ::SetActiveWindow(handle);

    return Napi::Boolean::New(env, b);
}

Napi::Boolean redrawWindow(const Napi::CallbackInfo& info) {
    Napi::Env env{ info.Env() };

    auto handle{ getValueFromCallbackData<HWND>(info, 0) };
    BOOL b{ SetWindowPos(handle, 0, 0, 0, 0, 0,
        SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
        SWP_NOOWNERZORDER | SWP_NOACTIVATE | SWP_DRAWFRAME | SWP_NOCOPYBITS) };

    return Napi::Boolean::New(env, b);
}

Napi::Boolean isWindow(const Napi::CallbackInfo& info) {
    Napi::Env env{ info.Env() };

    auto handle{ getValueFromCallbackData<HWND>(info, 0) };

    return Napi::Boolean::New(env, IsWindow(handle));
}

Napi::Boolean isWindowVisible(const Napi::CallbackInfo& info) {
    Napi::Env env{ info.Env() };

    auto handle{ getValueFromCallbackData<HWND>(info, 0) };

    return Napi::Boolean::New(env, IsWindowVisible(handle));
}

Napi::Object getMonitorInfo(const Napi::CallbackInfo& info) {
    Napi::Env env{ info.Env() };

    auto handle{ getValueFromCallbackData<HMONITOR>(info, 0) };

    MONITORINFO mInfo;
    mInfo.cbSize = sizeof(MONITORINFO);
    GetMonitorInfoA(handle, &mInfo);

    Napi::Object bounds{ Napi::Object::New(env) };

    bounds.Set("x", mInfo.rcMonitor.left);
    bounds.Set("y", mInfo.rcMonitor.top);
    bounds.Set("width", mInfo.rcMonitor.right - mInfo.rcMonitor.left);
    bounds.Set("height", mInfo.rcMonitor.bottom - mInfo.rcMonitor.top);

    Napi::Object workArea{ Napi::Object::New(env) };

    workArea.Set("x", mInfo.rcWork.left);
    workArea.Set("y", mInfo.rcWork.top);
    workArea.Set("width", mInfo.rcWork.right - mInfo.rcWork.left);
    workArea.Set("height", mInfo.rcWork.bottom - mInfo.rcWork.top);

    Napi::Object obj{ Napi::Object::New(env) };

    obj.Set("bounds", bounds);
    obj.Set("workArea", workArea);
    obj.Set("isPrimary", (mInfo.dwFlags & MONITORINFOF_PRIMARY) != 0);

    return obj;
}

Napi::Boolean hideInstantly(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    if (info.Length() < 1 || !info[0].IsNumber()) {
        Napi::TypeError::New(env, "Number expected").ThrowAsJavaScriptException();
        return Napi::Boolean::New(env, false);
    }

    int64_t handleValue = info[0].As<Napi::Number>().Int64Value();
    HWND handle = reinterpret_cast<HWND>(handleValue);

    LONG styles = GetWindowLong(handle, GWL_STYLE);
    LONG exStyles = GetWindowLong(handle, GWL_EXSTYLE);

    SetWindowLong(handle, GWL_STYLE, styles & ~WS_OVERLAPPEDWINDOW);
    SetWindowLong(handle, GWL_EXSTYLE, exStyles & ~(WS_EX_LAYERED | WS_EX_TRANSPARENT));

    BOOL result = ShowWindow(handle, SW_HIDE);

    return Napi::Boolean::New(env, result);
}

Napi::Boolean forceWindowPaint(const Napi::CallbackInfo& info) {
    Napi::Env env{ info.Env() };

    if (info.Length() < 1 || !info[0].IsNumber()) {
        Napi::TypeError::New(env, "Number expected").ThrowAsJavaScriptException();
        return Napi::Boolean::New(env, false);
    }

    int64_t handleValue = info[0].As<Napi::Number>().Int64Value();
    HWND handle = reinterpret_cast<HWND>(handleValue);

    BOOL b{ RedrawWindow(handle, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW) };

    return Napi::Boolean::New(env, b);
}

Napi::Boolean setWindowAsPopup(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    if (info.Length() < 1 || !info[0].IsNumber()) {
        Napi::TypeError::New(env, "Number expected").ThrowAsJavaScriptException();
        return Napi::Boolean::New(env, false);
    }

    int64_t handleValue = info[0].As<Napi::Number>().Int64Value();
    HWND handle = reinterpret_cast<HWND>(handleValue);

    LONG lStyle = GetWindowLongPtr(handle, GWL_STYLE);
    lStyle &= ~(WS_CAPTION | WS_THICKFRAME | WS_MINIMIZE | WS_MAXIMIZE | WS_SYSMENU);
    lStyle |= WS_POPUP;

    SetWindowLongPtr(handle, GWL_STYLE, lStyle);

    return Napi::Boolean::New(env, true);
}

Napi::Boolean setWindowAsPopupWithRoundedCorners(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    if (info.Length() < 1 || !info[0].IsNumber()) {
        Napi::TypeError::New(env, "Number expected").ThrowAsJavaScriptException();
        return Napi::Boolean::New(env, false);
    }

    int64_t handleValue = info[0].As<Napi::Number>().Int64Value();
    HWND handle = reinterpret_cast<HWND>(handleValue);

    LONG lStyle = GetWindowLongPtr(handle, GWL_STYLE);
    LONG lExStyle = GetWindowLongPtr(handle, GWL_EXSTYLE);

    lStyle &= ~(WS_CAPTION | WS_THICKFRAME | WS_MINIMIZE | WS_MAXIMIZE | WS_SYSMENU);
    lStyle |= WS_POPUP;
    lExStyle |= WS_EX_COMPOSITED;

    SetWindowLongPtr(handle, GWL_STYLE, lStyle);
    SetWindowLongPtr(handle, GWL_EXSTYLE, lExStyle);

    DWM_WINDOW_CORNER_PREFERENCE preference = DWMWCP_ROUND;
    DwmSetWindowAttribute(handle, DWMWA_WINDOW_CORNER_PREFERENCE, &preference, sizeof(preference));

    RedrawWindow(handle, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW);

    return Napi::Boolean::New(env, true);
}

Napi::Boolean showInstantly(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    if (info.Length() < 1 || !info[0].IsNumber()) {
        Napi::TypeError::New(env, "Number expected").ThrowAsJavaScriptException();
        return Napi::Boolean::New(env, false);
    }

    int64_t handleValue = info[0].As<Napi::Number>().Int64Value();
    HWND handle = reinterpret_cast<HWND>(handleValue);

    ANIMATIONINFO animationInfo = { sizeof(animationInfo) };
    animationInfo.iMinAnimate = 0;
    SystemParametersInfo(SPI_SETANIMATION, sizeof(animationInfo), &animationInfo, 0);

    SetWindowPos(handle, NULL, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_SHOWWINDOW | SWP_FRAMECHANGED);

    SetForegroundWindow(handle);
    SetActiveWindow(handle);

    animationInfo.iMinAnimate = 1;
    SystemParametersInfo(SPI_SETANIMATION, sizeof(animationInfo), &animationInfo, 0);

    RedrawWindow(handle, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);

    return Napi::Boolean::New(env, true);
}

Napi::Number getWindowAtPoint(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    if (info.Length() < 2 || !info[0].IsNumber() || !info[1].IsNumber()) {
        Napi::TypeError::New(env, "Expected x and y coordinates (Number)").ThrowAsJavaScriptException();
        return Napi::Number::New(env, 0);
    }

    long x = info[0].As<Napi::Number>().Int32Value();
    long y = info[1].As<Napi::Number>().Int32Value();
    POINT pt = { x, y };

    HWND excludedWindow = NULL;
    if (info.Length() > 2 && info[2].IsNumber()) {
        int64_t excludedHandle = info[2].As<Napi::Number>().Int64Value();
        excludedWindow = reinterpret_cast<HWND>(excludedHandle);
    }

    HWND targetWindow = NULL;

    // 1. 首先尝试最快速的系统 API
    HWND hitWindow = WindowFromPoint(pt);

    // 获取实际的顶层窗口 (因为 WindowFromPoint 可能会返回按钮等子控件)
    if (hitWindow != NULL) {
        HWND root = GetAncestor(hitWindow, GA_ROOT);
        if (root != NULL) {
            hitWindow = root;
        }
    }

    // 2. 如果命中的不是被忽略的窗口，直接返回结果
    if (excludedWindow == NULL || hitWindow != excludedWindow) {
        targetWindow = hitWindow;
    }
    else {
        // 3. 如果命中了被忽略的窗口，我们需要“手动”寻找它下方的窗口
        // 从 excludedWindow 的下一个 Z 序窗口开始遍历
        HWND current = GetWindow(excludedWindow, GW_HWNDNEXT);

        while (current != NULL) {
            // 必须是可见的窗口
            if (IsWindowVisible(current)) {
                RECT rc;
                GetWindowRect(current, &rc);

                // 检查点是否在矩形内
                if (PtInRect(&rc, pt)) {
                   targetWindow = current;
                   break; // 找到了！
                }
            }
            // 继续找下一个
            current = GetWindow(current, GW_HWNDNEXT);
        }
    }

    // 最后的安全检查：确保返回的是顶层窗口句柄
    if (targetWindow != NULL) {
        HWND root = GetAncestor(targetWindow, GA_ROOT);
        if (root != NULL) targetWindow = root;
    }

    return Napi::Number::New(env, reinterpret_cast<int64_t>(targetWindow));
}

// 获取桌面窗口句柄ID
Napi::Value getDesktopWindow(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    try {
        // 获取桌面窗口句柄
        HWND desktopHwnd = GetDesktopWindow();

        if (!desktopHwnd || !IsWindow(desktopHwnd)) {
            Napi::Error::New(env, "无法获取桌面窗口句柄").ThrowAsJavaScriptException();
            return env.Null();
        }

        // 验证窗口信息
        RECT rect;
        if (!GetWindowRect(desktopHwnd, &rect)) {
            Napi::Error::New(env, "无法获取桌面窗口区域").ThrowAsJavaScriptException();
            return env.Null();
        }

        int width = rect.right - rect.left;
        int height = rect.bottom - rect.top;

        std::cout << "[DEBUG] 桌面窗口句柄: " << desktopHwnd
                  << ", 尺寸: " << width << "x" << height << std::endl;

        // 返回句柄的整数值（确保在64位系统上正确处理）
        #ifdef _WIN64
            int64_t handleValue = reinterpret_cast<int64_t>(desktopHwnd);
        #else
            int32_t handleValue = reinterpret_cast<int32_t>(desktopHwnd);
        #endif

        return Napi::Number::New(env, handleValue);

    } catch (const std::exception& e) {
        Napi::Error::New(env, std::string("获取桌面句柄失败: ") + e.what()).ThrowAsJavaScriptException();
        return env.Null();
    } catch (...) {
        Napi::Error::New(env, "未知错误").ThrowAsJavaScriptException();
        return env.Null();
    }
}

// 导出的清理函数
Napi::Value CleanupInvalidWindowsExport(const Napi::CallbackInfo& info) {
    return info.Env().Undefined();
}

/**
 * 设置指定句柄的窗口全屏覆盖，并将其置于最顶层（覆盖任务栏）。
 * @param info Napi::CallbackInfo，包含窗口句柄ID (Number)。
 * @return Napi::Boolean，表示操作是否成功。
 */
Napi::Boolean setWindowFullScreenCover(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    // 1. 检查输入参数
    if (info.Length() < 1 || !info[0].IsNumber()) {
        Napi::TypeError::New(env, "Expected a window handle (Number)").ThrowAsJavaScriptException();
        return Napi::Boolean::New(env, false);
    }

    // 2. 转换窗口句柄
    int64_t handleValue = info[0].As<Napi::Number>().Int64Value();
    HWND targetWindow = reinterpret_cast<HWND>(handleValue);

    if (targetWindow == NULL || !IsWindow(targetWindow)) {
        std::cout << "[DEBUG] setWindowFullScreenCover - 无效的窗口句柄!" << std::endl;
        return Napi::Boolean::New(env, false);
    }

    // 3. 获取屏幕尺寸
    int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);

    // 4. 关键步骤：修改窗口扩展样式
    LONG_PTR extendedStyle = GetWindowLongPtr(targetWindow, GWL_EXSTYLE);

    // 添加必要的扩展样式
    extendedStyle |= WS_EX_TOOLWINDOW;    // 设置为工具窗口，避免任务栏显示
    extendedStyle |= WS_EX_LAYERED;        // 支持分层窗口（透明度）
    extendedStyle |= WS_EX_TOPMOST;        // 顶层窗口

    SetWindowLongPtr(targetWindow, GWL_EXSTYLE, extendedStyle);

    // 5. 修改窗口样式（移除标题栏、边框等）
    LONG_PTR style = GetWindowLongPtr(targetWindow, GWL_STYLE);
    style &= ~WS_CAPTION;     // 移除标题栏
    style &= ~WS_THICKFRAME;  // 移除可调整大小的边框
    style &= ~WS_SYSMENU;     // 移除系统菜单
    style |= WS_POPUP;        // 设置为弹出窗口

    SetWindowLongPtr(targetWindow, GWL_STYLE, style);

    // 6. 设置分层窗口属性（支持透明度）
    SetLayeredWindowAttributes(targetWindow, 0, 255, LWA_ALPHA);

    // 7. 设置窗口位置、大小和 Z-Order
    BOOL success = SetWindowPos(
        targetWindow,
        HWND_TOPMOST,
        0, 0, screenWidth, screenHeight,
        SWP_SHOWWINDOW | SWP_FRAMECHANGED
    );

    // 8. 强制重绘窗口
    RedrawWindow(targetWindow, NULL, NULL,
                RDW_ERASE | RDW_FRAME | RDW_INVALIDATE | RDW_ALLCHILDREN);

    return Napi::Boolean::New(env, success != 0);
}

// 模块初始化函数
Napi::Object Init(Napi::Env env, Napi::Object exports) {
    // 窗口管理函数导出
    exports.Set(Napi::String::New(env, "getActiveWindow"), Napi::Function::New(env, getActiveWindow));
    exports.Set(Napi::String::New(env, "getMonitorFromWindow"), Napi::Function::New(env, getMonitorFromWindow));
    exports.Set(Napi::String::New(env, "getMonitorScaleFactor"), Napi::Function::New(env, getMonitorScaleFactor));
    exports.Set(Napi::String::New(env, "setWindowBounds"), Napi::Function::New(env, setWindowBounds));
    exports.Set(Napi::String::New(env, "showWindow"), Napi::Function::New(env, showWindow));
    exports.Set(Napi::String::New(env, "bringWindowToTop"), Napi::Function::New(env, bringWindowToTop));
    exports.Set(Napi::String::New(env, "redrawWindow"), Napi::Function::New(env, redrawWindow));
    exports.Set(Napi::String::New(env, "isWindow"), Napi::Function::New(env, isWindow));
    exports.Set(Napi::String::New(env, "isWindowVisible"), Napi::Function::New(env, isWindowVisible));
    exports.Set(Napi::String::New(env, "setWindowOpacity"), Napi::Function::New(env, setWindowOpacity));
    exports.Set(Napi::String::New(env, "toggleWindowTransparency"), Napi::Function::New(env, toggleWindowTransparency));
    exports.Set(Napi::String::New(env, "setWindowParent"), Napi::Function::New(env, setWindowParent));
    exports.Set(Napi::String::New(env, "initWindow"), Napi::Function::New(env, initWindow));
    exports.Set(Napi::String::New(env, "getWindowBounds"), Napi::Function::New(env, getWindowBounds));
    exports.Set(Napi::String::New(env, "getWindowTitle"), Napi::Function::New(env, getWindowTitle));
    exports.Set(Napi::String::New(env, "getWindowName"), Napi::Function::New(env, getWindowName));
    exports.Set(Napi::String::New(env, "getWindowOwner"), Napi::Function::New(env, getWindowOwner));
    exports.Set(Napi::String::New(env, "getWindowOpacity"), Napi::Function::New(env, getWindowOpacity));
    exports.Set(Napi::String::New(env, "getMonitorInfo"), Napi::Function::New(env, getMonitorInfo));
    exports.Set(Napi::String::New(env, "getWindows"), Napi::Function::New(env, getWindows));
    exports.Set(Napi::String::New(env, "getMonitors"), Napi::Function::New(env, getMonitors));
    exports.Set(Napi::String::New(env, "createProcess"), Napi::Function::New(env, createProcess));
    exports.Set(Napi::String::New(env, "getProcessMainWindow"), Napi::Function::New(env, getProcessMainWindow));
    exports.Set(Napi::String::New(env, "forceWindowPaint"), Napi::Function::New(env, forceWindowPaint));
    exports.Set(Napi::String::New(env, "hideInstantly"), Napi::Function::New(env, hideInstantly));
    exports.Set(Napi::String::New(env, "setWindowAsPopup"), Napi::Function::New(env, setWindowAsPopup));
    exports.Set(Napi::String::New(env, "setWindowAsPopupWithRoundedCorners"), Napi::Function::New(env, setWindowAsPopupWithRoundedCorners));
    exports.Set(Napi::String::New(env, "showInstantly"), Napi::Function::New(env, showInstantly));
    exports.Set(Napi::String::New(env, "getWindowAtPoint"), Napi::Function::New(env, getWindowAtPoint));

    // 截图功能导出
    exports.Set(Napi::String::New(env, "captureWindow"), Napi::Function::New(env, captureWindow));

    exports.Set(Napi::String::New(env, "cleanup"), Napi::Function::New(env, CleanupInvalidWindowsExport));

    exports.Set(Napi::String::New(env, "setWindowFullScreenCover"), Napi::Function::New(env, setWindowFullScreenCover));

    exports.Set(Napi::String::New(env, "getDesktopWindow"), Napi::Function::New(env, getDesktopWindow));

    return exports;
}

NODE_API_MODULE(addon, Init)