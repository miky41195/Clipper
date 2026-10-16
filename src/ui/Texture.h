#pragma once
#include <d3d11.h>
#include <imgui.h>
#include <winrt/base.h>

namespace clipper {

// The D3D11 device ImGui renders with (set by UiWindow / renderOffscreen).
void setUiDevice(ID3D11Device* device);
ID3D11Device* uiDevice();

// BGRA texture for ImGui::Image; re-created when the size changes, otherwise updated in place.
class Texture {
public:
  bool upload(const uint8_t* bgra, int width, int height);
  void reset();
  bool valid() const { return srv_ != nullptr; }
  ImTextureID id() const { return ImTextureID(reinterpret_cast<intptr_t>(srv_.get())); }
  int width() const { return w_; }
  int height() const { return h_; }

private:
  winrt::com_ptr<ID3D11Texture2D> tex_;
  winrt::com_ptr<ID3D11ShaderResourceView> srv_;
  ID3D11Device* device_ = nullptr;
  int w_ = 0;
  int h_ = 0;
};

}  // namespace clipper
