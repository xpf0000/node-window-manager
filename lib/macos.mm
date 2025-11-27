#include <Availability.h>
#include <sys/types.h> // 用于 pid_t
#import <Foundation/Foundation.h>
#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>
#include <napi.h>
#include <string>
#include <map>
#include <vector>
#include <cmath>
#include <iostream>
#include <Cocoa/Cocoa.h>
#include <CoreGraphics/CoreGraphics.h>
#include <ApplicationServices/ApplicationServices.h>

// CGWindowID to AXUIElementRef windows map
std::map<int, AXUIElementRef> windowsMap;

// --- 辅助工具函数 ---

// macOS 版本检查工具函数
bool IsAtLeastMacOSVersion(int major, int minor = 0) {
    if (@available(macOS 10.10, *)) {
        NSOperatingSystemVersion version = [[NSProcessInfo processInfo] operatingSystemVersion];
        return (version.majorVersion > major) ||
               (version.majorVersion == major && version.minorVersion >= minor);
    }
    return true;
}

// 辅助功能权限检查
bool _requestAccessibility(bool showDialog) {
    if (@available(macOS 10.9, *)) {
        NSDictionary* opts = @{(id)kAXTrustedCheckOptionPrompt: @(showDialog)};
        return AXIsProcessTrustedWithOptions((__bridge CFDictionaryRef)opts);
    } else {
        // 忽略弃用警告，因为这是旧系统的回退路径
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
        return AXAPIEnabled();
#pragma clang diagnostic pop
    }
}

// 缓存清理函数
void cleanupWindowCache() {
    for (auto& pair : windowsMap) {
        if (pair.second) {
            CFRelease(pair.second);
        }
    }
    windowsMap.clear();
}

// 清理无效窗口的函数
void cleanupInvalidWindows() {
    std::vector<int> windowsToRemove;

    for (const auto& pair : windowsMap) {
        int handle = pair.first;
        AXUIElementRef window = pair.second;

        if (!window) {
            windowsToRemove.push_back(handle);
            continue;
        }

        CFTypeRef role = NULL;
        AXError error = AXUIElementCopyAttributeValue(window, kAXRoleAttribute, &role);

        if (error != kAXErrorSuccess || !role) {
            windowsToRemove.push_back(handle);
        }

        if (role) CFRelease(role);
    }

    for (int handle : windowsToRemove) {
        if (windowsMap.count(handle)) {
            if (windowsMap[handle]) {
                CFRelease(windowsMap[handle]);
            }
            windowsMap.erase(handle);
        }
    }
}

// 错误处理辅助函数
bool HandleAXError(Napi::Env env, AXError error, const char* operation) {
    if (error == kAXErrorSuccess) {
        return false; // 没有错误
    }

    std::string message = std::string(operation) + " failed. AXError code: " + std::to_string(error) + " (";
    switch (error) {
        case kAXErrorFailure: message += "AX Failure)"; break;
        case kAXErrorIllegalArgument: message += "Illegal Argument)"; break;
        case kAXErrorInvalidUIElement: message += "Invalid UI Element)"; break;
        case kAXErrorCannotComplete: message += "Cannot Complete)"; break;
        case kAXErrorAttributeUnsupported: message += "Attribute Unsupported)"; break;
        case kAXErrorActionUnsupported: message += "Action Unsupported)"; break;
        default: message += "Unknown Error)"; break;
    }

    Napi::Error::New(env, message).ThrowAsJavaScriptException();
    return true; // 有错误
}

