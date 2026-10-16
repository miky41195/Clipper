#include "ui/Texture.h"
#include <cstring>

namespace clipper {
namespace {
ID3D11Device* g_device = nullptr;
}

void setUiDevice(ID3D11Device* device) { g_device = device; }
ID3D11Device* uiDevice() { return g_device; }

void Texture::reset() {
  srv_ = nullptr;
  tex_ = nullptr;
  device_ = nullptr;
  w_ = h_ = 0;
}

bool Texture::upload(const uint8_t* bgra, int width, int height) {
  if (!g_device || width <= 0 || height <= 0) return false;
  if (!tex_ || width != w_ || height != h_ || device_ != g_device) {
    reset();
    D3D11_TEXTURE2D_DESC d{};
    d.Width = UINT(width);
    d.Height = UINT(height);
    d.MipLevels = 1;
    d.ArraySize = 1;
    d.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    d.SampleDesc.Count = 1;
    d.Usage = D3D11_USAGE_DYNAMIC;
    d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    d.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(g_device->CreateTexture2D(&d, nullptr, tex_.put()))) return false;
    if (FAILED(g_device->CreateShaderResourceView(tex_.get(), nullptr, srv_.put()))) {
      tex_ = nullptr;
      return false;
    }
    device_ = g_device;
    w_ = width;
    h_ = height;
  }
  winrt::com_ptr<ID3D11DeviceContext> ctx;
  g_device->GetImmediateContext(ctx.put());
  D3D11_MAPPED_SUBRESOURCE m{};
  if (FAILED(ctx->Map(tex_.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) return false;
  for (int y = 0; y < height; ++y)
    std::memcpy(static_cast<uint8_t*>(m.pData) + size_t(y) * m.RowPitch, bgra + size_t(y) * width * 4, size_t(width) * 4);
  ctx->Unmap(tex_.get(), 0);
  return true;
}

}  // namespace clipper
