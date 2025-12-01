#pragma once
#include <winrt/Windows.Graphics.Capture.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.capture.h>

class CaptureInterop {
public:
    static winrt::Windows::Graphics::Capture::GraphicsCaptureItem CreateCaptureItemForWindow(HWND hwnd) {
        try {
            auto factory = winrt::get_activation_factory<winrt::Windows::Graphics::Capture::GraphicsCaptureItem>();
            auto interop = factory.as<IGraphicsCaptureItemInterop>();

            winrt::Windows::Graphics::Capture::GraphicsCaptureItem item = nullptr;
            interop->CreateForWindow(hwnd, winrt::guid_of<winrt::Windows::Graphics::Capture::GraphicsCaptureItem>(), winrt::put_abi(item));

            return item;
        } catch (...) {
            return nullptr;
        }
    }
};