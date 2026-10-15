#pragma once
#include <d3d11_4.h>
#include <winrt/base.h>

namespace clipper {

struct GpuDevice {
  winrt::com_ptr<ID3D11Device> device;
  winrt::com_ptr<ID3D11DeviceContext> context;
};

GpuDevice createGpuDevice();
winrt::com_ptr<ID3D11Texture2D> createNv12Texture(const GpuDevice& g, int w, int h, const uint8_t* init = nullptr);

}  // namespace clipper
