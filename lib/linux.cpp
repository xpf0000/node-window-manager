#include <napi.h>

Napi::Number getProcessMainWindow (const Napi::CallbackInfo& info) {
    throw "Not implemented on Linux";
}

Napi::Number createProcess (const Napi::CallbackInfo& info) {
    throw "Not implemented on Linux";
}


Napi::Number getActiveWindow (const Napi::CallbackInfo& info) {
    throw "Not implemented on Linux";
}

Napi::Object getWindowBounds (const Napi::CallbackInfo& info) {
    throw "Not implemented on Linux";
}

Napi::Boolean setWindowBounds (const Napi::CallbackInfo& info) {
    throw "Not implemented on Linux";
}

Napi::Boolean showWindow (const Napi::CallbackInfo& info) {
    throw "Not implemented on Linux";
}

Napi::Boolean isWindow (const Napi::CallbackInfo& info) {
    throw "Not implemented on Linux";
}

// --- 新增功能: 获取指定坐标下的顶层窗口句柄 (Linux/X11) ---
// info[0]: x, info[1]: y, info[2]: excludedId (可选)
Napi::Number getWindowAtPoint(const Napi::CallbackInfo& info) {
    throw "Not implemented on Linux";
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
