#pragma once
#include "capture/FrameScaler.h"
#include "gpu/GpuDevice.h"
#include <atomic>
#include <mutex>
#include <windows.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>

namespace clipper {

// Windows.Graphics.Capture of one monitor. Each new frame is copied into an owned BGRA texture.
class ScreenCapture {
public:
  ScreenCapture(const GpuDevice& g, HMONITOR monitor, bool cursor);
  ~ScreenCapture();
  ScreenCapture(const ScreenCapture&) = delete;
  ScreenCapture& operator=(const ScreenCapture&) = delete;

  int width() const { return width_; }
  int height() const { return height_; }
  // Scales the latest frame into nv12. Returns false until the first frame has arrived.
  bool convertLatest(FrameScaler& scaler, ID3D11Texture2D* nv12);
  bool sizeChanged() const { return sizeChanged_; }

private:
  void onFrame(const winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool& pool,
               const winrt::Windows::Foundation::IInspectable&);

  GpuDevice g_;
  int width_ = 0;
  int height_ = 0;
  winrt::Windows::Graphics::Capture::GraphicsCaptureItem item_{nullptr};
  winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice device_{nullptr};
  winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool pool_{nullptr};
  winrt::Windows::Graphics::Capture::GraphicsCaptureSession session_{nullptr};
  winrt::event_token token_{};
  std::mutex mu_;
  winrt::com_ptr<ID3D11Texture2D> latest_;
  bool hasFrame_ = false;
  std::atomic<bool> sizeChanged_{false};
};

}  // namespace clipper
