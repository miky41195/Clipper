#include "capture/FrameScaler.h"

namespace clipper {
using winrt::check_hresult;

FrameScaler::FrameScaler(const GpuDevice& g, int inW, int inH, int outW, int outH) : g_(g) {
  vdev_ = g.device.as<ID3D11VideoDevice>();
  vctx_ = g.context.as<ID3D11VideoContext>();
  D3D11_VIDEO_PROCESSOR_CONTENT_DESC cd{};
  cd.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
  cd.InputFrameRate = {60, 1};
  cd.InputWidth = UINT(inW);
  cd.InputHeight = UINT(inH);
  cd.OutputFrameRate = {60, 1};
  cd.OutputWidth = UINT(outW);
  cd.OutputHeight = UINT(outH);
  cd.Usage = D3D11_VIDEO_USAGE_OPTIMAL_SPEED;
  check_hresult(vdev_->CreateVideoProcessorEnumerator(&cd, enum_.put()));
  check_hresult(vdev_->CreateVideoProcessor(enum_.get(), 0, vp_.put()));

  D3D11_VIDEO_PROCESSOR_COLOR_SPACE in{};
  in.RGB_Range = 0;  // full range RGB
  D3D11_VIDEO_PROCESSOR_COLOR_SPACE out{};
  out.YCbCr_Matrix = 1;  // BT.709
  out.Nominal_Range = D3D11_VIDEO_PROCESSOR_NOMINAL_RANGE_16_235;
  vctx_->VideoProcessorSetStreamColorSpace(vp_.get(), 0, &in);
  vctx_->VideoProcessorSetOutputColorSpace(vp_.get(), &out);
  vctx_->VideoProcessorSetStreamAutoProcessingMode(vp_.get(), 0, FALSE);
  vctx_->VideoProcessorSetStreamFrameFormat(vp_.get(), 0, D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE);
  const RECT src{0, 0, inW, inH};
  const RECT dst{0, 0, outW, outH};
  vctx_->VideoProcessorSetStreamSourceRect(vp_.get(), 0, TRUE, &src);
  vctx_->VideoProcessorSetStreamDestRect(vp_.get(), 0, TRUE, &dst);
  vctx_->VideoProcessorSetOutputTargetRect(vp_.get(), TRUE, &dst);
}

void FrameScaler::convert(ID3D11Texture2D* bgra, ID3D11Texture2D* nv12) {
  auto& iv = inViews_[bgra];
  if (!iv) {
    D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC d{};
    d.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D;
    check_hresult(vdev_->CreateVideoProcessorInputView(bgra, enum_.get(), &d, iv.put()));
  }
  auto& ov = outViews_[nv12];
  if (!ov) {
    D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC d{};
    d.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
    check_hresult(vdev_->CreateVideoProcessorOutputView(nv12, enum_.get(), &d, ov.put()));
  }
  D3D11_VIDEO_PROCESSOR_STREAM stream{};
  stream.Enable = TRUE;
  stream.pInputSurface = iv.get();
  check_hresult(vctx_->VideoProcessorBlt(vp_.get(), ov.get(), 0, 1, &stream));
}

}  // namespace clipper
