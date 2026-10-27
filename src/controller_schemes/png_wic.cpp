// SPDX-License-Identifier: GPL-2.0-only
#include "controller_schemes/schemes.h"

#ifdef _WIN32
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <array>
#include <fstream>

namespace rb_blitz::controller_schemes {
bool DecodePngWic(const std::filesystem::path& path, Image* image, std::string* why) {
  // WIC may expose an RGBA PNG as BGRA. Check IHDR first so RGB or indexed
  // files converted by WIC cannot silently pass the authoring contract.
  std::array<uint8_t, 26> ihdr{};
  std::ifstream input(path, std::ios::binary);
  if (!input.read(reinterpret_cast<char*>(ihdr.data()), ihdr.size()) ||
      !std::equal(ihdr.begin(), ihdr.begin() + 8,
                  std::array<uint8_t, 8>{137, 80, 78, 71, 13, 10, 26, 10}.begin()) ||
      ihdr[8] != 0 || ihdr[9] != 0 || ihdr[10] != 0 || ihdr[11] != 13 ||
      ihdr[12] != 'I' || ihdr[13] != 'H' || ihdr[14] != 'D' || ihdr[15] != 'R' ||
      ihdr[24] != 8 || ihdr[25] != 6) {
    *why = "PNG must have an 8-bit RGBA IHDR";
    return false;
  }
  using Microsoft::WRL::ComPtr;
  HRESULT initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  struct CoCleanup { bool active; ~CoCleanup() { if (active) CoUninitialize(); } } cleanup{SUCCEEDED(initialized)};
  ComPtr<IWICImagingFactory> factory;
  ComPtr<IWICBitmapDecoder> decoder;
  ComPtr<IWICBitmapFrameDecode> frame;
  ComPtr<IWICFormatConverter> converter;
  auto fail = [&](const char* message) {
    *why = message;
    return false;
  };
  if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                              IID_PPV_ARGS(&factory))) ||
      FAILED(factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
                                                WICDecodeMetadataCacheOnLoad, &decoder)) ||
      FAILED(decoder->GetFrame(0, &frame))) return fail("cannot decode PNG");
  GUID container{};
  if (FAILED(decoder->GetContainerFormat(&container)) || container != GUID_ContainerFormatPng)
    return fail("image is not PNG");
  UINT width = 0, height = 0;
  if (FAILED(frame->GetSize(&width, &height)) || !width || !height ||
      width > 2048 || height > 2048) return fail("PNG dimensions outside 1..2048");
  WICPixelFormatGUID format{};
  if (FAILED(frame->GetPixelFormat(&format)) ||
      (format != GUID_WICPixelFormat32bppRGBA && format != GUID_WICPixelFormat32bppBGRA))
    return fail("PNG must be 8-bit RGBA with alpha");
  if (FAILED(factory->CreateFormatConverter(&converter)) ||
      FAILED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA,
                                   WICBitmapDitherTypeNone, nullptr, 0,
                                   WICBitmapPaletteTypeCustom))) return fail("cannot convert PNG");
  image->width = width;
  image->height = height;
  image->rgba.resize(size_t(width) * height * 4);
  if (FAILED(converter->CopyPixels(nullptr, width * 4, UINT(image->rgba.size()),
                                   image->rgba.data()))) return fail("cannot read PNG pixels");
  return true;
}
}  // namespace rb_blitz::controller_schemes
#else
namespace rb_blitz::controller_schemes {
bool DecodePngWic(const std::filesystem::path&, Image*, std::string* why) {
  *why = "WIC PNG decoder requires Windows";
  return false;
}
}
#endif
