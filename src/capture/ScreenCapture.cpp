#include "capture/ScreenCapture.h"
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <winrt/Windows.Foundation.Metadata.h>
#include <winrt/Windows.Foundation.h>

namespace clipper {
namespace wgc = winrt::Windows::Graphics::Capture;
namespace wdx = winrt::Windows::Graphics::DirectX;
using winrt::check_hresult;

ScreenCapture::ScreenCapture(const GpuDevice& g, HMONITOR monitor, bool cursor) : g_(g) {
  auto interop = winrt::get_activation_factory<wgc::GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
  check_hresult(interop->CreateForMonitor(monitor, winrt::guid_of<wgc::GraphicsCaptureItem>(), winrt::put_abi(item_)));
  winrt::com_ptr<::IInspectable> insp;
  check_hresult(CreateDirect3D11DeviceFromDXGIDevice(g.device.as<IDXGIDevice>().get(), insp.put()));
  device_ = insp.as<wdx::Direct3D11::IDirect3DDevice>();
  const auto size = item_.Size();
  width_ = size.Width;
  height_ = size.Height;

  D3D11_TEXTURE2D_DESC desc{};
  desc.Width = UINT(width_);
  desc.Height = UINT(height_);
  desc.MipLevels = 1;
  desc.ArraySize = 1;
  desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
  desc.SampleDesc.Count = 1;
  desc.Usage = D3D11_USAGE_DEFAULT;
  desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
  check_hresult(g.device->CreateTexture2D(&desc, nullptr, latest_.put()));

  pool_ = wgc::Direct3D11CaptureFramePool::CreateFreeThreaded(device_, wdx::DirectXPixelFormat::B8G8R8A8UIntNormalized, 2, size);
  session_ = pool_.CreateCaptureSession(item_);
  session_.IsCursorCaptureEnabled(cursor);
  using winrt::Windows::Foundation::Metadata::ApiInformation;
  if (ApiInformation::IsPropertyPresent(L"Windows.Graphics.Capture.GraphicsCaptureSession", L"IsBorderRequired")) {
    try {
      wgc::GraphicsCaptureAccess::RequestAccessAsync(wgc::GraphicsCaptureAccessKind::Borderless).get();
      session_.IsBorderRequired(false);
    } catch (...) {
    }
  }
  token_ = pool_.FrameArrived({this, &ScreenCapture::onFrame});
  session_.StartCapture();
}

ScreenCapture::~ScreenCapture() {
  if (pool_) pool_.FrameArrived(token_);
  if (session_) session_.Close();
  if (pool_) pool_.Close();
  std::lock_guard lk(mu_);  // wait for an in-flight onFrame
}

void ScreenCapture::onFrame(const wgc::Direct3D11CaptureFramePool& pool, const winrt::Windows::Foundation::IInspectable&) {
  auto frame = pool.TryGetNextFrame();
  if (!frame) return;
  const auto cs = frame.ContentSize();
  if (cs.Width != width_ || cs.Height != height_) {
    sizeChanged_ = true;
    return;
  }
  auto access = frame.Surface().as<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
  winrt::com_ptr<ID3D11Texture2D> tex;
  if (FAILED(access->GetInterface(IID_PPV_ARGS(tex.put())))) return;
  std::lock_guard lk(mu_);
  g_.context->CopyResource(latest_.get(), tex.get());
  hasFrame_ = true;
}

bool ScreenCapture::convertLatest(FrameScaler& scaler, ID3D11Texture2D* nv12) {
  std::lock_guard lk(mu_);
  if (!hasFrame_) return false;
  scaler.convert(latest_.get(), nv12);
  return true;
}

}  // namespace clipper
