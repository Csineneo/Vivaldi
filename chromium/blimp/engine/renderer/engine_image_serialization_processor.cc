// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "blimp/engine/renderer/engine_image_serialization_processor.h"

#include <stddef.h>
#include <vector>

#include "base/logging.h"
#include "blimp/common/compositor/webp_decoder.h"
#include "content/public/renderer/render_frame.h"
#include "third_party/libwebp/webp/encode.h"
#include "third_party/skia/include/core/SkData.h"
#include "third_party/skia/include/core/SkPicture.h"
#include "third_party/skia/include/core/SkPixelSerializer.h"
#include "third_party/skia/include/core/SkUnPreMultiply.h"

namespace {
// TODO(nyquist): Make sure encoder does not serialize images more than once.
// See crbug.com/548434.
class WebPImageEncoder : public SkPixelSerializer {
 public:
  WebPImageEncoder() {}
  ~WebPImageEncoder() override{};

  bool onUseEncodedData(const void* data, size_t len) override {
    const unsigned char* cast_data = static_cast<const unsigned char*>(data);
    if (len < 14)
      return false;
    return !memcmp(cast_data, "RIFF", 4) && !memcmp(cast_data + 8, "WEBPVP", 6);
  }

  SkData* onEncode(const SkPixmap& pixmap) override {
    // Initialize an empty WebPConfig.
    WebPConfig config;
    if (!WebPConfigInit(&config))
      return nullptr;

    // Initialize an empty WebPPicture.
    WebPPicture picture;
    if (!WebPPictureInit(&picture))
      return nullptr;

    // Ensure width and height are valid dimensions.
    if (!pixmap.width() || pixmap.width() > WEBP_MAX_DIMENSION)
      return nullptr;
    picture.width = pixmap.width();
    if (!pixmap.height() || pixmap.height() > WEBP_MAX_DIMENSION)
      return nullptr;
    picture.height = pixmap.height();

    // Import picture from raw pixels.
    DCHECK(pixmap.alphaType() == kPremul_SkAlphaType);
    auto pixel_chars = static_cast<const unsigned char*>(pixmap.addr());
    if (!PlatformPictureImport(pixel_chars, &picture))
      return nullptr;

    // Create a buffer for where to store the output data.
    std::vector<unsigned char> data;
    picture.custom_ptr = &data;

    // Use our own WebPWriterFunction implementation.
    picture.writer = &WebPImageEncoder::WriteOutput;

    // Setup the configuration for the output WebP picture. This is currently
    // the same as the default configuration for WebP, but since any change in
    // the WebP defaults would invalidate all caches they are hard coded.
    config.quality = 75.0;  // between 0 (smallest file) and 100 (biggest).
    config.method = 4;  // quality/speed trade-off (0=fast, 6=slower-better).

    // Encode the picture using the given configuration.
    bool success = WebPEncode(&config, &picture);

    // Release the memory allocated by WebPPictureImport*(). This does not free
    // the memory used by the picture object itself.
    WebPPictureFree(&picture);

    if (!success)
      return nullptr;

    // Copy WebP data into SkData. |data| is allocated only on the stack, so
    // it is automatically deleted after this.
    return SkData::NewWithCopy(&data.front(), data.size());
  }

 private:
  // WebPWriterFunction implementation.
  static int WriteOutput(const uint8_t* data,
                         size_t size,
                         const WebPPicture* const picture) {
    std::vector<unsigned char>* dest =
        static_cast<std::vector<unsigned char>*>(picture->custom_ptr);
    dest->insert(dest->end(), data, data + size);
    return 1;
  }

  // For each pixel, un-premultiplies the alpha-channel for each of the RGB
  // channels. As an example, for a channel value that before multiplication was
  // 255, and after applying an alpha of 128, the premultiplied pixel would be
  // 128. The un-premultiply step uses the alpha-channel to get back to 255. The
  // alpha channel is kept unchanged.
  void UnPremultiply(const unsigned char* in_pixels,
                     unsigned char* out_pixels,
                     size_t pixel_count) {
    const SkUnPreMultiply::Scale* table = SkUnPreMultiply::GetScaleTable();
    for (; pixel_count-- > 0; in_pixels += 4) {
      unsigned char alpha = in_pixels[3];
      if (alpha == 255) {  // Full opacity, just blindly copy.
        *out_pixels++ = in_pixels[0];
        *out_pixels++ = in_pixels[1];
        *out_pixels++ = in_pixels[2];
        *out_pixels++ = alpha;
      } else {
        SkUnPreMultiply::Scale scale = table[alpha];
        *out_pixels++ = SkUnPreMultiply::ApplyScale(scale, in_pixels[0]);
        *out_pixels++ = SkUnPreMultiply::ApplyScale(scale, in_pixels[1]);
        *out_pixels++ = SkUnPreMultiply::ApplyScale(scale, in_pixels[2]);
        *out_pixels++ = alpha;
      }
    }
  }

  bool PlatformPictureImport(const unsigned char* pixels,
                             WebPPicture* picture) {
    // Need to unpremultiply each pixel, each pixel using 4 bytes (RGBA).
    size_t pixel_count = picture->height * picture->width;
    std::vector<unsigned char> unpremul_pixels(pixel_count * 4);
    UnPremultiply(pixels, unpremul_pixels.data(), pixel_count);

    // Each pixel uses 4 bytes (RGBA) which affects the stride per row.
    int row_stride = picture->width * 4;

    if (SK_B32_SHIFT)  // Android
      return WebPPictureImportRGBA(picture, unpremul_pixels.data(), row_stride);
    return WebPPictureImportBGRA(picture, unpremul_pixels.data(), row_stride);
  }
};

}  // namespace

namespace blimp {
namespace engine {

EngineImageSerializationProcessor::EngineImageSerializationProcessor(
    mojom::BlobChannelPtr blob_channel)
    : blob_channel_(std::move(blob_channel)) {
  DCHECK(blob_channel_);

  pixel_serializer_.reset(new WebPImageEncoder);

  // Dummy BlobChannel command.
  // TODO(nyquist): Remove this after integrating BlobChannel.
  blob_channel_->Push("foo");
}

EngineImageSerializationProcessor::~EngineImageSerializationProcessor() {}

SkPixelSerializer* EngineImageSerializationProcessor::GetPixelSerializer() {
  return pixel_serializer_.get();
}

SkPicture::InstallPixelRefProc
EngineImageSerializationProcessor::GetPixelDeserializer() {
  return nullptr;
}

}  // namespace engine
}  // namespace blimp
