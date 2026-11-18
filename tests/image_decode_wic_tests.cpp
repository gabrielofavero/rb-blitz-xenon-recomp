// SPDX-License-Identifier: GPL-2.0-only
#include "check.h"
#include "platform/image_decode.h"
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <array>
#include <stdexcept>
using Microsoft::WRL::ComPtr;
using namespace rb_blitz::test;
namespace {
void Require(HRESULT result) { if (FAILED(result)) throw std::runtime_error("WIC fixture encoding failed"); }
std::vector<std::uint8_t> Encode(REFGUID format) {
  ComPtr<IWICImagingFactory> factory;
  Require(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)));
  ComPtr<IStream> stream;
  Require(CreateStreamOnHGlobal(nullptr, TRUE, &stream));
  ComPtr<IWICBitmapEncoder> encoder;
  Require(factory->CreateEncoder(format, nullptr, &encoder));
  Require(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache));
  ComPtr<IWICBitmapFrameEncode> frame;
  ComPtr<IPropertyBag2> properties;
  Require(encoder->CreateNewFrame(&frame, &properties));
  Require(frame->Initialize(properties.Get()));
  Require(frame->SetSize(3, 2));
  WICPixelFormatGUID pixels = GUID_WICPixelFormat24bppBGR;
  Require(frame->SetPixelFormat(&pixels));
  if (pixels != GUID_WICPixelFormat24bppBGR) throw std::runtime_error("unexpected fixture pixel format");
  std::array<BYTE, 18> rows{0,0,255, 0,255,0, 255,0,0, 255,255,255, 0,0,0, 0,0,255};
  Require(frame->WritePixels(2, 9, static_cast<UINT>(rows.size()), rows.data()));
  Require(frame->Commit());
  Require(encoder->Commit());
  STATSTG stat{};
  Require(stream->Stat(&stat, STATFLAG_NONAME));
  std::vector<std::uint8_t> bytes(static_cast<std::size_t>(stat.cbSize.QuadPart));
  LARGE_INTEGER beginning{};
  Require(stream->Seek(beginning, STREAM_SEEK_SET, nullptr));
  ULONG read = 0;
  Require(stream->Read(bytes.data(), static_cast<ULONG>(bytes.size()), &read));
  if (read != bytes.size()) throw std::runtime_error("truncated fixture stream");
  return bytes;
}
}
int main() {
  Require(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED));
  try {
    const auto png = Encode(GUID_ContainerFormatPng);
    const auto jpeg = Encode(GUID_ContainerFormatJpeg);
    const auto bitmap = Encode(GUID_ContainerFormatBmp);
    std::vector<std::uint8_t> rgba;
    std::uint32_t width = 0, height = 0;
    std::string error;
    BeginCase("WIC decodes real PNG and JPEG into bounded RGBA on an existing STA thread");
    for (const auto* bytes : {&png, &jpeg}) {
      CHECK_TRUE(rb_blitz::platform::DecodeImageRgba(bytes->data(), bytes->size(), 3, &rgba, &width, &height, &error));
      CHECK_EQ(width, 3u); CHECK_EQ(height, 2u); CHECK_EQ(rgba.size(), 24u); CHECK_TRUE(error.empty());
      for (std::size_t i = 3; i < rgba.size(); i += 4) CHECK_EQ(rgba[i], 255u);
      if (bytes == &png) { CHECK_EQ(rgba[0], 255u); CHECK_EQ(rgba[1], 0u); CHECK_EQ(rgba[2], 0u); }
    }
    BeginCase("invalid formats, malformed bodies and oversized dimensions preserve prior output");
    const auto saved = rgba;
    CHECK_FALSE(rb_blitz::platform::DecodeImageRgba(png.data(), png.size(), 2, &rgba, &width, &height, &error));
    CHECK_TRUE(error.find("limit") != std::string::npos);
    CHECK_TRUE(rgba == saved); CHECK_EQ(width, 3u); CHECK_EQ(height, 2u);
    CHECK_FALSE(rb_blitz::platform::DecodeImageRgba(bitmap.data(), bitmap.size(), 3, &rgba, &width, &height, &error));
    CHECK_TRUE(error.find("PNG or JPEG") != std::string::npos); CHECK_TRUE(rgba == saved);
    constexpr std::array<std::uint8_t, 5> junk{1,2,3,4,5};
    CHECK_FALSE(rb_blitz::platform::DecodeImageRgba(junk.data(), junk.size(), 3, &rgba, &width, &height, &error));
    CHECK_TRUE(rgba == saved);
    CHECK_FALSE(rb_blitz::platform::DecodeImageRgba(nullptr, 0, 3, &rgba, &width, &height, &error));
    CHECK_FALSE(rb_blitz::platform::DecodeImageRgba(png.data(), png.size(), 16385, &rgba, &width, &height, &error));
    CHECK_FALSE(rb_blitz::platform::DecodeImageRgba(png.data(), png.size(), 3, nullptr, &width, &height, &error));
    CHECK_TRUE(rgba == saved);
  } catch (const std::exception& error) { std::fprintf(stderr, "%s\n", error.what()); CoUninitialize(); return 1; }
  CoUninitialize();
  return Finish();
}
