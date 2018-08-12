// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/common/gpu/media/gpu_video_decode_accelerator_factory_impl.h"

#include "content/common/gpu/media/gpu_video_decode_accelerator.h"
#include "gpu/command_buffer/service/gpu_preferences.h"
#include "media/gpu/ipc/common/gpu_video_accelerator_util.h"

#if defined(OS_WIN)
#include "base/win/windows_version.h"
#include "content/common/gpu/media/dxva_video_decode_accelerator_win.h"
#elif defined(OS_MACOSX)
#include "content/common/gpu/media/vt_video_decode_accelerator_mac.h"
#elif defined(OS_CHROMEOS)
#if defined(USE_V4L2_CODEC)
#include "content/common/gpu/media/v4l2_device.h"
#include "content/common/gpu/media/v4l2_slice_video_decode_accelerator.h"
#include "content/common/gpu/media/v4l2_video_decode_accelerator.h"
#include "ui/gl/gl_surface_egl.h"
#endif
#if defined(ARCH_CPU_X86_FAMILY)
#include "content/common/gpu/media/vaapi_video_decode_accelerator.h"
#include "ui/gl/gl_implementation.h"
#endif
#elif defined(OS_ANDROID)
#include "content/common/gpu/media/android_video_decode_accelerator.h"
#endif

