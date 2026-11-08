#include <capapp/ui/d3d11_renderer.hpp>

#if defined(_WIN32)
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <iostream>
#include <cstring>
#include <utility>

namespace capapp::ui {
namespace {

using Microsoft::WRL::ComPtr;

constexpr uint32_t kHudSurfaceWidth = 960;
constexpr uint32_t kHudSurfaceHeight = 760;
constexpr size_t kVideoTextureCount = 3;

constexpr char kVertexShader[] = R"(
struct VSOutput {
    float4 position : SV_Position;
    float2 texCoord : TEXCOORD0;
};

VSOutput main(uint vertexId : SV_VertexID) {
    float2 texCoord = float2((vertexId << 1) & 2, vertexId & 2);
    VSOutput output;
    output.position = float4(texCoord * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    output.texCoord = texCoord;
    return output;
}
)";

constexpr char kVideoPixelShader[] = R"(
Texture2D videoTexture : register(t0);
SamplerState videoSampler : register(s0);

cbuffer ScaleParams : register(b0) {
    float2 sourceSize;
    float2 outputSize;
    uint horizontalPass;
    uint clampOutput;
    float2 padding;
};

float sinc(float value) {
    if (abs(value) < 0.00001) return 1.0;
    float angle = 3.14159265359 * value;
    return sin(angle) / angle;
}

float4 main(float4 position : SV_Position, float2 texCoord : TEXCOORD0) : SV_Target {
    float sourceAxis = horizontalPass != 0 ? sourceSize.x : sourceSize.y;
    float outputAxis = horizontalPass != 0 ? outputSize.x : outputSize.y;
    float coordinate = horizontalPass != 0 ? texCoord.x : texCoord.y;
    float center = coordinate * sourceAxis - 0.5;
    float scale = max(sourceAxis / outputAxis, 1.0);
    float support = 3.0 * scale;
    int firstSample = (int)ceil(center - support);
    int lastSample = (int)floor(center + support);
    float4 color = 0.0;
    float totalWeight = 0.0;

    [loop]
    for (int sampleIndex = firstSample; sampleIndex <= lastSample; ++sampleIndex) {
        float distance = (center - sampleIndex) / scale;
        float weight = sinc(distance) * sinc(distance / 3.0);
        float samplePosition =
            (clamp(float(sampleIndex), 0.0, sourceAxis - 1.0) + 0.5) / sourceAxis;
        float2 sampleCoord = texCoord;
        if (horizontalPass != 0) sampleCoord.x = samplePosition;
        else sampleCoord.y = samplePosition;
        color += videoTexture.SampleLevel(videoSampler, sampleCoord, 0.0) * weight;
        totalWeight += weight;
    }

    color /= max(totalWeight, 0.00001);
    return clampOutput != 0 ? saturate(color) : color;
}
)";

constexpr char kHudPixelShader[] = R"(
Texture2D hudTexture : register(t0);
SamplerState hudSampler : register(s0);

float4 main(float4 position : SV_Position, float2 texCoord : TEXCOORD0) : SV_Target {
    float4 color = hudTexture.Sample(hudSampler, texCoord);
    clip(max(max(color.r, color.g), color.b) - (1.0 / 255.0));
    return float4(color.rgb, 1.0);
}
)";

bool compile_shader(const char* source, const char* target, ComPtr<ID3DBlob>& bytecode) {
    ComPtr<ID3DBlob> errors;
    const HRESULT hr = D3DCompile(source, std::strlen(source), nullptr, nullptr, nullptr,
                                  "main", target, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0,
                                  &bytecode, &errors);
    if (FAILED(hr)) {
        if (errors) {
            std::cerr << "[D3D11] Shader compilation failed: "
                      << static_cast<const char*>(errors->GetBufferPointer()) << "\n";
        } else {
            std::cerr << "[D3D11] Shader compilation failed: 0x"
                      << std::hex << hr << std::dec << "\n";
        }
        return false;
    }
    return true;
}

} // namespace

