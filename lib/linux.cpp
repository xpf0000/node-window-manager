#include <napi.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>

typedef Window HMONITOR;
typedef int DEVICE_SCALE_FACTOR;

struct Process {
    unsigned long pid;
    std::string path;
};

// 辅助函数：读取 X11 属性 (通用)
unsigned char* get_x11_property(Display* disp, Window w, Atom prop, Atom* type_return, int* format_return, unsigned long* nitems_return) {
    Atom type;
    int format;
    unsigned long nitems;
    unsigned long bytes_after;
    unsigned char* prop_return = NULL;

    XGetWindowProperty(disp, w, prop, 0, 1024, False, AnyPropertyType,
                       &type, &format, &nitems, &bytes_after, &prop_return);

    if (type_return) *type_return = type;
    if (format_return) *format_return = format;
    if (nitems_return) *nitems_return = nitems;

    return prop_return;
}

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
// info[0]: x, info[1]: y, info[2]: excludedId (可选)
Napi::Number getWindowAtPoint(const Napi::CallbackInfo& info) {
    Napi::Env env{ info.Env() };

    if (info.Length() < 2 || !info[0].IsNumber() || !info[1].IsNumber()) {
        Napi::TypeError::New(env, "Expected x and y coordinates (Number)").ThrowAsJavaScriptException();
        return Napi::Number::New(env, 0);
    }

    // 提取要排除的句柄 ID (可选参数)
    Window excludedId = 0;
    if (info.Length() > 2 && info[2].IsNumber()) {
        // XID 是 32 位，但从 Napi::Number 读取 64 位更安全
        excludedId = static_cast<Window>(info[2].As<Napi::Number>().Int64Value());
    }

    Display* display = XOpenDisplay(NULL);
    if (!display) return Napi::Number::New(env, 0);

    Window root = XDefaultRootWindow(display);

    // 1. 获取鼠标在根窗口上的坐标 (用于命中测试)
    Window root_return, child_return;
    int root_x, root_y;
    int win_x, win_y;
    unsigned int mask_return;
    // 使用传入的坐标进行检查，而不是实际鼠标位置（更灵活）
    // 但为了确保准确性，我们通常使用 XQueryPointer 获取当前鼠标位置。
    // 由于用户传入了 x, y，我们直接使用传入的 x, y 作为根坐标 (root_x, root_y)
    root_x = info[0].As<Napi::Number>().Int32Value();
    root_y = info[1].As<Napi::Number>().Int32Value();

    // 如果需要获取当前鼠标位置，请取消注释 XQueryPointer，并使用它的 root_x/root_y

    // 2. 获取按 Z-Order (堆叠顺序) 排序的窗口列表
    Atom client_list_stacking = XInternAtom(display, "_NET_CLIENT_LIST_STACKING", False);
    unsigned long nitems;
    unsigned char *prop = get_x11_property(display, root, client_list_stacking, NULL, NULL, &nitems);

    if (!prop) {
        XCloseDisplay(display);
        return Napi::Number::New(env, 0);
    }

    Window *windows = (Window*)prop;
    Window foundWindow = 0;

    // 3. 迭代 Z-Order (从顶层到底层)
    for (unsigned long i = 0; i < nitems; i++) {
        Window current = windows[i];

        // A. 检查排除条件
        if (excludedId != 0 && current == excludedId) {
            continue; // ID 匹配，跳过此窗口，检查下一层
        }

        // B. 检查窗口是否可见 (是否已映射)
        XWindowAttributes attr;
        if (!XGetWindowAttributes(display, current, &attr) || attr.map_state != IsViewable) {
            continue;
        }

        // C. 获取窗口的几何信息
        Window junk;
        int x_geom, y_geom;
        unsigned int width, height, border_width, depth;
        // 注意：XGetGeometry 获取的坐标是相对于 Root Window 的
        XGetGeometry(display, current, &junk, &x_geom, &y_geom, &width, &height, &border_width, &depth);

        // D. 执行命中测试 (检查传入的坐标是否在窗口几何范围内)
        if (root_x >= x_geom && root_x < (x_geom + (int)width) &&
            root_y >= y_geom && root_y < (y_geom + (int)height))
        {
            // 找到了一个符合条件的窗口 (命中, 可见, 且未被排除)
            foundWindow = current;
            break;
        }
    }

    XFree(prop); // 释放获取到的属性内存
    XCloseDisplay(display);

    return Napi::Number::New(env, static_cast<int64_t>(foundWindow));
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
