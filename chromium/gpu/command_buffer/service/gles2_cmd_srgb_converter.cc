// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gpu/command_buffer/service/gles2_cmd_srgb_converter.h"

#include "gpu/command_buffer/service/texture_manager.h"
#include "ui/gl/gl_version_info.h"

namespace {

void CompileShader(GLuint shader, const char* shader_source) {
  glShaderSource(shader, 1, &shader_source, 0);
  glCompileShader(shader);
#ifndef NDEBUG
  GLint compile_status;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &compile_status);
  if (GL_TRUE != compile_status)
    DLOG(ERROR) << "CopyTexImage: shader compilation failure.";
#endif
}

}  // anonymous namespace

namespace gpu {
namespace gles2 {

SRGBConverter::SRGBConverter(
    const gles2::FeatureInfo* feature_info)
    : feature_info_(feature_info) {
}

SRGBConverter::~SRGBConverter() {}



void SRGBConverter::InitializeSRGBConverterProgram() {
  if (srgb_converter_program_) {
    return;
  }

  srgb_converter_program_ = glCreateProgram();

  // Compile the vertex shader
  const char* vs_source =
      "#version 150\n"
      "out vec2 v_texcoord;\n"
      "\n"
      "void main()\n"
      "{\n"
      "    const vec2 quad_positions[6] = vec2[6]\n"
      "    (\n"
      "        vec2(0.0f, 0.0f),\n"
      "        vec2(0.0f, 1.0f),\n"
      "        vec2(1.0f, 0.0f),\n"
      "\n"
      "        vec2(0.0f, 1.0f),\n"
      "        vec2(1.0f, 0.0f),\n"
      "        vec2(1.0f, 1.0f)\n"
      "    );\n"
      "\n"
      "    vec2 xy = vec2((quad_positions[gl_VertexID] * 2.0) - 1.0);\n"
      "    gl_Position = vec4(xy, 0.0, 1.0);\n"
      "    v_texcoord = quad_positions[gl_VertexID];\n"
      "}\n";
  GLuint vs = glCreateShader(GL_VERTEX_SHADER);
  CompileShader(vs, vs_source);
  glAttachShader(srgb_converter_program_, vs);
  glDeleteShader(vs);

  // Compile the fragment shader

  // Sampling texels from a srgb texture to a linear image, it will convert
  // the srgb color space to linear color space automatically as a part of
  // filtering. See the section <sRGB Texture Color Conversion> in GLES and
  // OpenGL spec. So during decoding, we don't need to use the equation to
  // explicitly decode srgb to linear in fragment shader.
  // Drawing to a srgb image, it will convert linear to srgb automatically.
  // See the section <sRGB Conversion> in GLES and OpenGL spec. So during
  // encoding, we don't need to use the equation to explicitly encode linear
  // to srgb in fragment shader.
  // As a result, we just use a simple fragment shader to do srgb conversion.
  const char* fs_source =
      "#version 150\n"
      "uniform sampler2D u_source_texture;\n"
      "in vec2 v_texcoord;\n"
      "out vec4 output_color;\n"
      "\n"
      "void main()\n"
      "{\n"
      "    vec4 c = texture(u_source_texture, v_texcoord);\n"
      "    output_color = c;\n"
      "}\n";

  GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
  CompileShader(fs, fs_source);
  glAttachShader(srgb_converter_program_, fs);
  glDeleteShader(fs);

  glLinkProgram(srgb_converter_program_);
#ifndef NDEBUG
  GLint linked = 0;
  glGetProgramiv(srgb_converter_program_, GL_LINK_STATUS, &linked);
  if (!linked) {
    DLOG(ERROR) << "BlitFramebuffer: program link failure.";
  }
#endif

  GLuint texture_uniform =
      glGetUniformLocation(srgb_converter_program_, "u_source_texture");
  glUseProgram(srgb_converter_program_);
  glUniform1i(texture_uniform, 0);
}

void SRGBConverter::InitializeSRGBConverter(
    const gles2::GLES2Decoder* decoder) {
  if (srgb_converter_initialized_) {
    return;
  }

  InitializeSRGBConverterProgram();

  glGenTextures(
      srgb_converter_textures_.size(), srgb_converter_textures_.data());
  glActiveTexture(GL_TEXTURE0);
  for (auto srgb_converter_texture : srgb_converter_textures_) {
    glBindTexture(GL_TEXTURE_2D, srgb_converter_texture);

    // Use linear, non-mipmapped sampling with the srgb converter texture
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  }

  glGenFramebuffersEXT(1, &srgb_decoder_fbo_);
  glGenFramebuffersEXT(1, &srgb_encoder_fbo_);

  glGenVertexArraysOES(1, &srgb_converter_vao_);

  decoder->RestoreTextureUnitBindings(0);
  decoder->RestoreActiveTexture();
  decoder->RestoreProgramBindings();

  srgb_converter_initialized_ = true;
}

void SRGBConverter::Destroy() {
  if (srgb_converter_initialized_) {
    glDeleteTextures(srgb_converter_textures_.size(),
                     srgb_converter_textures_.data());
    srgb_converter_textures_.fill(0);

    glDeleteFramebuffersEXT(1, &srgb_decoder_fbo_);
    srgb_decoder_fbo_ = 0;
    glDeleteFramebuffersEXT(1, &srgb_encoder_fbo_);
    srgb_encoder_fbo_ = 0;

    glDeleteVertexArraysOES(1, &srgb_converter_vao_);
    srgb_converter_vao_ = 0;

    glDeleteProgram(srgb_converter_program_);
    srgb_converter_program_ = 0;

    srgb_converter_initialized_ = false;
  }
}

void SRGBConverter::Blit(
    const gles2::GLES2Decoder* decoder,
    GLint srcX0,
    GLint srcY0,
    GLint srcX1,
    GLint srcY1,
    GLint dstX0,
    GLint dstY0,
    GLint dstX1,
    GLint dstY1,
    GLbitfield mask,
    GLenum filter,
    const gfx::Size& framebuffer_size,
    GLuint src_framebuffer,
    GLenum src_framebuffer_internal_format,
    GLenum src_framebuffer_format,
    GLenum src_framebuffer_type,
    GLuint dst_framebuffer,
    bool decode,
    bool encode,
    bool enable_scissor_test) {
  // This function blits srgb image in src fb to srgb image in dst fb.
  // The steps are:
  // 1) Copy and crop pixels from source srgb image to the 1st texture(srgb).
  // 2) Sampling from the 1st texture and drawing to the 2nd texture(linear).
  //    During this step, color space is converted from srgb to linear.
  // 3) Blit pixels from the 2nd texture to the 3rd texture(linear).
  // 4) Sampling from the 3rd texture and drawing to the dst image(srgb).
  //    During this step, color space is converted from linear to srgb.
  // If we need to blit from linear to srgb or vice versa, some steps will be
  // skipped.
  DCHECK(srgb_converter_initialized_);

  // Set the states
  glActiveTexture(GL_TEXTURE0);
  glDisable(GL_SCISSOR_TEST);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_STENCIL_TEST);
  glDisable(GL_CULL_FACE);
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  glDepthMask(GL_FALSE);
  glDisable(GL_BLEND);
  glDisable(GL_DITHER);