struct D3D11Renderer::Impl {
    HWND window{nullptr};
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<IDXGISwapChain> swapChain;
    ComPtr<ID3D11RenderTargetView> renderTarget;
    ComPtr<ID3D11VertexShader> vertexShader;
    ComPtr<ID3D11PixelShader> videoPixelShader;
    ComPtr<ID3D11PixelShader> hudPixelShader;
    ComPtr<ID3D11SamplerState> sampler;
    ComPtr<ID3D11Buffer> scaleParamsBuffer;
    std::array<ComPtr<ID3D11Texture2D>, kVideoTextureCount> videoTextures;
    std::array<ComPtr<ID3D11ShaderResourceView>, kVideoTextureCount> videoViews;
    size_t videoTextureIndex{0};
    ComPtr<ID3D11Texture2D> scaleIntermediate;
    ComPtr<ID3D11RenderTargetView> scaleIntermediateTarget;
    ComPtr<ID3D11ShaderResourceView> scaleIntermediateView;
    uint32_t intermediateWidth{0};
    uint32_t intermediateHeight{0};
    ComPtr<ID3D11Texture2D> hudTexture;
    ComPtr<ID3D11ShaderResourceView> hudView;
    HDC hudDc{nullptr};
    HBITMAP hudBitmap{nullptr};
    HBITMAP oldHudBitmap{nullptr};
    uint8_t* hudPixels{nullptr};
    uint32_t videoWidth{0};
    uint32_t videoHeight{0};
    uint32_t hudWidth{0};
    uint32_t hudHeight{0};
    uint32_t backbufferWidth{0};
    uint32_t backbufferHeight{0};

    ~Impl() {
        if (hudDc) {
            if (oldHudBitmap) SelectObject(hudDc, oldHudBitmap);
            if (hudBitmap) DeleteObject(hudBitmap);
            DeleteDC(hudDc);
        }
    }

    bool create_render_target(uint32_t width, uint32_t height) {
        renderTarget.Reset();
        if (FAILED(swapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0))) {
            std::cerr << "[D3D11] Failed to resize the presentation buffers.\n";
            return false;
        }

