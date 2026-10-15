#include "gpu/GpuDevice.h"

namespace clipper {

GpuDevice createGpuDevice() {
  GpuDevice g;
  const UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_VIDEO_SUPPORT;
  const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
  winrt::check_hresult(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, levels, 2,
                                         D3D11_SDK_VERSION, g.device.put(), nullptr, g.context.put()));
  g.device.as<ID3D10Multithread>()->SetMultithreadProtected(TRUE);
  return g;
}

winrt::com_ptr<ID3D11Texture2D> createNv12Texture(const GpuDevice& g, int w, int h, const uint8_t* init) {
  D3D11_TEXTURE2D_DESC desc{};
  desc.Width = UINT(w);
  desc.Height = UINT(h);
  desc.MipLevels = 1;
  desc.ArraySize = 1;
  desc.Format = DXGI_FORMAT_NV12;
  desc.SampleDesc.Count = 1;
  desc.Usage = D3D11_USAGE_DEFAULT;
  desc.BindFlags = D3D11_BIND_RENDER_TARGET;
  D3D11_SUBRESOURCE_DATA data{init, UINT(w), 0};
  winrt::com_ptr<ID3D11Texture2D> tex;
  winrt::check_hresult(g.device->CreateTexture2D(&desc, init ? &data : nullptr, tex.put()));
  return tex;
}

}  // namespace clipper