  // Copy the image from read buffer to the 1st texture(srgb).
  // TODO(yunchao) If the read buffer is a fbo texture, we can sample
  // directly from that texture. In this way, we can save gpu memory.
  GLuint width_read = 0, height_read = 0, xoffset = 0, yoffset = 0;
  if (decode) {
    glBindFramebufferEXT(GL_FRAMEBUFFER, src_framebuffer);
    glBindTexture(GL_TEXTURE_2D, srgb_converter_textures_[0]);

    // We should not copy pixels outside of the read framebuffer. If we read
    // these pixels, they would become in-bound during BlitFramebuffer. However,
    // Out-of-bounds pixels will be initialized to 0 in CopyTexSubImage.
    // But they should read as if the GL_CLAMP_TO_EDGE texture mapping mode
    // were applied during BlitFramebuffer when the filter is GL_LINEAR.
    GLuint x = srcX1 > srcX0 ? srcX0 : srcX1;
    GLuint y = srcY1 > srcY0 ? srcY0 : srcY1;
    width_read = srcX1 > srcX0 ? srcX1 - srcX0 : srcX0 - srcX1;
    height_read = srcY1 > srcY0 ? srcY1 - srcY0 : srcY0 - srcY1;
    gfx::Rect c(0, 0, framebuffer_size.width(), framebuffer_size.height());
    c.Intersect(gfx::Rect(x, y, width_read, height_read));
    xoffset = c.x() - x;
    yoffset = c.y() - y;
    glCopyTexImage2D(GL_TEXTURE_2D, 0, src_framebuffer_internal_format,
                     c.x(), c.y(), c.width(), c.height(), 0);

    // Make a temporary linear texture as the 2nd texture, where we
    // render the converted (srgb to linear) result to.
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);