// 替代私有 API _AXUIElementGetWindow 的公共 API 实现
AXError GetWindowIDFromAXElement(AXUIElementRef element, CGWindowID* outWindowID) {
    if (!element || !outWindowID) return kAXErrorInvalidUIElement;

    *outWindowID = 0;
    CFTypeRef gwtValue = NULL;
    CFArrayRef windowList = NULL;
    CFTypeRef positionValue = NULL;
    CFTypeRef sizeValue = NULL;

    // 初始化变量
    pid_t elementPid = 0;
    CGPoint windowPos = CGPointZero;
    CGSize windowSize = CGSizeZero;

    // 1. 先获取进程ID
    AXError error = AXUIElementGetPid(element, &elementPid);
    if (error != kAXErrorSuccess) {
        return error; // 直接返回错误，不执行后续代码
    }

    // 2. 尝试 GWTIdentifier
    error = AXUIElementCopyAttributeValue(element, CFSTR("GWTIdentifier"), &gwtValue);
    if (error == kAXErrorSuccess && gwtValue) {
        if (CFGetTypeID(gwtValue) == CFNumberGetTypeID()) {
            SInt64 tempID = 0;
            if (CFNumberGetValue((CFNumberRef)gwtValue, kCFNumberSInt64Type, &tempID)) {
                *outWindowID = (CGWindowID)tempID;
            }
        }
        CFRelease(gwtValue);
        if (*outWindowID != 0) return kAXErrorSuccess;
    }

    // 3. 几何匹配
    windowList = CGWindowListCopyWindowInfo(kCGWindowListOptionOnScreenOnly | kCGWindowListExcludeDesktopElements, kCGNullWindowID);
    if (!windowList) return kAXErrorCannotComplete;

    error = AXUIElementCopyAttributeValue(element, kAXPositionAttribute, &positionValue);
    if (error != kAXErrorSuccess) goto cleanup;

    error = AXUIElementCopyAttributeValue(element, kAXSizeAttribute, &sizeValue);
    if (error != kAXErrorSuccess) goto cleanup;

    if (AXValueGetValue((AXValueRef)positionValue, (AXValueType)kAXValueCGPointType, &windowPos) &&
        AXValueGetValue((AXValueRef)sizeValue, (AXValueType)kAXValueCGSizeType, &windowSize)) {

        CGRect windowRect = CGRectMake(windowPos.x, windowPos.y, windowSize.width, windowSize.height);

        for (NSDictionary *windowInfo in (NSArray *)windowList) {
            NSNumber *ownerPid = windowInfo[(id)kCGWindowOwnerPID];
            if ([ownerPid intValue] != elementPid) continue;

            CGRect bounds;
            if (CGRectMakeWithDictionaryRepresentation((CFDictionaryRef)windowInfo[(id)kCGWindowBounds], &bounds)) {
                if (fabs(bounds.origin.x - windowRect.origin.x) < 1 &&
                    fabs(bounds.origin.y - windowRect.origin.y) < 1 &&
                    fabs(bounds.size.width - windowRect.size.width) < 1 &&
                    fabs(bounds.size.height - windowRect.size.height) < 1) {

                    NSNumber *windowNumber = windowInfo[(id)kCGWindowNumber];
                    *outWindowID = [windowNumber unsignedIntValue];
                    break;
                }
            }
        }
    }

cleanup:
    if (gwtValue) CFRelease(gwtValue);
    if (positionValue) CFRelease(positionValue);
    if (sizeValue) CFRelease(sizeValue);
    if (windowList) CFRelease(windowList);

    return (*outWindowID != 0) ? kAXErrorSuccess : kAXErrorAttributeUnsupported;
}

// --- 核心窗口查找和缓存 ---

NSDictionary* getWindowInfo(int handle) {
    CGWindowListOption listOptions = kCGWindowListOptionOnScreenOnly | kCGWindowListExcludeDesktopElements;
    CFArrayRef windowList = CGWindowListCopyWindowInfo(listOptions, kCGNullWindowID);

    for (NSDictionary *info in (NSArray *)windowList) {
        NSNumber *windowNumber = info[(id)kCGWindowNumber];

        if ([windowNumber intValue] == handle) {
            CFRetain((CFPropertyListRef)info);
            CFRelease(windowList);
            return info;
        }
    }

    if (windowList) {
        CFRelease(windowList);
    }
    return NULL;
}