        ComPtr<ID3D11Texture2D> backbuffer;
        if (FAILED(swapChain->GetBuffer(0, IID_PPV_ARGS(&backbuffer))) ||
            FAILED(device->CreateRenderTargetView(backbuffer.Get(), nullptr, &renderTarget))) {
            std::cerr << "[D3D11] Failed to create the presentation render target.\n";
            return false;
        }
        backbufferWidth = width;
        backbufferHeight = height;
        return true;
    }

    bool create_video_texture(uint32_t width, uint32_t height) {
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = width;
        desc.Height = height;
        desc.MipLevels = 0;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
        desc.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
        std::array<ComPtr<ID3D11Texture2D>, kVideoTextureCount> textures;
        std::array<ComPtr<ID3D11ShaderResourceView>, kVideoTextureCount> views;
        for (size_t i = 0; i < kVideoTextureCount; ++i) {
            if (FAILED(device->CreateTexture2D(&desc, nullptr, &textures[i])) ||
                FAILED(device->CreateShaderResourceView(textures[i].Get(), nullptr, &views[i]))) {
                std::cerr << "[D3D11] Failed to allocate capture upload textures.\n";
                return false;
            }
        }
        videoTextures = std::move(textures);
        videoViews = std::move(views);
        videoTextureIndex = 0;
        videoWidth = width;
        videoHeight = height;
        return true;
    }

    bool create_scale_intermediate(uint32_t width, uint32_t height) {
        if (width == intermediateWidth && height == intermediateHeight) return true;
        ComPtr<ID3D11Texture2D> texture;
        ComPtr<ID3D11RenderTargetView> target;
        ComPtr<ID3D11ShaderResourceView> view;
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = width;
        desc.Height = height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        if (FAILED(device->CreateTexture2D(&desc, nullptr, &texture)) ||
            FAILED(device->CreateRenderTargetView(texture.Get(), nullptr, &target)) ||
            FAILED(device->CreateShaderResourceView(texture.Get(), nullptr, &view))) {
            std::cerr << "[D3D11] Failed to create the Lanczos scaling intermediate.\n";
            return false;
        }
        scaleIntermediate = std::move(texture);
        scaleIntermediateTarget = std::move(target);
        scaleIntermediateView = std::move(view);
        intermediateWidth = width;
        intermediateHeight = height;
        return true;
    }

    bool set_scale_params(uint32_t sourceWidth, uint32_t sourceHeight,
                          uint32_t outputWidth, uint32_t outputHeight,
                          bool horizontal, bool clampOutput) {
        struct ScaleParams {
            float sourceWidth;
            float sourceHeight;
            float outputWidth;
            float outputHeight;
            uint32_t horizontal;
            uint32_t clampOutput;
            float padding[2]{};
        } params{
            static_cast<float>(sourceWidth), static_cast<float>(sourceHeight),
            static_cast<float>(outputWidth), static_cast<float>(outputHeight),
            horizontal ? 1u : 0u, clampOutput ? 1u : 0u
        };
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(context->Map(scaleParamsBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD,
                                0, &mapped))) return false;
        std::memcpy(mapped.pData, &params, sizeof(params));
        context->Unmap(scaleParamsBuffer.Get(), 0);
        context->PSSetConstantBuffers(0, 1, scaleParamsBuffer.GetAddressOf());
        return true;
    }

    bool create_hud_resources(uint32_t width, uint32_t height) {
        if (hudDc) {
            if (oldHudBitmap) SelectObject(hudDc, oldHudBitmap);
            if (hudBitmap) DeleteObject(hudBitmap);
            DeleteDC(hudDc);
            hudDc = nullptr;
            hudBitmap = nullptr;
            oldHudBitmap = nullptr;
            hudPixels = nullptr;
        }
        hudView.Reset();
        hudTexture.Reset();

        hudDc = CreateCompatibleDC(nullptr);
        if (!hudDc) return false;
        BITMAPINFO bmi{};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = static_cast<LONG>(width);
        bmi.bmiHeader.biHeight = -static_cast<LONG>(height);
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;
        void* pixels = nullptr;
        hudBitmap = CreateDIBSection(hudDc, &bmi, DIB_RGB_COLORS, &pixels, nullptr, 0);
        if (!hudBitmap || !pixels) return false;
        oldHudBitmap = static_cast<HBITMAP>(SelectObject(hudDc, hudBitmap));
        hudPixels = static_cast<uint8_t*>(pixels);

        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = width;
        desc.Height = height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DYNAMIC;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(device->CreateTexture2D(&desc, nullptr, &hudTexture)) ||
            FAILED(device->CreateShaderResourceView(hudTexture.Get(), nullptr, &hudView))) {
            std::cerr << "[D3D11] Failed to allocate the HUD texture.\n";
            return false;
        }
        hudWidth = width;
        hudHeight = height;
        return true;
    }

    bool upload_texture(ID3D11Texture2D* texture, const uint8_t* pixels,
                        uint32_t width, uint32_t height, uint32_t sourceStride) {
        const size_t rowBytes = static_cast<size_t>(width) * 4;
        if (sourceStride < rowBytes) return false;

        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(context->Map(texture, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
            return false;
        }
        if (mapped.RowPitch == rowBytes && sourceStride == rowBytes) {
            std::memcpy(mapped.pData, pixels, rowBytes * height);
        } else {
            for (uint32_t row = 0; row < height; ++row) {
                std::memcpy(static_cast<uint8_t*>(mapped.pData) + row * mapped.RowPitch,
                            pixels + static_cast<size_t>(row) * sourceStride, rowBytes);
            }
        }
        context->Unmap(texture, 0);
        return true;
    }

    void upload_video_texture(ID3D11Texture2D* texture, ID3D11ShaderResourceView* view,
                              const uint8_t* pixels, uint32_t sourceStride) {
        context->UpdateSubresource(texture, 0, nullptr, pixels, sourceStride, 0);
        context->GenerateMips(view);
    }
};

D3D11Renderer::D3D11Renderer() : impl_(std::make_unique<Impl>()) {}
D3D11Renderer::~D3D11Renderer() = default;

