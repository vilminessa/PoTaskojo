#include "app/renderer.h"

#include <dxgi.h>

#include "core/log.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

namespace app {

bool Renderer::create_device_and_swapchain(HWND hwnd, int width, int height) {
  DXGI_SWAP_CHAIN_DESC sd{};
  sd.BufferCount = 2;
  sd.BufferDesc.Width = width;
  sd.BufferDesc.Height = height;
  sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  sd.BufferDesc.RefreshRate.Numerator = 60;
  sd.BufferDesc.RefreshRate.Denominator = 1;
  sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
  sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  sd.OutputWindow = hwnd;
  sd.SampleDesc.Count = 1;
  sd.SampleDesc.Quality = 0;
  sd.Windowed = TRUE;
  sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

  UINT flags = 0;
#ifdef _DEBUG
  flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
  const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
  D3D_FEATURE_LEVEL got{};

  HRESULT hr = D3D11CreateDeviceAndSwapChain(
      nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, levels, 2,
      D3D11_SDK_VERSION, &sd, &swapchain_, &device_, &got, &context_);
  if (FAILED(hr)) {
    core::log::warn("D3D11 hardware device failed, falling back to WARP");
    hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, levels, 2,
        D3D11_SDK_VERSION, &sd, &swapchain_, &device_, &got, &context_);
  }
  return SUCCEEDED(hr);
}

bool Renderer::init(HWND hwnd, int width, int height) {
  hwnd_ = hwnd;
  width_ = width;
  height_ = height;
  if (!create_device_and_swapchain(hwnd, width, height)) {
    core::log::error("failed to create D3D11 device/swapchain");
    return false;
  }

  ComPtr<ID3D11Texture2D> backbuffer;
  swapchain_->GetBuffer(0, IID_PPV_ARGS(&backbuffer));
  device_->CreateRenderTargetView(backbuffer.Get(), nullptr, &rtv_);
  core::log::info("D3D11 renderer initialized");
  return rtv_ != nullptr;
}

void Renderer::shutdown() {
  rtv_.Reset();
  swapchain_.Reset();
  context_.Reset();
  device_.Reset();
}

void Renderer::resize(int width, int height) {
  if (!swapchain_ || width <= 0 || height <= 0) return;
  context_->OMSetRenderTargets(0, nullptr, nullptr);
  rtv_.Reset();
  swapchain_->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
  ComPtr<ID3D11Texture2D> backbuffer;
  swapchain_->GetBuffer(0, IID_PPV_ARGS(&backbuffer));
  device_->CreateRenderTargetView(backbuffer.Get(), nullptr, &rtv_);
  width_ = width;
  height_ = height;
}

void Renderer::begin_frame(float r, float g, float b, float a) {
  if (!rtv_) return;
  const float clear[4] = {r, g, b, a};
  context_->OMSetRenderTargets(1, rtv_.GetAddressOf(), nullptr);
  context_->ClearRenderTargetView(rtv_.Get(), clear);
}

void Renderer::end_frame() {
  if (swapchain_) swapchain_->Present(1, 0);  // vsync
}

}  // namespace app