// 修复后的 getAXWindow 函数
AXUIElementRef getAXWindow(int pid, int handle) {
    AXUIElementRef app = AXUIElementCreateApplication(pid);
    if (!app) return NULL;

    CFArrayRef windows = NULL;
    AXError error = AXUIElementCopyAttributeValues(app, kAXWindowsAttribute, 0, 100, &windows);

    AXUIElementRef foundWindow = NULL;

    if (error == kAXErrorSuccess && windows && CFArrayGetCount(windows) > 0) {
        CFIndex count = CFArrayGetCount(windows);
        for (CFIndex i = 0; i < count; i++) {
            AXUIElementRef window = (AXUIElementRef)CFArrayGetValueAtIndex(windows, i);
            if (!window) continue;

            CGWindowID windowId = 0;
            if (GetWindowIDFromAXElement(window, &windowId) == kAXErrorSuccess &&
                windowId == static_cast<unsigned int>(handle)) {
                CFRetain(window);
                foundWindow = window;
                break;
            }
        }
        CFRelease(windows);
    }

    CFRelease(app);
    return foundWindow;
}

void cacheWindow(int handle, int pid) {
    if (_requestAccessibility(false)) {
        if (windowsMap.find(handle) == windowsMap.end()) {
            windowsMap[handle] = getAXWindow(pid, handle);
        }
    }
}

void cacheWindowByInfo(NSDictionary* info) {
    if (info) {
        NSNumber *ownerPid = info[(id)kCGWindowOwnerPID];
        NSNumber *windowNumber = info[(id)kCGWindowNumber];

        cacheWindow([windowNumber intValue], [ownerPid intValue]);
        CFRelease((CFPropertyListRef)info);
    }
}

void findAndCacheWindow(int handle) {
    cacheWindowByInfo(getWindowInfo(handle));
}

AXUIElementRef getAXWindowById(int handle) {
    auto win = windowsMap[handle];

    if (!win) {
        findAndCacheWindow(handle);
        win = windowsMap[handle];
    }

    return win;
}

// --- NAPI 导出函数 ---

Napi::Boolean requestAccessibility(const Napi::CallbackInfo &info) {
    Napi::Env env{info.Env()};
    return Napi::Boolean::New(env, _requestAccessibility(true));
}

Napi::Array getWindows(const Napi::CallbackInfo &info) {
    Napi::Env env{info.Env()};

    CGWindowListOption listOptions = kCGWindowListOptionOnScreenOnly | kCGWindowListExcludeDesktopElements;
    CFArrayRef windowList = CGWindowListCopyWindowInfo(listOptions, kCGNullWindowID);

    std::vector<Napi::Number> vec;

    for (NSDictionary *infoDict in (NSArray *)windowList) {
        NSNumber *ownerPid = infoDict[(id)kCGWindowOwnerPID];
        NSNumber *windowNumber = infoDict[(id)kCGWindowNumber];

        @autoreleasepool {
            auto app = [NSRunningApplication runningApplicationWithProcessIdentifier: [ownerPid intValue]];
            auto path = (app && app.bundleURL && app.bundleURL.path) ? [app.bundleURL.path UTF8String] : "";

            if (app && strcmp(path, "") != 0)  {
                vec.push_back(Napi::Number::New(env, [windowNumber intValue]));
            }
        }
    }

    auto arr = Napi::Array::New(env, vec.size());

    for (size_t i = 0; i < vec.size(); i++) {
        arr[i] = vec[i];
    }

    if (windowList) {
        CFRelease(windowList);
    }

    return arr;
}

Napi::Number getActiveWindow(const Napi::CallbackInfo &info) {
    Napi::Env env{info.Env()};

    CGWindowListOption listOptions = kCGWindowListOptionOnScreenOnly | kCGWindowListExcludeDesktopElements;
    CFArrayRef windowList = CGWindowListCopyWindowInfo(listOptions, kCGNullWindowID);

    for (NSDictionary *infoDict in (NSArray *)windowList) {
        NSNumber *ownerPid = infoDict[(id)kCGWindowOwnerPID];
        NSNumber *windowNumber = infoDict[(id)kCGWindowNumber];

        @autoreleasepool {
            auto app = [NSRunningApplication runningApplicationWithProcessIdentifier: [ownerPid intValue]];

            if (app) {
                if ([app isActive]) {
                    CFRelease(windowList);
                    return Napi::Number::New(env, [windowNumber intValue]);
                }
            }
        }
    }

    if (windowList) {
        CFRelease(windowList);
    }
    return Napi::Number::New(env, 0);
}