bool D3D11Renderer::initialize(HWND window) {
    if (!window) return false;
    impl_->window = window;

    DXGI_SWAP_CHAIN_DESC swapDesc{};
    swapDesc.BufferCount = 2;
    swapDesc.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    swapDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapDesc.OutputWindow = window;
    swapDesc.SampleDesc.Count = 1;
    swapDesc.Windowed = TRUE;
    swapDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    D3D_FEATURE_LEVEL featureLevel{};
    constexpr D3D_FEATURE_LEVEL featureLevels[] = {
        D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0
    };
    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
        featureLevels, static_cast<UINT>(std::size(featureLevels)), D3D11_SDK_VERSION,
        &swapDesc, &impl_->swapChain, &impl_->device, &featureLevel, &impl_->context);
    if (hr == E_INVALIDARG) {
        hr = D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
            featureLevels + 1, static_cast<UINT>(std::size(featureLevels) - 1),
            D3D11_SDK_VERSION, &swapDesc, &impl_->swapChain, &impl_->device,
            &featureLevel, &impl_->context);
    }
    if (FAILED(hr)) {
        impl_->swapChain.Reset();
        impl_->context.Reset();
        impl_->device.Reset();
        swapDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
        hr = D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
            featureLevels, static_cast<UINT>(std::size(featureLevels)), D3D11_SDK_VERSION,
            &swapDesc, &impl_->swapChain, &impl_->device, &featureLevel, &impl_->context);
        if (hr == E_INVALIDARG) {
            hr = D3D11CreateDeviceAndSwapChain(
                nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                featureLevels + 1, static_cast<UINT>(std::size(featureLevels) - 1),
                D3D11_SDK_VERSION, &swapDesc, &impl_->swapChain, &impl_->device,
                &featureLevel, &impl_->context);
        }
    }
    if (FAILED(hr)) {
        std::cerr << "[D3D11] Failed to create hardware device and swap chain: 0x"
                  << std::hex << hr << std::dec << "\n";
        return false;
    }

    ComPtr<ID3DBlob> vertexCode;
    ComPtr<ID3DBlob> videoCode;
    ComPtr<ID3DBlob> hudCode;
    if (!compile_shader(kVertexShader, "vs_4_0", vertexCode) ||
        !compile_shader(kVideoPixelShader, "ps_4_0", videoCode) ||
        !compile_shader(kHudPixelShader, "ps_4_0", hudCode) ||
        FAILED(impl_->device->CreateVertexShader(vertexCode->GetBufferPointer(), vertexCode->GetBufferSize(),
                                                  nullptr, &impl_->vertexShader)) ||
        FAILED(impl_->device->CreatePixelShader(videoCode->GetBufferPointer(), videoCode->GetBufferSize(),
                                                 nullptr, &impl_->videoPixelShader)) ||
        FAILED(impl_->device->CreatePixelShader(hudCode->GetBufferPointer(), hudCode->GetBufferSize(),
                                                 nullptr, &impl_->hudPixelShader))) {
        std::cerr << "[D3D11] Failed to create presentation shaders.\n";
        return false;
    }

    D3D11_BUFFER_DESC scaleParamsDesc{};
    scaleParamsDesc.ByteWidth = 32;
    scaleParamsDesc.Usage = D3D11_USAGE_DYNAMIC;
    scaleParamsDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    scaleParamsDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(impl_->device->CreateBuffer(&scaleParamsDesc, nullptr,
                                            &impl_->scaleParamsBuffer))) {
        std::cerr << "[D3D11] Failed to create the Lanczos scaling parameters.\n";
        return false;
    }

    D3D11_SAMPLER_DESC samplerDesc{};
    samplerDesc.Filter = D3D11_FILTER_ANISOTROPIC;
    samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.MaxAnisotropy = 16;
    samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
    if (FAILED(impl_->device->CreateSamplerState(&samplerDesc, &impl_->sampler))) return false;

    RECT rect{};
    GetClientRect(window, &rect);
    const uint32_t width = static_cast<uint32_t>(rect.right - rect.left);
    const uint32_t height = static_cast<uint32_t>(rect.bottom - rect.top);
    if (!width || !height || !impl_->create_render_target(width, height)) return false;

    std::cout << "[D3D11] Hardware-accelerated presentation initialized.\n";
    return true;
}

