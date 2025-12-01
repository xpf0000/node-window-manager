#pragma once
#include <d3d11.h>
#include <dxgi1_2.h> // 必须包含这个文件以支持 IDXGIFactory2 和 IDXGISwapChain1
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

class D3D11Helpers {
public:
    static ComPtr<ID3D11Device> CreateD3D11Device() {
        ComPtr<ID3D11Device> device;
        D3D_FEATURE_LEVEL featureLevels[] = {
            D3D_FEATURE_LEVEL_11_1,
            D3D_FEATURE_LEVEL_11_0,
            D3D_FEATURE_LEVEL_10_1,
            D3D_FEATURE_LEVEL_10_0
        };

        D3D_FEATURE_LEVEL featureLevel;

        // 必须加上 D3D11_CREATE_DEVICE_BGRA_SUPPORT 以支持 Direct2D/WinRT 互操作
        HRESULT hr = D3D11CreateDevice(
            nullptr,
            D3D_DRIVER_TYPE_HARDWARE,
            nullptr,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT,
            featureLevels,
            ARRAYSIZE(featureLevels),
            D3D11_SDK_VERSION,
            &device,
            &featureLevel,
            nullptr
        );

        if (FAILED(hr)) {
            return nullptr;
        }

        return device;
    }
};