Napi::Object initWindow(const Napi::CallbackInfo &info) {
    Napi::Env env{info.Env()};
    int handle = info[0].As<Napi::Number>().Int32Value();
    auto wInfo = getWindowInfo(handle);

    if (wInfo) {
        // 先获取需要的数据
        NSNumber *ownerPid = wInfo[(id)kCGWindowOwnerPID];
        int pidValue = [ownerPid intValue]; // 拷贝 int 值，这是安全的

        @autoreleasepool {
            NSRunningApplication *app = [NSRunningApplication runningApplicationWithProcessIdentifier: pidValue];

            if (!app || !app.bundleURL || !app.bundleURL.path) {
                CFRelease((CFPropertyListRef)wInfo);
                return Napi::Object::New(env);
            }

            auto obj = Napi::Object::New(env);
            obj.Set("processId", pidValue);
            obj.Set("path", [app.bundleURL.path UTF8String]);

            // 使用 cacheWindowByInfo 来处理缓存和 wInfo 的释放
            // cacheWindowByInfo 内部会调用 CFRelease(info)，所以这里不需要手动释放
            cacheWindowByInfo(wInfo);

            return obj;
        }
    }

    return Napi::Object::New(env);
}

Napi::String getWindowTitle(const Napi::CallbackInfo &info) {
    Napi::Env env{info.Env()};
    int handle = info[0].As<Napi::Number>().Int32Value();
    auto wInfo = getWindowInfo(handle);

    if (wInfo) {
        @autoreleasepool {
            // 1. 获取 NSString
            NSString *title = wInfo[(id)kCGWindowOwnerName];
            Napi::String result;

            // 2. 在释放字典前，将 NSString 转换为 Napi::String
            if (title) {
                result = Napi::String::New(env, [title UTF8String]);
            } else {
                result = Napi::String::New(env, "");
            }

            // 3. 安全释放字典
            CFRelease((CFPropertyListRef)wInfo);

            return result;
        }
    }
    return Napi::String::New(env, "");
}

Napi::String getWindowName(const Napi::CallbackInfo &info) {
    Napi::Env env{info.Env()};
    int handle = info[0].As<Napi::Number>().Int32Value();
    auto wInfo = getWindowInfo(handle);

    if (wInfo) {
        @autoreleasepool {
            // 1. 获取 NSString
            NSString *name = wInfo[(id)kCGWindowName];
            Napi::String result;

            // 2. 在释放字典前转换
            if (name) {
                result = Napi::String::New(env, [name UTF8String]);
            } else {
                result = Napi::String::New(env, "");
            }

            // 3. 安全释放字典
            CFRelease((CFPropertyListRef)wInfo);

            return result;
        }
    }
    return Napi::String::New(env, "");
}

Napi::Object getWindowBounds(const Napi::CallbackInfo &info) {
    Napi::Env env{info.Env()};
    int handle = info[0].As<Napi::Number>().Int32Value();
    auto wInfo = getWindowInfo(handle);

    if (wInfo) {
        CGRect bounds;
        CGRectMakeWithDictionaryRepresentation((CFDictionaryRef)wInfo[(id)kCGWindowBounds], &bounds);
        CFRelease((CFPropertyListRef)wInfo);

        auto obj = Napi::Object::New(env);
        obj.Set("x", bounds.origin.x);
        obj.Set("y", bounds.origin.y);
        obj.Set("width", bounds.size.width);
        obj.Set("height", bounds.size.height);

        return obj;
    }
    return Napi::Object::New(env);
}

