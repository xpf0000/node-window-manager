// Direct3D 11 基础定义
#include <d3d11.h>

// Windows.Graphics.Capture 和 interop API
#include <windows.graphics.capture.h>
#include <windows.graphics.capture.interop.h>

// 包含 IDirect3DDxgiInterfaceAccess 的原始 COM 定义
#include <windows.graphics.directx.direct3d11.interop.h>
#include <winrt/Windows.Foundation.h> // 用于 winrt::com_ptr
#include <winrt/base.h> // 确保 com_ptr 及其操作符可见
#include <winrt/Windows.Foundation.Metadata.h>

// ... 其他头文件，如 iostream, win_capture_manager.h 等
#include "win_capture_manager.h"
#include <iostream>
#include <iomanip>
#include <wingdi.h>

#include <stdexcept>
#include <wincodec.h>
#include <objbase.h> // For CoUninitialize
#include <Shlwapi.h> // For IStream utility (not strictly needed but good practice)

using namespace winrt::Windows::Foundation::Metadata;

// 全局WinRT初始化标志
std::atomic<bool> g_winrtInitialized{ false };

void EnsureWinRTInitialized() {
    if (!g_winrtInitialized.exchange(true)) {
        winrt::init_apartment(winrt::apartment_type::single_threaded);
    }
}

std::string base64_encode(unsigned char const* bytes_to_encode, unsigned int in_len) {
    std::string ret;
    int i = 0;
    int j = 0;
    unsigned char char_array_3[3];
    unsigned char char_array_4[4];

    while (in_len--) {
        char_array_3[i++] = *(bytes_to_encode++);
        if (i == 3) {
            char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
            char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
            char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
            char_array_4[3] = char_array_3[2] & 0x3f;

            for (i = 0; (i < 4); i++)
                ret += base64_chars[char_array_4[i]];
            i = 0;
        }
    }

    if (i) {
        for (j = i; j < 3; j++)
            char_array_3[j] = '\0';

        char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
        char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
        char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
        char_array_4[3] = char_array_3[2] & 0x3f;

        for (j = 0; (j < i + 1); j++)
            ret += base64_chars[char_array_4[j]];

        while ((i++ < 3))
            ret += '=';
    }

    return ret;
}

bool ScreenCaptureManager::Initialize() {
    try {
        // 使用本地 D3D11Helpers 创建设备
        auto d3dPtr = D3D11Helpers::CreateD3D11Device();
        if (!d3dPtr) {
            return false;
        }

        // 转换 WRL ComPtr 到 winrt::com_ptr
        m_d3dDevice = nullptr;
        d3dPtr.CopyTo(m_d3dDevice.put());

        if (!m_d3dDevice) {
            return false;
        }

        m_d3dDevice->GetImmediateContext(m_d3dContext.put());

        // 创建 WinRT Direct3D 设备
        auto dxgiDevice = m_d3dDevice.as<IDXGIDevice>();
        if (!dxgiDevice) {
            return false;
        }

        winrt::Windows::Foundation::IInspectable inspectable = nullptr;
        HRESULT hr = CreateDirect3D11DeviceFromDXGIDevice(
            dxgiDevice.get(),
            reinterpret_cast<IInspectable**>(winrt::put_abi(inspectable))
        );

        if (FAILED(hr)) {
            return false;
        }

        m_device = inspectable.as<winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice>();
        if (!m_device) {
            return false;
        }
        return true;
    }
    catch (...) {
        return false;
    }
}

