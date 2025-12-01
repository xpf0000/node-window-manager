#pragma once
#include <cmath>
#include <cstdint>
#include <iostream>
#include <napi.h>
#include <shtypes.h>
#include <string>
#include <windows.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <wincodec.h>
#include <atomic>
#include <mutex>
#include <memory>
#include <vector>
#include <dxgi1_2.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include "win_d3d_helpers.h"
#include "win_capture_interop.h"
#include "win_direct3d11_interop.h"

// base64 编码实现
static const std::string base64_chars =
"ABCDEFGHIJKLMNOPQRSTUVWXYZ"
"abcdefghijklmnopqrstuvwxyz"
"0123456789+/";

std::string base64_encode(unsigned char const* bytes_to_encode, unsigned int in_len);

// WinRT命名空间
namespace winrt {
    using namespace Windows::Graphics::Capture;
    using namespace Windows::Graphics::DirectX;
    using namespace Windows::Graphics::DirectX::Direct3D11;
    using namespace Windows::Foundation;
}

// 全局WinRT初始化标志
extern std::atomic<bool> g_winrtInitialized;

void EnsureWinRTInitialized();

// 截图管理器类
class ScreenCaptureManager {
public:
    static ScreenCaptureManager& GetInstance() {
        static ScreenCaptureManager instance;
        return instance;
    }

    bool CaptureWindow(HWND hwnd, std::vector<uint8_t>& rgbData, int& width, int& height);

private:
    ScreenCaptureManager() = default;

    bool Initialize();
    bool CreateCaptureItem(HWND hwnd);
    winrt::Direct3D11CaptureFrame CaptureSingleFrame();
    std::vector<uint8_t> TextureToRGBData(ID3D11Device* device, ID3D11Texture2D* texture);
    void Cleanup();

    winrt::com_ptr<ID3D11Device> m_d3dDevice{ nullptr };
    winrt::com_ptr<ID3D11DeviceContext> m_d3dContext{ nullptr };
    winrt::IDirect3DDevice m_device{ nullptr };
    winrt::GraphicsCaptureItem m_captureItem{ nullptr };
    winrt::Direct3D11CaptureFramePool m_framePool{ nullptr };
    winrt::GraphicsCaptureSession m_session{ nullptr };
    std::mutex m_mutex;
};

// PNG转换函数
std::vector<uint8_t> ConvertRgbToPng(const std::vector<uint8_t>& rgbData, int width, int height);

// NAPI截图函数
Napi::Value captureWindow(const Napi::CallbackInfo& info);