    glBindTexture(GL_TEXTURE_2D, srgb_converter_textures_[1]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
                 c.width(), c.height(),
                 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glBindFramebufferEXT(GL_FRAMEBUFFER, srgb_decoder_fbo_);
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                              GL_TEXTURE_2D, srgb_converter_textures_[1], 0);

    // Sampling from the 1st texture(srgb) and drawing to the
    // 2nd texture(linear),
    glUseProgram(srgb_converter_program_);
    glViewport(0, 0, width_read, height_read);

    glBindTexture(GL_TEXTURE_2D, srgb_converter_textures_[0]);
    glBindVertexArrayOES(srgb_converter_vao_);

    glDrawArrays(GL_TRIANGLES, 0, 6);
  } else {
    // Set approriate read framebuffer if decoding is skipped.
    glBindFramebufferEXT(GL_READ_FRAMEBUFFER, src_framebuffer);
  }

  // Create the 3rd texture(linear) as encoder_fbo's draw buffer. But we can
  // reuse the 1st texture and re-allocate the image. Then Blit framebuffer
  // from the 2nd texture(linear) to the 3rd texture. Filtering is done
  // during bliting. Note that the src and dst coordinates may be reversed.
  GLuint width_draw = 0, height_draw = 0;
  if (encode) {
    glBindTexture(GL_TEXTURE_2D, srgb_converter_textures_[0]);

    width_draw = dstX1 > dstX0 ? dstX1 - dstX0 : dstX0 - dstX1;
    height_draw = dstY1 > dstY0 ? dstY1 - dstY0 : dstY0 - dstY1;
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    glTexImage2D(
        GL_TEXTURE_2D, 0, decode ? GL_RGBA : src_framebuffer_internal_format,
        width_draw, height_draw, 0,
        decode ? GL_RGBA : src_framebuffer_format,
        decode ? GL_UNSIGNED_BYTE : src_framebuffer_type,
        nullptr);

    glBindFramebufferEXT(GL_DRAW_FRAMEBUFFER, srgb_encoder_fbo_);
    glFramebufferTexture2DEXT(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                              GL_TEXTURE_2D, srgb_converter_textures_[0], 0);
  } else {
    // Set approriate draw framebuffer if encoding is skipped.
    glBindFramebufferEXT(GL_DRAW_FRAMEBUFFER, dst_framebuffer);

    if (enable_scissor_test) {
      glEnable(GL_SCISSOR_TEST);
    }
  }

  glBlitFramebuffer(
      decode ? (srcX0 < srcX1 ? 0 - xoffset : width_read - xoffset) : srcX0,
      decode ? (srcY0 < srcY1 ? 0 - yoffset : height_read - yoffset) : srcY0,
      decode ? (srcX0 < srcX1 ? width_read - xoffset : 0 - xoffset) : srcX1,
      decode ? (srcY0 < srcY1 ? height_read - yoffset : 0 - yoffset) : srcY1,
      encode ? (dstX0 < dstX1 ? 0 : width_draw) : dstX0,
      encode ? (dstY0 < dstY1 ? 0 : height_draw) : dstY0,
      encode ? (dstX0 < dstX1 ? width_draw : 0) : dstX1,
      encode ? (dstY0 < dstY1 ? height_draw : 0) : dstY1,
      mask, filter);

  // Sampling from the 3rd texture(linear) and drawing to the target srgb image.
  // During this step, color space is converted from linear to srgb. We should
  // set appropriate viewport to draw to the correct location in target FB.
  if (encode) {
    GLuint xstart = dstX0 < dstX1 ? dstX0 : dstX1;
    GLuint ystart = dstY0 < dstY1 ? dstY0 : dstY1;
    glBindFramebufferEXT(GL_DRAW_FRAMEBUFFER, dst_framebuffer);
    glUseProgram(srgb_converter_program_);
    glViewport(xstart, ystart, width_draw, height_draw);

    glBindTexture(GL_TEXTURE_2D, srgb_converter_textures_[0]);
    glBindVertexArrayOES(srgb_converter_vao_);

    if (enable_scissor_test) {
      glEnable(GL_SCISSOR_TEST);
    }

    glDrawArrays(GL_TRIANGLES, 0, 6);
  }

  // Restore state
  decoder->RestoreAllAttributes();
  decoder->RestoreTextureUnitBindings(0);
  decoder->RestoreActiveTexture();
  decoder->RestoreProgramBindings();
  decoder->RestoreBufferBindings();
  decoder->RestoreFramebufferBindings();
  decoder->RestoreGlobalState();
}

}  // namespace gles2.
}  // namespace gpu