// 实现 GDI 截图：这是获取静态清晰文本的最佳方式
bool ScreenCaptureManager::CaptureDesktopWithGDI(std::vector<uint8_t>& rgbaData, int& width, int& height) {
    // 1. 获取桌面 DC
    HDC hScreenDC = GetDC(NULL);
    if (!hScreenDC) return false;

    // 2. 获取屏幕分辨率 (物理)
    // 注意：在高 DPI 感知模式下，GetSystemMetrics 返回的是物理像素
    int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);

    width = screenWidth;
    height = screenHeight;

    // 3. 创建兼容 DC 和 位图
    HDC hMemoryDC = CreateCompatibleDC(hScreenDC);
    HBITMAP hBitmap = CreateCompatibleBitmap(hScreenDC, width, height);
    HBITMAP hOldBitmap = (HBITMAP)SelectObject(hMemoryDC, hBitmap);

    // 4. 执行 BitBlt (像素拷贝) - 核心步骤
    // SRCCOPY 直接拷贝，不进行任何混合或过滤，保证清晰度
    if (!BitBlt(hMemoryDC, 0, 0, width, height, hScreenDC, 0, 0, SRCCOPY)) {
        SelectObject(hMemoryDC, hOldBitmap);
        DeleteObject(hBitmap);
        DeleteDC(hMemoryDC);
        ReleaseDC(NULL, hScreenDC);
        return false;
    }

    // 5. 准备 BITMAPINFO 以获取像素位
    BITMAPINFO bmi = {0};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height; // 负数表示自顶向下 (Top-down)，否则图片是倒的
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32; // 32位 BGRA
    bmi.bmiHeader.biCompression = BI_RGB;

    // 6. 分配内存并获取数据
    // GDI 默认返回的数据通常是 BGRA 格式，这正好适配我们在 ConvertRgbToPng 里设置的 WICPixelFormat32bppBGRA
    size_t dataSize = width * height * 4;
    rgbaData.resize(dataSize);

    // GetDIBits 将 HBITMAP 数据复制到我们的 vector 中
    if (!GetDIBits(hMemoryDC, hBitmap, 0, height, rgbaData.data(), &bmi, DIB_RGB_COLORS)) {
        rgbaData.clear();
        // 清理资源
        SelectObject(hMemoryDC, hOldBitmap);
        DeleteObject(hBitmap);
        DeleteDC(hMemoryDC);
        ReleaseDC(NULL, hScreenDC);
        return false;
    }

    // 7. 清理资源
    SelectObject(hMemoryDC, hOldBitmap);
    DeleteObject(hBitmap);
    DeleteDC(hMemoryDC);
    ReleaseDC(NULL, hScreenDC);

    return true;
}

// 专门处理桌面捕获
bool ScreenCaptureManager::CaptureDesktop(std::vector<uint8_t>& rgbaData, int& width, int& height) {
    try {
        auto monitor = MonitorFromWindow(GetDesktopWindow(), MONITOR_DEFAULTTOPRIMARY);
        m_captureItem = CaptureInterop::CreateCaptureItemForMonitor(monitor);
        if (!m_captureItem) {
            std::cout << "[ERROR] Failed to create capture item for monitor" << std::endl;
            return false;
        }

        auto frame = CaptureSingleFrame();
        if (!frame) {
            std::cout << "[ERROR] Failed to capture desktop frame" << std::endl;
            return false;
        }
        auto surface = frame.Surface();
        if (!surface) {
            std::cout << "[ERROR] Invalid surface from desktop frame" << std::endl;
            return false;
        }
        winrt::com_ptr<ID3D11Texture2D> texture = GetDXGIInterfaceFromObject<ID3D11Texture2D>(surface);
        if (!texture) {
            std::cout << "[ERROR] Failed to get texture from desktop surface" << std::endl;
            Cleanup();
            return false;
        }
        D3D11_TEXTURE2D_DESC desc;
        texture->GetDesc(&desc);

        rgbaData = TextureToRGBData(m_d3dDevice.get(), texture.get());
        if (rgbaData.empty()) {
            std::cout << "[ERROR] Failed to convert desktop texture to RGB" << std::endl;
            Cleanup();
            return false;
        }
        width = desc.Width;
        height = desc.Height;

        Cleanup();
        return true;
    }
    catch (const std::exception& e) {
        std::cout << "[ERROR] Desktop capture exception: " << e.what() << std::endl;
        Cleanup();
        return false;
    }
    catch (...) {
        std::cout << "[ERROR] Unknown exception in desktop capture" << std::endl;
        Cleanup();
        return false;
    }
}

