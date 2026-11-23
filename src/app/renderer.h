#pragma once
// Direct3D 11 device/swapchain holder for ImGui rendering.

#include <d3d11.h>
#include <wrl/client.h>

#include <cstdint>

namespace app {

using Microsoft::WRL::ComPtr;

class Renderer {
 public:
  bool init(HWND hwnd, int width, int height);
  void shutdown();
  void resize(int width, int height);
  void begin_frame(float clear_r, float clear_g, float clear_b, float clear_a);
  void end_frame();  // present (vsync=1)

  ID3D11Device* device() const { return device_.Get(); }
  ID3D11DeviceContext* context() const { return context_.Get(); }
  bool ready() const { return device_ != nullptr; }

 private:
  bool create_device_and_swapchain(HWND hwnd, int width, int height);

  ComPtr<ID3D11Device> device_;
  ComPtr<ID3D11DeviceContext> context_;
  ComPtr<IDXGISwapChain> swapchain_;
  ComPtr<ID3D11RenderTargetView> rtv_;
  HWND hwnd_ = nullptr;
  int width_ = 0;
  int height_ = 0;
};

}  // namespace app