Napi::Boolean setWindowBounds(const Napi::CallbackInfo &info) {
    Napi::Env env{info.Env()};
    if (!IsAtLeastMacOSVersion(10, 9)) return Napi::Boolean::New(env, false);

    auto handle = info[0].As<Napi::Number>().Int32Value();
    auto bounds = info[1].As<Napi::Object>();

    auto x = bounds.Get("x").As<Napi::Number>().DoubleValue();
    auto y = bounds.Get("y").As<Napi::Number>().DoubleValue();
    auto width = bounds.Get("width").As<Napi::Number>().DoubleValue();
    auto height = bounds.Get("height").As<Napi::Number>().DoubleValue();

    auto win = getAXWindowById(handle);
    if (!win) {
        return Napi::Boolean::New(env, false);
    }

    NSPoint point = NSMakePoint((CGFloat)x, (CGFloat)y);
    NSSize size = NSMakeSize((CGFloat)width, (CGFloat)height);

    // [修复] 添加 (AXValueType) 显式强制转换
    CFTypeRef positionStorage = AXValueCreate((AXValueType)kAXValueCGPointType, &point);
    CFTypeRef sizeStorage = AXValueCreate((AXValueType)kAXValueCGSizeType, &size);

    bool success = true;

    if (positionStorage) {
        AXError err = AXUIElementSetAttributeValue(win, kAXPositionAttribute, positionStorage);
        CFRelease(positionStorage);
        if (HandleAXError(env, err, "setWindowBounds: kAXPositionAttribute")) {
            success = false;
        }
    }

    if (sizeStorage && success) {
        AXError err = AXUIElementSetAttributeValue(win, kAXSizeAttribute, sizeStorage);
        CFRelease(sizeStorage);
        if (HandleAXError(env, err, "setWindowBounds: kAXSizeAttribute")) {
            success = false;
        }
    }

    return Napi::Boolean::New(env, success);
}

Napi::Boolean bringWindowToTop(const Napi::CallbackInfo &info) {
    Napi::Env env{info.Env()};
    if (!IsAtLeastMacOSVersion(10, 9)) return Napi::Boolean::New(env, false);

    auto handle = info[0].As<Napi::Number>().Int32Value();
    auto pid = info[1].As<Napi::Number>().Int32Value();

    AXUIElementRef app = AXUIElementCreateApplication(pid);
    AXUIElementRef win = getAXWindowById(handle);

    bool success = true;

    if (app) {
        AXError err = AXUIElementSetAttributeValue(app, kAXFrontmostAttribute, kCFBooleanTrue);
        CFRelease(app);
        if (HandleAXError(env, err, "bringWindowToTop: kAXFrontmostAttribute")) {
            success = false;
        }
    }

    if (win && success) {
        AXError err = AXUIElementSetAttributeValue(win, kAXMainAttribute, kCFBooleanTrue);
        if (HandleAXError(env, err, "bringWindowToTop: kAXMainAttribute")) {
            success = false;
        }
    }

    return Napi::Boolean::New(env, success);
}

Napi::Boolean setWindowMinimized(const Napi::CallbackInfo &info) {
    Napi::Env env{info.Env()};
    if (!IsAtLeastMacOSVersion(10, 9)) return Napi::Boolean::New(env, false);

    auto handle = info[0].As<Napi::Number>().Int32Value();
    auto toggle = info[1].As<Napi::Boolean>();

    auto win = getAXWindowById(handle);
    if (!win) {
        return Napi::Boolean::New(env, false);
    }

    AXError err = AXUIElementSetAttributeValue(win, kAXMinimizedAttribute, toggle ? kCFBooleanTrue : kCFBooleanFalse);
    if (HandleAXError(env, err, "setWindowMinimized: kAXMinimizedAttribute")) {
        return Napi::Boolean::New(env, false);
    }

    return Napi::Boolean::New(env, true);
}