bool D3D11Renderer::render(const Frame& frame, bool showHud,
                           const std::function<void(HDC, int, int)>& drawHud,
                           bool useLanczos) {
    if (!impl_->device || !impl_->window) return false;
    RECT rect{};
    GetClientRect(impl_->window, &rect);
    const uint32_t width = static_cast<uint32_t>(rect.right - rect.left);
    const uint32_t height = static_cast<uint32_t>(rect.bottom - rect.top);
    if (!width || !height) return true;
    if ((width != impl_->backbufferWidth || height != impl_->backbufferHeight) &&
        !impl_->create_render_target(width, height)) {
        return false;
    }

    if (frame.data && frame.width && frame.height) {
        if (frame.format != PixelFormat::BGRA8) {
            std::cerr << "[D3D11] Unsupported capture pixel format for GPU presentation.\n";
            return false;
        }
        if ((frame.width != impl_->videoWidth || frame.height != impl_->videoHeight) &&
            !impl_->create_video_texture(frame.width, frame.height)) {
            return false;
        }
        const uint32_t stride = frame.stride ? frame.stride : frame.width * 4;
        if (frame.dataSize > 0 &&
            static_cast<size_t>(stride) * frame.height > frame.dataSize) {
            std::cerr << "[D3D11] Capture frame buffer is smaller than its declared dimensions.\n";
            return false;
        }
        impl_->videoTextureIndex = (impl_->videoTextureIndex + 1) % kVideoTextureCount;
        impl_->upload_video_texture(
            impl_->videoTextures[impl_->videoTextureIndex].Get(),
            impl_->videoViews[impl_->videoTextureIndex].Get(),
            frame.data, stride);
    }

    if (showHud) {
        if ((kHudSurfaceWidth != impl_->hudWidth || kHudSurfaceHeight != impl_->hudHeight) &&
            !impl_->create_hud_resources(kHudSurfaceWidth, kHudSurfaceHeight)) {
            std::cerr << "[D3D11] Failed to create HUD drawing surface.\n";
            return false;
        }
        std::memset(impl_->hudPixels, 0,
                    static_cast<size_t>(kHudSurfaceWidth) * kHudSurfaceHeight * 4);
        if (drawHud) {
            drawHud(impl_->hudDc, static_cast<int>(kHudSurfaceWidth),
                    static_cast<int>(kHudSurfaceHeight));
        }
        if (!impl_->upload_texture(impl_->hudTexture.Get(), impl_->hudPixels,
                                   kHudSurfaceWidth, kHudSurfaceHeight,
                                   kHudSurfaceWidth * 4)) {
            return false;
        }
    }

    const float clearColor[4]{0.0f, 0.0f, 0.0f, 1.0f};
    impl_->context->OMSetRenderTargets(1, impl_->renderTarget.GetAddressOf(), nullptr);
    impl_->context->ClearRenderTargetView(impl_->renderTarget.Get(), clearColor);
    D3D11_VIEWPORT viewport{};
    viewport.Width = static_cast<float>(width);
    viewport.Height = static_cast<float>(height);
    viewport.MaxDepth = 1.0f;
    impl_->context->RSSetViewports(1, &viewport);
    impl_->context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    impl_->context->VSSetShader(impl_->vertexShader.Get(), nullptr, 0);
    impl_->context->PSSetSamplers(0, 1, impl_->sampler.GetAddressOf());

    if (frame.data && impl_->videoViews[impl_->videoTextureIndex]) {
        uint32_t videoViewWidth = width;
        uint32_t videoViewHeight = height;
        if (static_cast<uint64_t>(width) * frame.height >
            static_cast<uint64_t>(height) * frame.width) {
            videoViewWidth = static_cast<uint32_t>(
                static_cast<uint64_t>(height) * frame.width / frame.height);
        } else {
            videoViewHeight = static_cast<uint32_t>(
                static_cast<uint64_t>(width) * frame.height / frame.width);
        }
        videoViewWidth = std::max(videoViewWidth, 1u);
        videoViewHeight = std::max(videoViewHeight, 1u);
        D3D11_VIEWPORT videoViewport{};
        videoViewport.TopLeftX = static_cast<float>((width - videoViewWidth) / 2);
        videoViewport.TopLeftY = static_cast<float>((height - videoViewHeight) / 2);
        videoViewport.Width = static_cast<float>(videoViewWidth);
        videoViewport.Height = static_cast<float>(videoViewHeight);
        videoViewport.MaxDepth = 1.0f;
        impl_->context->PSSetShader(impl_->videoPixelShader.Get(), nullptr, 0);
        ID3D11ShaderResourceView* nullView = nullptr;
        if (useLanczos) {
            if (!impl_->create_scale_intermediate(videoViewWidth, frame.height)) return false;
            if (!impl_->set_scale_params(frame.width, frame.height,
                                         videoViewWidth, frame.height, true, false)) {
                std::cerr << "[D3D11] Failed to map Lanczos horizontal pass parameters.\n";
                return false;
            }
            D3D11_VIEWPORT horizontalViewport{};
            horizontalViewport.Width = static_cast<float>(videoViewWidth);
            horizontalViewport.Height = static_cast<float>(frame.height);
            horizontalViewport.MaxDepth = 1.0f;
            impl_->context->OMSetRenderTargets(
                1, impl_->scaleIntermediateTarget.GetAddressOf(), nullptr);
            impl_->context->RSSetViewports(1, &horizontalViewport);
            impl_->context->PSSetShaderResources(
                0, 1, impl_->videoViews[impl_->videoTextureIndex].GetAddressOf());
            impl_->context->Draw(3, 0);
            impl_->context->PSSetShaderResources(0, 1, &nullView);

            impl_->context->OMSetRenderTargets(1, impl_->renderTarget.GetAddressOf(), nullptr);
            impl_->context->RSSetViewports(1, &videoViewport);
            if (!impl_->set_scale_params(videoViewWidth, frame.height,
                                         videoViewWidth, videoViewHeight, false, true)) {
                std::cerr << "[D3D11] Failed to map Lanczos vertical pass parameters.\n";
                return false;
            }
            impl_->context->PSSetShaderResources(
                0, 1, impl_->scaleIntermediateView.GetAddressOf());
            impl_->context->Draw(3, 0);
            impl_->context->PSSetShaderResources(0, 1, &nullView);
        } else {
            impl_->context->OMSetRenderTargets(1, impl_->renderTarget.GetAddressOf(), nullptr);
            impl_->context->RSSetViewports(1, &videoViewport);
            impl_->context->PSSetShaderResources(
                0, 1, impl_->videoViews[impl_->videoTextureIndex].GetAddressOf());
            impl_->context->Draw(3, 0);
            impl_->context->PSSetShaderResources(0, 1, &nullView);
        }
    }

    if (showHud && impl_->hudView) {
        D3D11_VIEWPORT hudViewport{};
        hudViewport.Width = static_cast<float>(std::min(width, kHudSurfaceWidth));
        hudViewport.Height = static_cast<float>(std::min(height, kHudSurfaceHeight));
        hudViewport.MaxDepth = 1.0f;
        impl_->context->RSSetViewports(1, &hudViewport);
        impl_->context->PSSetShader(impl_->hudPixelShader.Get(), nullptr, 0);
        impl_->context->PSSetShaderResources(0, 1, impl_->hudView.GetAddressOf());
        impl_->context->Draw(3, 0);
        ID3D11ShaderResourceView* nullView = nullptr;
        impl_->context->PSSetShaderResources(0, 1, &nullView);
    }

    return SUCCEEDED(impl_->swapChain->Present(0, 0));
}

} // namespace capapp::ui

#else

namespace capapp::ui {
struct D3D11Renderer::Impl {};
D3D11Renderer::D3D11Renderer() : impl_(std::make_unique<Impl>()) {}
D3D11Renderer::~D3D11Renderer() = default;
} // namespace capapp::ui

#endif
