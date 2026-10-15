#pragma once
#include "gpu/GpuDevice.h"
#include <unordered_map>

namespace clipper {

// Converts a BGRA texture to a scaled NV12 texture on the GPU (BT.709 limited range).
class FrameScaler {
public:
  FrameScaler(const GpuDevice& g, int inW, int inH, int outW, int outH);
  void convert(ID3D11Texture2D* bgra, ID3D11Texture2D* nv12);

private:
  GpuDevice g_;
  winrt::com_ptr<ID3D11VideoDevice> vdev_;
  winrt::com_ptr<ID3D11VideoContext> vctx_;
  winrt::com_ptr<ID3D11VideoProcessorEnumerator> enum_;
  winrt::com_ptr<ID3D11VideoProcessor> vp_;
  std::unordered_map<ID3D11Texture2D*, winrt::com_ptr<ID3D11VideoProcessorInputView>> inViews_;
  std::unordered_map<ID3D11Texture2D*, winrt::com_ptr<ID3D11VideoProcessorOutputView>> outViews_;
};

}  // namespace clipper
