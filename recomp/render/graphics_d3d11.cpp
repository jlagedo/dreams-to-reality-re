#include "render/graphics_backend.h"

#include <d3d11.h>
#include <dxgi1_2.h>

#include <cstdio>
#include <new>
#include <vector>

namespace od {
namespace {

struct D3DState {
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    IDXGISwapChain1* swapchain = nullptr;
    ID3D11RenderTargetView* render_view = nullptr;
    int width = 0;
    int height = 0;
};

void set_hr_error(std::string& error, const char* operation, HRESULT hr) {
    char code[24]{};
    std::snprintf(code, sizeof(code), "0x%08lX", static_cast<unsigned long>(hr));
    error = std::string(operation) + " failed (HRESULT " + code + ")";
}

bool create_render_view(D3DState& state, std::string& error) {
    ID3D11Texture2D* back_buffer = nullptr;
    HRESULT hr = state.swapchain->GetBuffer(0, __uuidof(ID3D11Texture2D),
                                            reinterpret_cast<void**>(&back_buffer));
    if (FAILED(hr)) {
        set_hr_error(error, "DXGI back-buffer acquisition", hr);
        return false;
    }
    hr = state.device->CreateRenderTargetView(back_buffer, nullptr, &state.render_view);
    back_buffer->Release();
    if (FAILED(hr)) {
        set_hr_error(error, "D3D11 render-target creation", hr);
        return false;
    }
    return true;
}

} // namespace

SDL_WindowFlags GraphicsBackend::window_flags() { return static_cast<SDL_WindowFlags>(0); }

bool GraphicsBackend::configure_window(std::string&) { return true; }

bool GraphicsBackend::init(SDL_Window* window, std::string& error) {
    auto* state = new (std::nothrow) D3DState();
    if (!state) {
        error = "out of memory while creating D3D11 state";
        return false;
    }
    state_ = state;
    const HWND hwnd = static_cast<HWND>(SDL_GetPointerProperty(
        SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
    if (!hwnd) {
        error = "SDL did not expose a Win32 window handle";
        return false;
    }
    if (!SDL_GetWindowSizeInPixels(window, &state->width, &state->height)) {
        error = std::string("SDL drawable-size query failed: ") + SDL_GetError();
        return false;
    }
    if (state->width < 1) state->width = 1;
    if (state->height < 1) state->height = 1;

    const D3D_FEATURE_LEVEL requested[] = {D3D_FEATURE_LEVEL_11_0};
    D3D_FEATURE_LEVEL actual{};
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                                   D3D11_CREATE_DEVICE_BGRA_SUPPORT, requested, 1,
                                   D3D11_SDK_VERSION, &state->device, &actual, &state->context);
    if (FAILED(hr)) {
        set_hr_error(error, "D3D11 device creation", hr);
        return false;
    }

    IDXGIFactory2* factory = nullptr;
    hr = CreateDXGIFactory1(__uuidof(IDXGIFactory2), reinterpret_cast<void**>(&factory));
    if (FAILED(hr)) {
        set_hr_error(error, "DXGI factory creation", hr);
        return false;
    }
    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width = static_cast<UINT>(state->width);
    desc.Height = static_cast<UINT>(state->height);
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    desc.Scaling = DXGI_SCALING_STRETCH;
    desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    hr = factory->CreateSwapChainForHwnd(state->device, hwnd, &desc, nullptr,
                                         nullptr, &state->swapchain);
    factory->Release();
    if (FAILED(hr)) {
        set_hr_error(error, "DXGI swapchain creation", hr);
        return false;
    }
    return create_render_view(*state, error);
}

sg_environment GraphicsBackend::environment() const {
    const auto* state = static_cast<const D3DState*>(state_);
    sg_environment env{};
    env.defaults.color_format = SG_PIXELFORMAT_BGRA8;
    env.defaults.depth_format = SG_PIXELFORMAT_NONE;
    env.defaults.sample_count = 1;
    env.d3d11.device = state->device;
    env.d3d11.device_context = state->context;
    return env;
}

FrameState GraphicsBackend::acquire(SDL_Window* window, sg_swapchain& out, std::string& error) {
    auto* state = static_cast<D3DState*>(state_);
    int width = 0;
    int height = 0;
    if (!SDL_GetWindowSizeInPixels(window, &width, &height)) {
        error = std::string("SDL drawable-size query failed: ") + SDL_GetError();
        return FrameState::failed;
    }
    if (width == 0 || height == 0) return FrameState::skipped;
    if (width != state->width || height != state->height) {
        state->context->OMSetRenderTargets(0, nullptr, nullptr);
        state->render_view->Release();
        state->render_view = nullptr;
        const HRESULT hr = state->swapchain->ResizeBuffers(0, static_cast<UINT>(width),
                                                           static_cast<UINT>(height),
                                                           DXGI_FORMAT_UNKNOWN, 0);
        if (FAILED(hr)) {
            set_hr_error(error, "DXGI swapchain resize", hr);
            return FrameState::failed;
        }
        state->width = width;
        state->height = height;
        if (!create_render_view(*state, error)) return FrameState::failed;
    }
    out.width = state->width;
    out.height = state->height;
    out.color_format = SG_PIXELFORMAT_BGRA8;
    out.depth_format = SG_PIXELFORMAT_NONE;
    out.sample_count = 1;
    out.d3d11.render_view = state->render_view;
    return FrameState::ready;
}

bool GraphicsBackend::capture(const std::string& png_path, std::string& error) {
    auto* state = static_cast<D3DState*>(state_);
    if (!state || !state->swapchain || state->width < 1 || state->height < 1) {
        error = "D3D11 frame capture has no completed swapchain";
        return false;
    }
    ID3D11Resource* rendered_resource = nullptr;
    state->render_view->GetResource(&rendered_resource);
    if (!rendered_resource) {
        error = "D3D11 frame capture has no rendered color target";
        return false;
    }
    ID3D11Texture2D* back_buffer = nullptr;
    HRESULT hr = rendered_resource->QueryInterface(__uuidof(ID3D11Texture2D),
                                                   reinterpret_cast<void**>(&back_buffer));
    rendered_resource->Release();
    if (FAILED(hr)) {
        set_hr_error(error, "capture render-target texture", hr);
        return false;
    }
    D3D11_TEXTURE2D_DESC desc{};
    back_buffer->GetDesc(&desc);
    if (desc.Format != DXGI_FORMAT_B8G8R8A8_UNORM || desc.SampleDesc.Count != 1) {
        back_buffer->Release();
        error = "frame capture requires a single-sample BGRA8 back buffer";
        return false;
    }
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.MiscFlags = 0;
    ID3D11Texture2D* staging = nullptr;
    hr = state->device->CreateTexture2D(&desc, nullptr, &staging);
    if (FAILED(hr)) {
        back_buffer->Release();
        set_hr_error(error, "capture staging texture", hr);
        return false;
    }
    state->context->CopyResource(staging, back_buffer);
    back_buffer->Release();
    D3D11_MAPPED_SUBRESOURCE mapped{};
    hr = state->context->Map(staging, 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) {
        staging->Release();
        set_hr_error(error, "capture readback", hr);
        return false;
    }
    ++downloads_;
    std::vector<uint8_t> rgba(static_cast<size_t>(desc.Width) * desc.Height * 4u);
    for (UINT y = 0; y < desc.Height; ++y) {
        const auto* src = static_cast<const uint8_t*>(mapped.pData) +
                          static_cast<size_t>(y) * mapped.RowPitch;
        auto* dst = rgba.data() + static_cast<size_t>(y) * desc.Width * 4u;
        for (UINT x = 0; x < desc.Width; ++x) {
            dst[4*x] = src[4*x+2];
            dst[4*x+1] = src[4*x+1];
            dst[4*x+2] = src[4*x];
            dst[4*x+3] = 255;
        }
    }
    state->context->Unmap(staging, 0);
    staging->Release();
    SDL_Surface* surface = SDL_CreateSurfaceFrom(static_cast<int>(desc.Width),
        static_cast<int>(desc.Height), SDL_PIXELFORMAT_RGBA32, rgba.data(),
        static_cast<int>(desc.Width * 4u));
    if (!surface) {
        error = std::string("cannot create capture surface: ") + SDL_GetError();
        return false;
    }
    const bool saved = SDL_SavePNG(surface, png_path.c_str());
    if (!saved) error = std::string("cannot save frame capture: ") + SDL_GetError();
    SDL_DestroySurface(surface);
    return saved;
}

bool GraphicsBackend::present(std::string& error) {
    auto* state = static_cast<D3DState*>(state_);
    const HRESULT hr = state->swapchain->Present(1, 0);
    if (FAILED(hr)) {
        set_hr_error(error, "DXGI presentation", hr);
        return false;
    }
    return true;
}

bool GraphicsBackend::read_image(sg_image image,int x,int y,int width,int height,
                                 std::vector<uint32_t>& rgba,std::string& error) {
    auto* state=static_cast<D3DState*>(state_);
    if(!state||!sg_isvalid()||sg_query_image_state(image)!=SG_RESOURCESTATE_VALID||
       x<0||y<0||width<=0||height<=0){error="invalid GPU export request";return false;}
    auto* source=static_cast<ID3D11Texture2D*>(const_cast<void*>(sg_d3d11_query_image_info(image).tex2d));
    if(!state||!source||x<0||y<0||width<=0||height<=0){error="invalid GPU export request";return false;}
    D3D11_TEXTURE2D_DESC desc{};source->GetDesc(&desc);
    if(uint64_t(x)+width>desc.Width||uint64_t(y)+height>desc.Height||desc.SampleDesc.Count!=1||
       (desc.Format!=DXGI_FORMAT_R8G8B8A8_UNORM&&desc.Format!=DXGI_FORMAT_B8G8R8A8_UNORM)) {
        error="GPU export requires an in-bounds single-sample RGBA8/BGRA8 region";return false;
    }
    const bool bgra=desc.Format==DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.Width=UINT(width);desc.Height=UINT(height);desc.MipLevels=1;desc.ArraySize=1;
    desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;desc.MiscFlags=0;
    ID3D11Texture2D* staging=nullptr;
    HRESULT hr=state->device->CreateTexture2D(&desc,nullptr,&staging);
    if(FAILED(hr)){set_hr_error(error,"export staging texture",hr);return false;}
    const D3D11_BOX box{UINT(x),UINT(y),0,UINT(x+width),UINT(y+height),1};
    state->context->CopySubresourceRegion(staging,0,0,0,0,source,0,&box);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    hr=state->context->Map(staging,0,D3D11_MAP_READ,0,&mapped);
    if(FAILED(hr)){staging->Release();set_hr_error(error,"export readback",hr);return false;}
    ++downloads_;
    rgba.resize(size_t(width)*height);
    for(int row=0;row<height;++row) {
        const auto* bytes=static_cast<const uint8_t*>(mapped.pData)+size_t(row)*mapped.RowPitch;
        for(int col=0;col<width;++col) {
            const auto* p=bytes+col*4;
            rgba[size_t(row)*width+col]=uint32_t(p[bgra?2:0])|uint32_t(p[1])<<8|
                uint32_t(p[bgra?0:2])<<16|uint32_t(p[3])<<24;
        }
    }
    state->context->Unmap(staging,0);staging->Release();sg_reset_state_cache();return true;
}
void GraphicsBackend::shutdown() {
    auto* state = static_cast<D3DState*>(state_);
    if (!state) return;
    if (state->render_view) state->render_view->Release();
    if (state->swapchain) state->swapchain->Release();
    if (state->context) state->context->Release();
    if (state->device) state->device->Release();
    delete state;
    state_ = nullptr;
}

} // namespace od