namespace content {

namespace {
static base::WeakPtr<gpu::gles2::GLES2Decoder> GetEmptyGLES2Decoder() {
  NOTREACHED() << "VDA requests a GLES2Decoder, but client did not provide it";
  return base::WeakPtr<gpu::gles2::GLES2Decoder>();
}
}

// static
scoped_ptr<GpuVideoDecodeAcceleratorFactoryImpl>
GpuVideoDecodeAcceleratorFactoryImpl::Create(
    const GetGLContextCallback& get_gl_context_cb,
    const MakeGLContextCurrentCallback& make_context_current_cb,
    const BindGLImageCallback& bind_image_cb) {
  return make_scoped_ptr(new GpuVideoDecodeAcceleratorFactoryImpl(
      get_gl_context_cb, make_context_current_cb, bind_image_cb,
      base::Bind(&GetEmptyGLES2Decoder)));
}

// static
scoped_ptr<GpuVideoDecodeAcceleratorFactoryImpl>
GpuVideoDecodeAcceleratorFactoryImpl::CreateWithGLES2Decoder(
    const GetGLContextCallback& get_gl_context_cb,
    const MakeGLContextCurrentCallback& make_context_current_cb,
    const BindGLImageCallback& bind_image_cb,
    const GetGLES2DecoderCallback& get_gles2_decoder_cb) {
  return make_scoped_ptr(new GpuVideoDecodeAcceleratorFactoryImpl(
      get_gl_context_cb, make_context_current_cb, bind_image_cb,
      get_gles2_decoder_cb));
}

// static
gpu::VideoDecodeAcceleratorCapabilities
GpuVideoDecodeAcceleratorFactoryImpl::GetDecoderCapabilities(
    const gpu::GpuPreferences& gpu_preferences) {
  media::VideoDecodeAccelerator::Capabilities capabilities;
  if (gpu_preferences.disable_accelerated_video_decode)
    return gpu::VideoDecodeAcceleratorCapabilities();

  // Query VDAs for their capabilities and construct a set of supported
  // profiles for current platform. This must be done in the same order as in
  // CreateVDA(), as we currently preserve additional capabilities (such as
  // resolutions supported) only for the first VDA supporting the given codec
  // profile (instead of calculating a superset).
  // TODO(posciak,henryhsu): improve this so that we choose a superset of
  // resolutions and other supported profile parameters.
#if defined(OS_WIN)
  capabilities.supported_profiles =
      DXVAVideoDecodeAccelerator::GetSupportedProfiles();
#elif defined(OS_CHROMEOS)
  media::VideoDecodeAccelerator::SupportedProfiles vda_profiles;
#if defined(USE_V4L2_CODEC)
  vda_profiles = V4L2VideoDecodeAccelerator::GetSupportedProfiles();
  media::GpuVideoAcceleratorUtil::InsertUniqueDecodeProfiles(
      vda_profiles, &capabilities.supported_profiles);
  vda_profiles = V4L2SliceVideoDecodeAccelerator::GetSupportedProfiles();
  media::GpuVideoAcceleratorUtil::InsertUniqueDecodeProfiles(
      vda_profiles, &capabilities.supported_profiles);
#endif
#if defined(ARCH_CPU_X86_FAMILY)
  vda_profiles = VaapiVideoDecodeAccelerator::GetSupportedProfiles();
  media::GpuVideoAcceleratorUtil::InsertUniqueDecodeProfiles(
      vda_profiles, &capabilities.supported_profiles);
#endif
#elif defined(OS_MACOSX)
  capabilities.supported_profiles =
      VTVideoDecodeAccelerator::GetSupportedProfiles();
#elif defined(OS_ANDROID)
  capabilities =
      AndroidVideoDecodeAccelerator::GetCapabilities(gpu_preferences);
#endif
  return media::GpuVideoAcceleratorUtil::ConvertMediaToGpuDecodeCapabilities(
      capabilities);
}

scoped_ptr<media::VideoDecodeAccelerator>
GpuVideoDecodeAcceleratorFactoryImpl::CreateVDA(
    media::VideoDecodeAccelerator::Client* client,
    const media::VideoDecodeAccelerator::Config& config,
    const gpu::GpuPreferences& gpu_preferences) {
  DCHECK(thread_checker_.CalledOnValidThread());

  if (gpu_preferences.disable_accelerated_video_decode)
    return nullptr;

  // Array of Create..VDA() function pointers, potentially usable on current
  // platform. This list is ordered by priority, from most to least preferred,
  // if applicable. This list must be in the same order as the querying order
  // in GetDecoderCapabilities() above.
  using CreateVDAFp = scoped_ptr<media::VideoDecodeAccelerator> (
      GpuVideoDecodeAcceleratorFactoryImpl::*)(const gpu::GpuPreferences&)
      const;
  const CreateVDAFp create_vda_fps[] = {
#if defined(OS_WIN)
    &GpuVideoDecodeAcceleratorFactoryImpl::CreateDXVAVDA,
#endif
#if defined(OS_CHROMEOS) && defined(USE_V4L2_CODEC)
    &GpuVideoDecodeAcceleratorFactoryImpl::CreateV4L2VDA,
    &GpuVideoDecodeAcceleratorFactoryImpl::CreateV4L2SVDA,
#endif
#if defined(OS_CHROMEOS) && defined(ARCH_CPU_X86_FAMILY)
    &GpuVideoDecodeAcceleratorFactoryImpl::CreateVaapiVDA,
#endif
#if defined(OS_MACOSX)
    &GpuVideoDecodeAcceleratorFactoryImpl::CreateVTVDA,
#endif
#if defined(OS_ANDROID)
    &GpuVideoDecodeAcceleratorFactoryImpl::CreateAndroidVDA,
#endif
  };

  scoped_ptr<media::VideoDecodeAccelerator> vda;

  for (const auto& create_vda_function : create_vda_fps) {
    vda = (this->*create_vda_function)(gpu_preferences);
    if (vda && vda->Initialize(config, client))
      return vda;
  }

  return nullptr;
}

#if defined(OS_WIN)
scoped_ptr<media::VideoDecodeAccelerator>
GpuVideoDecodeAcceleratorFactoryImpl::CreateDXVAVDA(
    const gpu::GpuPreferences& gpu_preferences) const {
  scoped_ptr<media::VideoDecodeAccelerator> decoder;
  if (base::win::GetVersion() >= base::win::VERSION_WIN7) {
    DVLOG(0) << "Initializing DXVA HW decoder for windows.";
    decoder.reset(new DXVAVideoDecodeAccelerator(
        get_gl_context_cb_, make_context_current_cb_,
        gpu_preferences.enable_accelerated_vpx_decode));
  }
  return decoder;
}
#endif

#if defined(OS_CHROMEOS) && defined(USE_V4L2_CODEC)
scoped_ptr<media::VideoDecodeAccelerator>
GpuVideoDecodeAcceleratorFactoryImpl::CreateV4L2VDA(
    const gpu::GpuPreferences& gpu_preferences) const {
  scoped_ptr<media::VideoDecodeAccelerator> decoder;
  scoped_refptr<V4L2Device> device = V4L2Device::Create(V4L2Device::kDecoder);
  if (device.get()) {
    decoder.reset(new V4L2VideoDecodeAccelerator(
        gfx::GLSurfaceEGL::GetHardwareDisplay(), get_gl_context_cb_,
        make_context_current_cb_, device));
  }
  return decoder;
}

scoped_ptr<media::VideoDecodeAccelerator>
GpuVideoDecodeAcceleratorFactoryImpl::CreateV4L2SVDA(
    const gpu::GpuPreferences& gpu_preferences) const {
  scoped_ptr<media::VideoDecodeAccelerator> decoder;
  scoped_refptr<V4L2Device> device = V4L2Device::Create(V4L2Device::kDecoder);
  if (device.get()) {
    decoder.reset(new V4L2SliceVideoDecodeAccelerator(
        device, gfx::GLSurfaceEGL::GetHardwareDisplay(), get_gl_context_cb_,
        make_context_current_cb_));
  }
  return decoder;
}
#endif

#if defined(OS_CHROMEOS) && defined(ARCH_CPU_X86_FAMILY)
scoped_ptr<media::VideoDecodeAccelerator>
GpuVideoDecodeAcceleratorFactoryImpl::CreateVaapiVDA(
    const gpu::GpuPreferences& gpu_preferences) const {
  scoped_ptr<media::VideoDecodeAccelerator> decoder;
  decoder.reset(new VaapiVideoDecodeAccelerator(make_context_current_cb_,
                                                bind_image_cb_));
  return decoder;
}
#endif

#if defined(OS_MACOSX)
scoped_ptr<media::VideoDecodeAccelerator>
GpuVideoDecodeAcceleratorFactoryImpl::CreateVTVDA(
    const gpu::GpuPreferences& gpu_preferences) const {
  scoped_ptr<media::VideoDecodeAccelerator> decoder;
  decoder.reset(
      new VTVideoDecodeAccelerator(make_context_current_cb_, bind_image_cb_));
  return decoder;
}
#endif

#if defined(OS_ANDROID)
scoped_ptr<media::VideoDecodeAccelerator>
GpuVideoDecodeAcceleratorFactoryImpl::CreateAndroidVDA(
    const gpu::GpuPreferences& gpu_preferences) const {
  scoped_ptr<media::VideoDecodeAccelerator> decoder;
  decoder.reset(new AndroidVideoDecodeAccelerator(make_context_current_cb_,
                                                  get_gles2_decoder_cb_));
  return decoder;
}
#endif

GpuVideoDecodeAcceleratorFactoryImpl::GpuVideoDecodeAcceleratorFactoryImpl(
    const GetGLContextCallback& get_gl_context_cb,
    const MakeGLContextCurrentCallback& make_context_current_cb,
    const BindGLImageCallback& bind_image_cb,
    const GetGLES2DecoderCallback& get_gles2_decoder_cb)
    : get_gl_context_cb_(get_gl_context_cb),
      make_context_current_cb_(make_context_current_cb),
      bind_image_cb_(bind_image_cb),
      get_gles2_decoder_cb_(get_gles2_decoder_cb) {}

GpuVideoDecodeAcceleratorFactoryImpl::~GpuVideoDecodeAcceleratorFactoryImpl() {}

}  // namespace content