// 普通窗口捕获（原有逻辑）
bool ScreenCaptureManager::CaptureNormalWindow(HWND hwnd, std::vector<uint8_t>& rgbaData, int& width, int& height) {
   try {
        if (!CreateCaptureItem(hwnd)) {
            std::cout << "[ERROR] Failed to create capture item for window" << std::endl;
            return false;
        }

        auto frame = CaptureSingleFrame();
        if (!frame) {
            std::cout << "[ERROR] Failed to capture window frame" << std::endl;
            return false;
        }

        auto surface = frame.Surface();
        if (!surface) {
            std::cout << "[ERROR] Invalid surface from window frame" << std::endl;
            Cleanup();
            return false;
        }

        winrt::com_ptr<ID3D11Texture2D> texture = GetDXGIInterfaceFromObject<ID3D11Texture2D>(surface);
        if (!texture) {
            Cleanup();
            return false;
        }

        D3D11_TEXTURE2D_DESC desc;
        texture->GetDesc(&desc);

        rgbaData = TextureToRGBData(m_d3dDevice.get(), texture.get());
        if (rgbaData.empty()) {
            Cleanup();
            return false;
        }

        width = desc.Width;
        height = desc.Height;

        Cleanup();
        return true;
    } catch (...) {
        Cleanup();
        return false;
    }
}

bool ScreenCaptureManager::CaptureWindow(HWND hwnd, std::vector<uint8_t>& rgbaData, int& width, int& height) {
    std::lock_guard<std::mutex> lock(m_mutex);
    try {
        if (!Initialize()) {
            return false;
        }
        // 检查是否为桌面窗口
        if (hwnd == GetDesktopWindow()) {
            return CaptureDesktop(rgbaData, width, height);
        } else {
            return CaptureNormalWindow(hwnd, rgbaData, width, height);
        }
    }
    catch (...) {
        Cleanup();
        return false;
    }
}

bool ScreenCaptureManager::CreateCaptureItem(HWND hwnd) {
    // 验证窗口句柄
    if (!IsWindow(hwnd)) {
        return false;
    }

    try {
        m_captureItem = CaptureInterop::CreateCaptureItemForWindow(hwnd);
        if (!m_captureItem) {
            return false;
        }

        auto size = m_captureItem.Size();
        return true;
    }
    catch (...) {
        return false;
    }
}

winrt::Direct3D11CaptureFrame ScreenCaptureManager::CaptureSingleFrame() {
    if (!m_captureItem || !m_device) {
        return { nullptr };
    }

    winrt::Direct3D11CaptureFrame resultFrame{ nullptr };
    winrt::event_token token{};
    HANDLE hEvent = NULL;

    try {
        auto size = m_captureItem.Size();
        // 1. 创建帧池和会话
        m_framePool = winrt::Direct3D11CaptureFramePool::CreateFreeThreaded(
            m_device,
            winrt::DirectXPixelFormat::B8G8R8A8UIntNormalized,
            1,
            size);

        m_session = m_framePool.CreateCaptureSession(m_captureItem);

        // 2. 【核心修改】 设置为 false 以忽略鼠标
        // 注意：这需要 Windows 10 Version 2004 (Build 19041) 或更高版本
        // 检查 GraphicsCaptureSession 类中是否存在 IsCursorCaptureEnabled 属性
        if (ApiInformation::IsPropertyPresent(
                winrt::name_of<winrt::Windows::Graphics::Capture::GraphicsCaptureSession>(),
                L"IsCursorCaptureEnabled"))
        {
            m_session.IsCursorCaptureEnabled(false);
        }
        if (ApiInformation::IsPropertyPresent(
                winrt::name_of<winrt::Windows::Graphics::Capture::GraphicsCaptureSession>(),
                L"IsBorderRequired"))
        {
            m_session.IsBorderRequired(false); // 隐藏黄色边框
        }

        // 2. 使用事件进行同步
        hEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
        if (!hEvent) {
            // 确保在返回前清理 WinRT 对象
            m_session = nullptr;
            m_framePool = nullptr;
            return { nullptr };
        }

        std::atomic<bool> frameReceived{false};

        // 3. 注册 FrameArrived 回调
        token = m_framePool.FrameArrived([&, hEvent](auto& framePool, auto&) {
            if (frameReceived.exchange(true)) {
                return;
            }
            try {
                resultFrame = framePool.TryGetNextFrame();
            } catch (...) {
            }
            SetEvent(hEvent);
        });

        m_session.StartCapture();
        WaitForSingleObject(hEvent, 5000); // 5秒超时
    }
    catch (...) {
        resultFrame = nullptr;
    }

    // 5. 清理代码 (无论成功与否都会执行)

    if (m_framePool && token) {
        // **修正点：先移除事件处理程序**
        m_framePool.FrameArrived(token);
    }

    if (m_session) {
        m_session.Close();
    }

    if (m_framePool) {
        m_framePool.Close();
    }

    if (hEvent) {
        CloseHandle(hEvent);
    }

    m_framePool = nullptr;
    m_session = nullptr;

    return resultFrame;
}

