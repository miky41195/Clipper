#include "ui/ImageIO.h"
#include <windows.h>
#include <wincodec.h>
#include <winrt/base.h>

namespace clipper {
using winrt::com_ptr;

static com_ptr<IWICImagingFactory> wic() {
  return winrt::create_instance<IWICImagingFactory>(CLSID_WICImagingFactory);
}

bool writeImage(const std::filesystem::path& file, const uint8_t* bgra, int width, int height, int stride, ImageFormat fmt) {
  try {
    auto f = wic();
    com_ptr<IWICBitmap> bitmap;
    winrt::check_hresult(f->CreateBitmapFromMemory(UINT(width), UINT(height), GUID_WICPixelFormat32bppBGRA, UINT(stride),
                                                   UINT(stride * height), const_cast<BYTE*>(bgra), bitmap.put()));
    com_ptr<IWICStream> stream;
    winrt::check_hresult(f->CreateStream(stream.put()));
    winrt::check_hresult(stream->InitializeFromFilename(file.c_str(), GENERIC_WRITE));
    com_ptr<IWICBitmapEncoder> enc;
    winrt::check_hresult(
        f->CreateEncoder(fmt == ImageFormat::Png ? GUID_ContainerFormatPng : GUID_ContainerFormatJpeg, nullptr, enc.put()));
    winrt::check_hresult(enc->Initialize(stream.get(), WICBitmapEncoderNoCache));
    com_ptr<IWICBitmapFrameEncode> frame;
    com_ptr<IPropertyBag2> props;
    winrt::check_hresult(enc->CreateNewFrame(frame.put(), props.put()));
    if (fmt == ImageFormat::Jpeg && props) {
      PROPBAG2 opt{};
      opt.pstrName = const_cast<LPOLESTR>(L"ImageQuality");
      VARIANT v;
      VariantInit(&v);
      v.vt = VT_R4;
      v.fltVal = 0.88f;
      props->Write(1, &opt, &v);
    }
    winrt::check_hresult(frame->Initialize(props.get()));
    winrt::check_hresult(frame->SetSize(UINT(width), UINT(height)));
    WICPixelFormatGUID pf = GUID_WICPixelFormat32bppBGRA;
    winrt::check_hresult(frame->SetPixelFormat(&pf));
    winrt::check_hresult(frame->WriteSource(bitmap.get(), nullptr));  // converts to the negotiated format
    winrt::check_hresult(frame->Commit());
    winrt::check_hresult(enc->Commit());
    return true;
  } catch (...) {
    return false;
  }
}

bool readImage(const std::filesystem::path& file, std::vector<uint8_t>& bgra, int& width, int& height) {
  try {
    auto f = wic();
    com_ptr<IWICBitmapDecoder> dec;
    winrt::check_hresult(f->CreateDecoderFromFilename(file.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, dec.put()));
    com_ptr<IWICBitmapFrameDecode> frame;
    winrt::check_hresult(dec->GetFrame(0, frame.put()));
    com_ptr<IWICBitmapSource> conv;
    winrt::check_hresult(WICConvertBitmapSource(GUID_WICPixelFormat32bppBGRA, frame.get(), conv.put()));
    UINT w = 0, h = 0;
    conv->GetSize(&w, &h);
    bgra.resize(size_t(w) * h * 4);
    winrt::check_hresult(conv->CopyPixels(nullptr, w * 4, UINT(bgra.size()), bgra.data()));
    width = int(w);
    height = int(h);
    return true;
  } catch (...) {
    return false;
  }
}

}  // namespace clipper