Napi::Boolean setWindowMaximized(const Napi::CallbackInfo &info) {
    Napi::Env env{info.Env()};
    if (!IsAtLeastMacOSVersion(10, 9)) return Napi::Boolean::New(env, false);

    auto handle = info[0].As<Napi::Number>().Int32Value();
    auto win = getAXWindowById(handle);

    if (!win) {
        return Napi::Boolean::New(env, false);
    }

    @autoreleasepool {
        NSScreen *mainScreen = [NSScreen mainScreen];
        if (!mainScreen) return Napi::Boolean::New(env, false);

        NSRect screenFrame = [mainScreen frame];
        NSRect visibleFrame = [mainScreen visibleFrame];

        CGFloat ax_x = visibleFrame.origin.x;
        CGFloat ax_y = screenFrame.size.height - visibleFrame.origin.y - visibleFrame.size.height;
        CGFloat ax_width = visibleFrame.size.width;
        CGFloat ax_height = visibleFrame.size.height;

        NSPoint point = NSMakePoint(ax_x, ax_y);
        NSSize size = NSMakeSize(ax_width, ax_height);

        // [修复] 添加 (AXValueType) 显式强制转换
        CFTypeRef positionStorage = AXValueCreate((AXValueType)kAXValueCGPointType, &point);
        CFTypeRef sizeStorage = AXValueCreate((AXValueType)kAXValueCGSizeType, &size);

        bool success = true;

        if (positionStorage) {
            AXError err = AXUIElementSetAttributeValue(win, kAXPositionAttribute, positionStorage);
            CFRelease(positionStorage);
            if (HandleAXError(env, err, "setWindowMaximized: kAXPositionAttribute")) {
                success = false;
            }
        }

        if (sizeStorage && success) {
            AXError err = AXUIElementSetAttributeValue(win, kAXSizeAttribute, sizeStorage);
            CFRelease(sizeStorage);
            if (HandleAXError(env, err, "setWindowMaximized: kAXSizeAttribute")) {
                success = false;
            }
        }

        return Napi::Boolean::New(env, success);
    }
}

Napi::Number getWindowAtPoint(const Napi::CallbackInfo& info) {
    Napi::Env env{info.Env()};

    if (info.Length() < 2 || !info[0].IsNumber() || !info[1].IsNumber()) {
        Napi::TypeError::New(env, "Expected x and y coordinates (Number)").ThrowAsJavaScriptException();
        return Napi::Number::New(env, 0);
    }

    double x = info[0].As<Napi::Number>().DoubleValue();
    double y = info[1].As<Napi::Number>().DoubleValue();
    CGPoint point = CGPointMake((CGFloat)x, (CGFloat)y);

    int excludedId = 0;
    if (info.Length() > 2 && info[2].IsNumber()) {
        excludedId = info[2].As<Napi::Number>().Int32Value();
    }

    CGWindowListOption listOptions = kCGWindowListOptionOnScreenOnly | kCGWindowListExcludeDesktopElements;
    CFArrayRef windowList = CGWindowListCopyWindowInfo(listOptions, kCGNullWindowID);

    if (!windowList) return Napi::Number::New(env, 0);

    int foundHandle = 0;

    for (NSDictionary *infoDict in (NSArray *)windowList) {
        CGRect bounds;
        if (!CGRectMakeWithDictionaryRepresentation((CFDictionaryRef)infoDict[(id)kCGWindowBounds], &bounds)) {
            continue;
        }

        if (CGRectContainsPoint(bounds, point)) {
            NSNumber *windowNumber = infoDict[(id)kCGWindowNumber];
            if (excludedId != 0 && [windowNumber intValue] == excludedId) {
                continue;
            }

            NSNumber *alpha = infoDict[(id)kCGWindowAlpha];
            if (alpha && [alpha floatValue] <= 0.01) continue;

            NSNumber *layer = infoDict[(id)kCGWindowLayer];
            if (layer && [layer intValue] < 0) continue;

            NSNumber *ownerPid = infoDict[(id)kCGWindowOwnerPID];

            @autoreleasepool {
                NSRunningApplication *app = [NSRunningApplication runningApplicationWithProcessIdentifier: [ownerPid intValue]];

                if (app && app.activationPolicy == NSApplicationActivationPolicyRegular) {
                    foundHandle = [windowNumber intValue];
                    break;
                }
            }
        }
    }

    CFRelease(windowList);
    return Napi::Number::New(env, foundHandle);
}

