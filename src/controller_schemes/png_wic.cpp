// SPDX-License-Identifier: GPL-2.0-only
#include "controller_schemes/schemes.h"

#ifdef _WIN32
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <array>
#include <fstream>
#include <vector>

namespace rb_blitz::controller_schemes {
bool DecodePngWicBytes(const uint8_t* bytes, size_t size, Image* image, std::string* why) {
  // WIC may expose an RGBA PNG as BGRA. Check IHDR first so RGB or indexed
  // files converted by WIC cannot silently pass the authoring contract.
  if (!bytes || size < 26 || size > 8 * 1024 * 1024 ||
      !std::equal(bytes, bytes + 8,
                  std::array<uint8_t, 8>{137, 80, 78, 71, 13, 10, 26, 10}.begin()) ||
      bytes[8] != 0 || bytes[9] != 0 || bytes[10] != 0 || bytes[11] != 13 ||
      bytes[12] != 'I' || bytes[13] != 'H' || bytes[14] != 'D' || bytes[15] != 'R' ||
      bytes[24] != 8 || bytes[25] != 6) {
    *why = "PNG must have an 8-bit RGBA IHDR";
    return false;
  }
  using Microsoft::WRL::ComPtr;
  HRESULT initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  struct CoCleanup { bool active; ~CoCleanup() { if (active) CoUninitialize(); } } cleanup{SUCCEEDED(initialized)};
  ComPtr<IWICImagingFactory> factory;
  ComPtr<IWICStream> stream;
  ComPtr<IWICBitmapDecoder> decoder;
  ComPtr<IWICBitmapFrameDecode> frame;
  ComPtr<IWICFormatConverter> converter;
  auto fail = [&](const char* message) {
    *why = message;
    return false;
  };
  if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                              IID_PPV_ARGS(&factory))) ||
      FAILED(factory->CreateStream(&stream)) ||
      FAILED(stream->InitializeFromMemory(const_cast<BYTE*>(bytes), static_cast<DWORD>(size))) ||
      FAILED(factory->CreateDecoderFromStream(stream.Get(), nullptr,
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

bool DecodePngWic(const std::filesystem::path& path, Image* image, std::string* why) {
  // A cold NTFS/antivirus path can make opening each source file expensive.
  // Reading it once also lets the packed-cache path use the identical decoder.
  std::ifstream input(path, std::ios::binary | std::ios::ate);
  if (!input) { *why = "cannot read PNG"; return false; }
  const auto length = input.tellg();
  if (length < 26 || length > 8 * 1024 * 1024) {
    *why = "PNG size outside 26 bytes..8 MB";
    return false;
  }
  std::vector<uint8_t> bytes(static_cast<size_t>(length));
  input.seekg(0);
  if (!input.read(reinterpret_cast<char*>(bytes.data()), length)) {
    *why = "cannot read PNG";
    return false;
  }
  return DecodePngWicBytes(bytes.data(), bytes.size(), image, why);
}
}  // namespace rb_blitz::controller_schemes
#else
namespace rb_blitz::controller_schemes {
bool DecodePngWic(const std::filesystem::path&, Image*, std::string* why) {
  *why = "WIC PNG decoder requires Windows";
  return false;
}
bool DecodePngWicBytes(const uint8_t*, size_t, Image*, std::string* why) {
  *why = "WIC PNG decoder requires Windows";
  return false;
}
}
#endif
