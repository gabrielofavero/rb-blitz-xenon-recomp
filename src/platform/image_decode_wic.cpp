// SPDX-License-Identifier: GPL-2.0-only
#include "platform/image_decode.h"

#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <limits>
#include <utility>

namespace rb_blitz::platform {

bool DecodeImageRgba(const std::uint8_t* bytes, const std::size_t size,
                     const std::uint32_t max_dimension, std::vector<std::uint8_t>* rgba,
                     std::uint32_t* width, std::uint32_t* height, std::string* error) {
  if (!bytes || !rgba || !width || !height || !error || !size ||
      size > std::numeric_limits<DWORD>::max() || !max_dimension || max_dimension > 16384) {
    if (error) *error = "invalid image input";
    return false;
  }
  const HRESULT initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  struct Cleanup {
    bool active;
    ~Cleanup() { if (active) CoUninitialize(); }
  } cleanup{SUCCEEDED(initialized)};
  auto fail = [&](const char* message) { *error = message; return false; };
  if (FAILED(initialized) && initialized != RPC_E_CHANGED_MODE)
    return fail("cannot initialize image decoder");
  using Microsoft::WRL::ComPtr;
  ComPtr<IWICImagingFactory> factory;
  ComPtr<IWICStream> stream;
  ComPtr<IWICBitmapDecoder> decoder;
  ComPtr<IWICBitmapFrameDecode> frame;
  ComPtr<IWICFormatConverter> converter;
  if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                             IID_PPV_ARGS(&factory))) ||
      FAILED(factory->CreateStream(&stream)) ||
      FAILED(stream->InitializeFromMemory(const_cast<BYTE*>(bytes), static_cast<DWORD>(size))) ||
      FAILED(factory->CreateDecoderFromStream(stream.Get(), nullptr,
                                              WICDecodeMetadataCacheOnDemand, &decoder)))
    return fail("cannot decode image");
  GUID container{};
  if (FAILED(decoder->GetContainerFormat(&container)) ||
      (container != GUID_ContainerFormatPng && container != GUID_ContainerFormatJpeg))
    return fail("image must be PNG or JPEG");
  UINT decoded_width = 0, decoded_height = 0;
  if (FAILED(decoder->GetFrame(0, &frame)) ||
      FAILED(frame->GetSize(&decoded_width, &decoded_height)) ||
      !decoded_width || !decoded_height || decoded_width > max_dimension ||
      decoded_height > max_dimension)
    return fail("image dimensions exceed limit");
  if (FAILED(factory->CreateFormatConverter(&converter)) ||
      FAILED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA,
                                   WICBitmapDitherTypeNone, nullptr, 0,
                                   WICBitmapPaletteTypeCustom)))
    return fail("cannot convert image");
  std::vector<std::uint8_t> decoded(static_cast<std::size_t>(decoded_width) * decoded_height * 4);
  if (FAILED(converter->CopyPixels(nullptr, decoded_width * 4,
                                   static_cast<UINT>(decoded.size()), decoded.data())))
    return fail("cannot read image pixels");
  *rgba = std::move(decoded);
  *width = decoded_width;
  *height = decoded_height;
  error->clear();
  return true;
}

}  // namespace rb_blitz::platform
