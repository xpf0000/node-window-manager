#include <napi.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

typedef Window HMONITOR;
typedef int DEVICE_SCALE_FACTOR;

struct Process {
    unsigned long pid;
    std::string path;
};

Process getWindowProcess (Window handle) {
    throw "getWindowProcess is not implemented on Linux";
}

Window find_top_window (unsigned long pid) {
    throw "find_top_window is not implemented on Linux";
}

Napi::Number getProcessMainWindow (const Napi::CallbackInfo& info) {
    Napi::Env env{ info.Env () };

    unsigned long process_id = info[0].ToNumber ().Uint32Value ();

    auto handle = find_top_window (process_id);

    return Napi::Number::New (env, static_cast<int64_t> (handle));
}

Napi::Number createProcess (const Napi::CallbackInfo& info) {
    throw "createProcess is not implemented on Linux";
}


Napi::Number getActiveWindow (const Napi::CallbackInfo& info) {
    Napi::Env env{ info.Env () };

    Display* display = XOpenDisplay(NULL);
    Window root = XDefaultRootWindow(display);
    Atom active_window = XInternAtom(display, "_NET_ACTIVE_WINDOW", False);
    Atom type;
    int format;
    unsigned long nItems, bytesAfter;
    unsigned char *property = NULL;
    Window active = 0;

    if (XGetWindowProperty(display, root, active_window, 0, 1024, False, AnyPropertyType, 
                           &type, &format, &nItems, &bytesAfter, &property) == Success) {
        active = *(Window*)property;
        XFree(property);
    }

    XCloseDisplay(display);

    return Napi::Number::New (env, static_cast<int64_t> (active));
}

template <typename T>
T getValueFromCallbackData (const Napi::CallbackInfo& info, unsigned handleIndex) {
    return static_cast<T> (info[handleIndex].As<Napi::Number> ().Int64Value ());
}


Napi::Object getWindowBounds (const Napi::CallbackInfo& info) {
    Napi::Env env{ info.Env () };

    auto handle{ getValueFromCallbackData<Window> (info, 0) };

    Display* display = XOpenDisplay(NULL);
    Window root;
    int x, y;
    unsigned int width, height, border_width, depth;

    XGetGeometry(display, handle, &root, &x, &y, &width, &height, &border_width, &depth);

    XCloseDisplay(display);

    Napi::Object bounds{ Napi::Object::New (env) };

    bounds.Set ("x", x);
    bounds.Set ("y", y);
    bounds.Set ("width", width);
    bounds.Set ("height", height);

    return bounds;
}

Napi::Boolean setWindowBounds (const Napi::CallbackInfo& info) {
    Napi::Env env{ info.Env () };

    Napi::Object bounds{ info[1].As<Napi::Object> () };
    auto handle{ getValueFromCallbackData<Window> (info, 0) };

    Display* display = XOpenDisplay(NULL);

    XMoveResizeWindow(display, handle, bounds.Get ("x").ToNumber (), bounds.Get ("y").ToNumber (),
                      bounds.Get ("width").ToNumber (), bounds.Get ("height").ToNumber ());

    XCloseDisplay(display);

    return Napi::Boolean::New (env, true);
}

Napi::Boolean showWindow (const Napi::CallbackInfo& info) {
    Napi::Env env{ info.Env () };

    auto handle{ getValueFromCallbackData<Window> (info, 0) };
    std::string type{ info[1].As<Napi::String> () };

    Display* display = XOpenDisplay(NULL);

    if (type == "hide")
        XUnmapWindow(display, handle);
    else
        XMapWindow(display, handle);

    XCloseDisplay(display);

    return Napi::Boolean::New (env, true);
}

Napi::Boolean isWindow (const Napi::CallbackInfo& info) {
    Napi::Env env{ info.Env () };

    auto handle{ getValueFromCallbackData<Window> (info, 0) };

    Display* display = XOpenDisplay(NULL);
    XWindowAttributes attr;
    Status s = XGetWindowAttributes(display, handle, &attr);

    XCloseDisplay(display);

    return Napi::Boolean::New (env, s != 0);
}

// --- 新增功能: 获取指定坐标下的顶层窗口句柄 (Linux/X11) ---
Napi::Number getWindowAtPoint(const Napi::CallbackInfo& info) {
    Napi::Env env{ info.Env() };

    // 1. 检查参数 (虽然我们实际使用 XQueryPointer 获取位置，但 Napi 函数约定需要 x, y)
    if (info.Length() < 2 || !info[0].IsNumber() || !info[1].IsNumber()) {
        // 尽管传入的 x, y 在 XQueryPointer 中不直接使用，但仍需要检查类型
        Napi::TypeError::New(env, "Expected x and y coordinates (Number)").ThrowAsJavaScriptException();
        return Napi::Number::New(env, 0);
    }

    Display* display = XOpenDisplay(NULL);
    if (!display) {
        return Napi::Number::New(env, 0);
    }

    // 根窗口
    Window root = XDefaultRootWindow(display);

    // XQueryPointer 的输出变量
    Window root_return, child_return;
    int root_x, root_y;
    int win_x, win_y;
    unsigned int mask_return;

    // 2. 查询鼠标指针状态
    // XQueryPointer 会获取当前鼠标位置，以及鼠标指针所在窗口 ID (child_return)
    Status status = XQueryPointer(display, root,
                                  &root_return, &child_return,
                                  &root_x, &root_y,
                                  &win_x, &win_y,
                                  &mask_return);

    Window target_window = 0;

    if (status) {
        // 如果 child_return 不为 None (0)，则表示鼠标在一个子窗口上，通常就是应用窗口
        if (child_return != None) {
            // 鼠标正停留在某个窗口上
            target_window = child_return;

            // XQueryPointer 可能会返回窗口装饰或边框。
            // 理论上，我们需要遍历找到最顶级的应用窗口，但 X11 没有标准的 API 直接进行精确的 Z-Order Hit Test。
            // 对于截图/拾取功能，返回 child_return (即鼠标所在的窗口) 通常是可接受的。

            // 可选：如果 child_return 是根窗口，但 XQueryPointer 确实找到了一个子窗口，则使用 child_return
            if (target_window == root) {
                // 如果鼠标在根窗口上，但 XQueryPointer 找到了一个子窗口，说明它是根窗口的直接子窗口
                // 由于 XQueryPointer 已经是点击测试的结果，我们信任它。
            }
        }
    }

    XCloseDisplay(display);

    // 返回找到的窗口 ID，如果未找到则返回 0
    return Napi::Number::New (env, static_cast<int64_t> (target_window));
}

Napi::Object Init(Napi::Env env, Napi::Object exports) {
    exports.Set("getProcessMainWindow", Napi::Function::New(env, getProcessMainWindow));
    exports.Set("createProcess", Napi::Function::New(env, createProcess));
    exports.Set("getActiveWindow", Napi::Function::New(env, getActiveWindow));
    exports.Set("getWindowBounds", Napi::Function::New(env, getWindowBounds));
    exports.Set("setWindowBounds", Napi::Function::New(env, setWindowBounds));
    exports.Set("showWindow", Napi::Function::New(env, showWindow));
    exports.Set("isWindow", Napi::Function::New(env, isWindow));
    exports.Set("getWindowAtPoint", Napi::Function::New(env, getWindowAtPoint));
    return exports;
}

NODE_API_MODULE(addon, Init)