std::vector<uint8_t> ScreenCaptureManager::TextureToRGBData(ID3D11Device* device, ID3D11Texture2D* texture) {

    if (!texture || !device) {
        return {};
    }

    D3D11_TEXTURE2D_DESC desc;
    texture->GetDesc(&desc);

    // 创建暂存纹理
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.MiscFlags = 0;

    winrt::com_ptr<ID3D11Texture2D> stagingTexture;
    HRESULT hr = device->CreateTexture2D(&desc, nullptr, stagingTexture.put());
    if (FAILED(hr)) {
        return {};
    }

    winrt::com_ptr<ID3D11DeviceContext> context;
    device->GetImmediateContext(context.put());
    context->CopyResource(stagingTexture.get(), texture);

    D3D11_MAPPED_SUBRESOURCE mapped;
    hr = context->Map(stagingTexture.get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) {
        return {};
    }

    std::vector<uint8_t> rgbaData;
    try {
        const size_t pixelCount = desc.Width * desc.Height;
        const size_t rgbaDataSize = pixelCount * 4;
        if (rgbaDataSize == 0) {
            context->Unmap(stagingTexture.get(), 0);
            return {};
        }
        rgbaData.resize(rgbaDataSize);
        const uint8_t* sourceData = static_cast<const uint8_t*>(mapped.pData);
        const UINT sourceRowPitch = mapped.RowPitch;

        // 根据纹理格式处理数据
        if (desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM || desc.Format == DXGI_FORMAT_R8G8B8A8_UNORM) {
            for (size_t y = 0; y < desc.Height; ++y) {
                const uint8_t* sourceRow = sourceData + (y * sourceRowPitch);
                uint8_t* destRow = rgbaData.data() + (y * desc.Width * 4);
                // 直接内存拷贝
                memcpy(destRow, sourceRow, desc.Width * 4);
            }
        } else {
            // 假设是BGRA格式进行转换
            for (size_t y = 0; y < desc.Height; ++y) {
                const uint8_t* sourceRow = sourceData + (y * sourceRowPitch);
                uint8_t* destRow = rgbaData.data() + (y * desc.Width * 4);
                memcpy(destRow, sourceRow, desc.Width * 4);
            }
        }
    }
    catch (...) {
        context->Unmap(stagingTexture.get(), 0);
        return {};
    }

    context->Unmap(stagingTexture.get(), 0);
    return rgbaData;
}

void ScreenCaptureManager::Cleanup() {
    if (m_session) {
        m_session.Close();
        m_session = nullptr;
    }

    if (m_framePool) {
        m_framePool.Close();
        m_framePool = nullptr;
    }

    m_captureItem = nullptr;
}