Napi::Value captureWindow(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    if (info.Length() < 1 || !info[0].IsNumber()) {
        Napi::TypeError::New(env, "Expected window handle ID (Number)").ThrowAsJavaScriptException();
        return env.Null();
    }

    CGWindowID windowID = (CGWindowID)info[0].As<Napi::Number>().Int32Value();

    if (windowID == 0 || windowID == kCGNullWindowID) {
        return Napi::String::New(env, "");
    }

    @autoreleasepool {
        CGImageRef windowImage = CGWindowListCreateImage(
            CGRectNull,
            kCGWindowListOptionIncludingWindow,
            windowID,
            kCGWindowImageBoundsIgnoreFraming
        );

        if (!windowImage) {
            return Napi::String::New(env, "");
        }

        CFMutableDataRef pngData = CFDataCreateMutable(kCFAllocatorDefault, 0);
        if (!pngData) {
            CFRelease(windowImage);
            return Napi::String::New(env, "");
        }

        CFStringRef typeIdentifier = CFSTR("public.png");
        CGImageDestinationRef destination = CGImageDestinationCreateWithData(pngData, typeIdentifier, 1, NULL);

        if (!destination) {
            CFRelease(windowImage);
            CFRelease(pngData);
            return Napi::String::New(env, "");
        }

        CGImageDestinationAddImage(destination, windowImage, NULL);
        bool success = CGImageDestinationFinalize(destination);

        CFRelease(destination);
        CFRelease(windowImage);

        if (!success) {
            CFRelease(pngData);
            return Napi::String::New(env, "");
        }

        NSData *imageData = (NSData *)CFBridgingRelease(pngData);
        if (!imageData || imageData.length == 0) {
            return Napi::String::New(env, "");
        }

        NSString *base64String = [imageData base64EncodedStringWithOptions:0];
        return Napi::String::New(env, [base64String UTF8String]);
    }
}

// 导出的清理函数
Napi::Value CleanupInvalidWindowsExport(const Napi::CallbackInfo& info) {
    cleanupInvalidWindows();
    return info.Env().Undefined();
}

// 模块卸载时的清理函数
void CleanupOnModuleUnload(void*) {
    cleanupWindowCache();
}

Napi::Object Init(Napi::Env env, Napi::Object exports) {
    // 注册模块卸载时的清理函数
    napi_add_env_cleanup_hook(env, CleanupOnModuleUnload, nullptr);

    exports.Set(Napi::String::New(env, "getWindows"),
                Napi::Function::New(env, getWindows));
    exports.Set(Napi::String::New(env, "getActiveWindow"),
                Napi::Function::New(env, getActiveWindow));
    exports.Set(Napi::String::New(env, "setWindowBounds"),
                Napi::Function::New(env, setWindowBounds));
    exports.Set(Napi::String::New(env, "getWindowBounds"),
                Napi::Function::New(env, getWindowBounds));
    exports.Set(Napi::String::New(env, "getWindowTitle"),
                Napi::Function::New(env, getWindowTitle));
    exports.Set(Napi::String::New(env, "getWindowName"),
                Napi::Function::New(env, getWindowName));
    exports.Set(Napi::String::New(env, "initWindow"),
                Napi::Function::New(env, initWindow));
    exports.Set(Napi::String::New(env, "bringWindowToTop"),
                Napi::Function::New(env, bringWindowToTop));
    exports.Set(Napi::String::New(env, "setWindowMinimized"),
                Napi::Function::New(env, setWindowMinimized));
    exports.Set(Napi::String::New(env, "setWindowMaximized"),
                Napi::Function::New(env, setWindowMaximized));
    exports.Set(Napi::String::New(env, "requestAccessibility"),
                Napi::Function::New(env, requestAccessibility));
    exports.Set(Napi::String::New(env, "getWindowAtPoint"),
                Napi::Function::New(env, getWindowAtPoint));
    exports.Set(Napi::String::New(env, "captureWindow"),
                Napi::Function::New(env, captureWindow));
    exports.Set(Napi::String::New(env, "cleanup"),
                Napi::Function::New(env, CleanupInvalidWindowsExport));

    return exports;
}

NODE_API_MODULE(addon, Init)