std::vector<uint8_t> ConvertRgbToPng(const std::vector<uint8_t>& rgbaData, int width, int height) {
    std::vector<uint8_t> pngData;

    // 检查输入数据是否有效
    if (rgbaData.empty() || width <= 0 || height <= 0) {
        std::cout << "[DEBUG] ConvertRgbToPng: Invalid input size." << std::endl;
        return pngData;
    }

    // 计算预期的RGB数据大小
    size_t expectedSize = width * height * 4;
    if (rgbaData.size() != expectedSize) {
        std::cout << "[DEBUG] ConvertRgbToPng: Data size mismatch. Expected: " << expectedSize << ", Got: " << rgbaData.size() << std::endl;
        return pngData;
    }

    // 资源指针，使用 NULL 初始化
    IWICImagingFactory* pFactory = NULL;
    IStream* pMemoryStream = NULL; // 内存流
    IWICBitmapEncoder* pEncoder = NULL;
    IWICBitmapFrameEncode* pFrame = NULL;

    HRESULT hr = S_OK;

    try {
        // 创建WIC工厂
        hr = CoCreateInstance(
            CLSID_WICImagingFactory,
            NULL,
            CLSCTX_INPROC_SERVER,
            IID_IWICImagingFactory,
            (LPVOID*)&pFactory
        );
        if (FAILED(hr)) {
            std::cout << "[ERROR] ConvertRgbToPng: CoCreateInstance failed. HRESULT: 0x" << std::hex << hr << std::dec << std::endl;
            throw std::runtime_error("CoCreateInstance failed");
        }

        // 创建内存流
        hr = CreateStreamOnHGlobal(NULL, TRUE, &pMemoryStream);
        if (FAILED(hr)) {
            std::cout << "[ERROR] ConvertRgbToPng: CreateStreamOnHGlobal failed. HRESULT: 0x" << std::hex << hr << std::dec << std::endl;
            throw std::runtime_error("CreateStreamOnHGlobal failed");
        }

        // 创建PNG编码器
        hr = pFactory->CreateEncoder(GUID_ContainerFormatPng, NULL, &pEncoder);
        if (FAILED(hr)) {
            std::cout << "[ERROR] ConvertRgbToPng: CreateEncoder failed. HRESULT: 0x" << std::hex << hr << std::dec << std::endl;
            throw std::runtime_error("CreateEncoder failed");
        }

        // 初始化编码器到内存流
        hr = pEncoder->Initialize(pMemoryStream, WICBitmapEncoderNoCache);
        if (FAILED(hr)) {
            std::cout << "[ERROR] ConvertRgbToPng: Encoder Initialize failed. HRESULT: 0x" << std::hex << hr << std::dec << std::endl;
            throw std::runtime_error("Encoder Initialize failed");
        }

        // 创建帧
        hr = pEncoder->CreateNewFrame(&pFrame, NULL);
        if (FAILED(hr)) {
            std::cout << "[ERROR] ConvertRgbToPng: CreateNewFrame failed. HRESULT: 0x" << std::hex << hr << std::dec << std::endl;
            throw std::runtime_error("CreateNewFrame failed");
        }

        // 初始化帧
        hr = pFrame->Initialize(NULL);
        if (FAILED(hr)) {
            std::cout << "[ERROR] ConvertRgbToPng: Frame Initialize failed. HRESULT: 0x" << std::hex << hr << std::dec << std::endl;
            throw std::runtime_error("Frame Initialize failed");
        }

        // 设置帧尺寸
        hr = pFrame->SetSize(width, height);
        if (FAILED(hr)) {
            std::cout << "[ERROR] ConvertRgbToPng: SetSize failed. HRESULT: 0x" << std::hex << hr << std::dec << std::endl;
            throw std::runtime_error("SetSize failed");
        }

        // *** 核心修复 ***
        // GDI GetDIBits 返回的是 32bpp BGRA 格式，必须告知 WIC
        WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;

        hr = pFrame->SetPixelFormat(&format);
        if (FAILED(hr)) {
            std::cout << "[ERROR] ConvertRgbToPng: SetPixelFormat failed. HRESULT: 0x" << std::hex << hr << std::dec << std::endl;
            throw std::runtime_error("SetPixelFormat failed");
        }

        // 写入像素数据
        UINT stride = width * 4; // 4字节每像素 (BGRA)
        UINT bufferSize = stride * height;

        hr = pFrame->WritePixels(height, stride, bufferSize, const_cast<BYTE*>(rgbaData.data()));
        if (FAILED(hr)) {
            std::cout << "[ERROR] ConvertRgbToPng: WritePixels failed. HRESULT: 0x" << std::hex << hr << std::dec << std::endl;
            throw std::runtime_error("WritePixels failed");
        }

        // 提交帧
        hr = pFrame->Commit();
        if (FAILED(hr)) {
            std::cout << "[ERROR] ConvertRgbToPng: Frame Commit failed. HRESULT: 0x" << std::hex << hr << std::dec << std::endl;
            throw std::runtime_error("Frame Commit failed");
        }

        // 提交编码器 (将数据写入内存流)
        hr = pEncoder->Commit();
        if (FAILED(hr)) {
            std::cout << "[ERROR] ConvertRgbToPng: Encoder Commit failed. HRESULT: 0x" << std::hex << hr << std::dec << std::endl;
            throw std::runtime_error("Encoder Commit failed");
        }

        // 获取流统计信息
        STATSTG stats;
        hr = pMemoryStream->Stat(&stats, STATFLAG_NONAME);
        if (FAILED(hr)) {
            std::cout << "[ERROR] ConvertRgbToPng: Stream Stat failed. HRESULT: 0x" << std::hex << hr << std::dec << std::endl;
            throw std::runtime_error("Stream Stat failed");
        }

        // 重置流位置
        LARGE_INTEGER liZero = {0};
        hr = pMemoryStream->Seek(liZero, STREAM_SEEK_SET, NULL);
        if (FAILED(hr)) {
            std::cout << "[ERROR] ConvertRgbToPng: Stream Seek failed. HRESULT: 0x" << std::hex << hr << std::dec << std::endl;
            throw std::runtime_error("Stream Seek failed");
        }

        // 读取PNG数据
        if (stats.cbSize.QuadPart > 0) {
            pngData.resize(stats.cbSize.QuadPart);
            ULONG bytesRead = 0;

            hr = pMemoryStream->Read(pngData.data(), stats.cbSize.QuadPart, &bytesRead);
            if (FAILED(hr)) {
                std::cout << "[ERROR] ConvertRgbToPng: Stream Read failed. HRESULT: 0x" << std::hex << hr << std::dec << std::endl;
                throw std::runtime_error("Stream Read failed");
            }
        } else {
            std::cout << "[ERROR] ConvertRgbToPng: Stream size is 0 after encoding." << std::endl;
            throw std::runtime_error("Stream size is 0");
        }
    }
    catch (const std::exception& e) {
        std::cout << "[ERROR] ConvertRgbToPng: Exception caught: " << e.what() << std::endl;
        pngData.clear();
    }
    catch (...) {
        std::cout << "[ERROR] ConvertRgbToPng: Unknown exception caught." << std::endl;
        pngData.clear();
    }

    // 清理资源
    if (pFrame) pFrame->Release();
    if (pEncoder) pEncoder->Release();
    if (pMemoryStream) pMemoryStream->Release(); // 清理 IStream 对象
    if (pFactory) pFactory->Release();

    // 如果 CoCreateInstance 成功，通常不需要 CoUninitialize，
    // 因为 Electron/Node Addons 的 COM 初始化通常由外部环境管理。
    // 如果您确定需要调用，请在成功调用 CoCreateInstance 之后再调用，
    // 但为安全起见，通常不在一个工具函数内做全局的 CoUninitialize。

    return pngData;
}

Napi::Value captureWindow(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    // 参数验证
    if (info.Length() < 1 || !info[0].IsNumber()) {
        Napi::TypeError::New(env, "Window handle (number) expected").ThrowAsJavaScriptException();
        return env.Null();
    }

    // 获取窗口句柄
    int64_t handleValue = info[0].As<Napi::Number>().Int64Value();
    HWND hwnd = reinterpret_cast<HWND>(handleValue);

    // 验证窗口句柄有效性
    if (!IsWindow(hwnd)) {
        Napi::Error::New(env, "Invalid window handle").ThrowAsJavaScriptException();
        return env.Null();
    }

    try {
        // 确保WinRT已初始化
        EnsureWinRTInitialized();

        // 使用截图管理器进行截图
        std::vector<uint8_t> rgbaData;
        int width = 0;
        int height = 0;
        bool success = ScreenCaptureManager::GetInstance().CaptureWindow(hwnd, rgbaData, width, height);

        if (!success) {
            std::cout << "[ERROR] CaptureWindow not success" << std::endl;
            return env.Null();
        }

        if (rgbaData.empty()) {
            return env.Null();
        }

        // 转换为PNG
        std::vector<uint8_t> pngData = ConvertRgbToPng(rgbaData, width, height);

        if (pngData.empty()) {
            std::cout << "[ERROR] ConvertRgbToPng not success" << std::endl;
            return env.Null();
        }

        // 将PNG数据转换为base64
        std::string base64Data = base64_encode(pngData.data(), pngData.size());

        if (base64Data.empty()) {
            std::cout << "[ERROR] base64_encode not success" << std::endl;
            return env.Null();
        }

        Napi::String result = Napi::String::New(env, base64Data);
        return result;
    }
    catch (...) {
        Napi::Error::New(env, "Capture failed with unknown error").ThrowAsJavaScriptException();
        return env.Null();
    }
}