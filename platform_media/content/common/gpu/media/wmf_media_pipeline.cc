// -*- Mode: c++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
//
// Copyright (C) 2014 Opera Software ASA.  All rights reserved.
//
// This file is an original work developed by Opera Software ASA

#include "content/common/gpu/media/wmf_media_pipeline.h"

#include <Mferror.h>

#include <algorithm>
#include <string>

#include "base/callback_helpers.h"
#include "base/numerics/safe_conversions.h"
#include "base/task_runner_util.h"
#include "base/time/time.h"
#include "base/trace_event/trace_event.h"
#include "media/base/bind_to_current_loop.h"
#include "media/base/data_buffer.h"
#include "media/base/platform_mime_util.h"
#include "media/base/timestamp_constants.h"
#include "media/base/win/mf_initializer.h"
#include "media/base/win/mf_util.h"
#include "media/filters/platform_media_pipeline_constants.h"
#include "ui/gl/gl_context.h"
#include "ui/gl/gl_surface_egl.h"

namespace content {

namespace {

const int kMicrosecondsPerSecond = 1000000;
const int kHundredsOfNanosecondsPerSecond = 10000000;

class AutoPropVariant {
 public:
  AutoPropVariant();
  ~AutoPropVariant();

  PROPVARIANT* get() { return &var; }
  PROPVARIANT& get_ref() { return var; }

  HRESULT ToInt64(LONGLONG* ret);
  HRESULT ToInt32(int* ret);

 private:
  PROPVARIANT var;
};  // class AutoPropVariant

class SourceReaderCallback : public IMFSourceReaderCallback {
 public:
  using OnReadSampleCB =
      base::Callback<void(media::MediaDataStatus status,
                          DWORD stream_index,
                          const base::win::ScopedComPtr<IMFSample>& sample)>;

  explicit SourceReaderCallback(const OnReadSampleCB& on_read_sample_cb);

  // IUnknown methods
  STDMETHODIMP QueryInterface(REFIID iid, void** ppv) override;
  STDMETHODIMP_(ULONG) AddRef() override;
  STDMETHODIMP_(ULONG) Release() override;

  // IMFSourceReaderCallback methods
  STDMETHODIMP OnReadSample(HRESULT status,
                            DWORD stream_index,
                            DWORD stream_flags,
                            LONGLONG timestamp_hns,
                            IMFSample* unwrapped_sample) override;
  STDMETHODIMP OnEvent(DWORD, IMFMediaEvent*) override;
  STDMETHODIMP OnFlush(DWORD) override;

 private:
  // Destructor is private. Caller should call Release.
  virtual ~SourceReaderCallback() {}

  OnReadSampleCB on_read_sample_cb_;
  LONG reference_count_;

  DISALLOW_COPY_AND_ASSIGN(SourceReaderCallback);
};  // class SourceReaderCallback

AutoPropVariant::AutoPropVariant() {
  PropVariantInit(&var);
}

AutoPropVariant::~AutoPropVariant() {
  PropVariantClear(&var);
}

HRESULT AutoPropVariant::ToInt64(LONGLONG* ret) {
  return PropVariantToInt64(var, ret);
}

HRESULT AutoPropVariant::ToInt32(int* ret) {
  return PropVariantToInt32(var, ret);
}

SourceReaderCallback::SourceReaderCallback(
    const OnReadSampleCB& on_read_sample_cb)
    : on_read_sample_cb_(on_read_sample_cb), reference_count_(1) {
  DCHECK(!on_read_sample_cb.is_null());
}

STDMETHODIMP SourceReaderCallback::QueryInterface(REFIID iid, void** ppv) {
  static const QITAB qit[] = {
      QITABENT(SourceReaderCallback, IMFSourceReaderCallback), {0},
  };
  return QISearch(this, qit, iid, ppv);
}

STDMETHODIMP_(ULONG) SourceReaderCallback::AddRef() {
  return InterlockedIncrement(&reference_count_);
}

STDMETHODIMP_(ULONG) SourceReaderCallback::Release() {
  ULONG uCount = InterlockedDecrement(&reference_count_);
  if (uCount == 0) {
    delete this;
  }
  return uCount;
}

STDMETHODIMP SourceReaderCallback::OnReadSample(HRESULT status,
                                                DWORD stream_index,
                                                DWORD stream_flags,
                                                LONGLONG timestamp_hns,
                                                IMFSample* unwrapped_sample) {
  base::win::ScopedComPtr<IMFSample> sample(unwrapped_sample);

  if (FAILED(status)) {
    on_read_sample_cb_.Run(media::MediaDataStatus::kError, stream_index,
                           sample);
    return S_OK;
  }

  if (stream_flags & MF_SOURCE_READERF_ENDOFSTREAM) {
    on_read_sample_cb_.Run(media::MediaDataStatus::kEOS, stream_index, sample);
    return S_OK;
  }

  if (stream_flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED) {
    on_read_sample_cb_.Run(media::MediaDataStatus::kConfigChanged, stream_index,
                           sample);
    return S_OK;
  }

  if (!sample) {
    // NULL |sample| can occur when there is a gap in the stream what
    // is signalled by |MF_SOURCE_READERF_STREAMTICK| flag but from the sparse
    // documentation that can be found on the subject it seems to be used only
    // with "live sources" of AV data (like cameras?), so we should be safe to
    // ignore it
    DCHECK(!(stream_flags & MF_SOURCE_READERF_STREAMTICK));
    on_read_sample_cb_.Run(media::MediaDataStatus::kError, stream_index,
                           sample);
    return E_ABORT;
  }

  on_read_sample_cb_.Run(media::MediaDataStatus::kOk, stream_index, sample);
  return S_OK;
}

STDMETHODIMP SourceReaderCallback::OnEvent(DWORD, IMFMediaEvent*) {
  return S_OK;
}

STDMETHODIMP SourceReaderCallback::OnFlush(DWORD) {
  return S_OK;
}

// Helper function that counts how many bits are set in the input number.
int NumberOfSetBits(uint32_t i) {
  int number_of_set_bits = 0;
  while (i > 0) {
    if (i & 1) {
      number_of_set_bits++;
    }
    i = i >> 1;
  }
  return number_of_set_bits;
}

}  // namespace

class WMFMediaPipeline::AudioTimestampCalculator {
 public:
  AudioTimestampCalculator();
  ~AudioTimestampCalculator() {}

  void SetChannelCount(int channel_count);
  void SetBytesPerSample(int bytes_per_sample);
  void SetSamplesPerSecond(int samples_per_second);
  void RecapturePosition();

  int64_t GetFramesCount(int64_t data_size);
  base::TimeDelta GetTimestamp(int64_t timestamp_hns, bool discontinuity);
  base::TimeDelta GetDuration(int64_t frames_count);
  void UpdateFrameCounter(int64_t frames_count);

 private:
  int channel_count_;
  int bytes_per_sample_;
  int samples_per_second_;
  int64_t frame_sum_;
  int64_t frame_offset_;
  bool must_recapture_position_;
};  // class WMFMediaPipeline::AudioTimestampCalculator

struct WMFMediaPipeline::Direct3DContext {
 public:
  Direct3DContext() : dev_manager_reset_token(0) {}

  bool Initialize();

  base::win::ScopedComPtr<IDirect3D9Ex> d3d9;
  base::win::ScopedComPtr<IDirect3DDevice9Ex> device;
  base::win::ScopedComPtr<IDirect3DDeviceManager9> device_manager;
  base::win::ScopedComPtr<IDirect3DQuery9> query;
  uint32_t dev_manager_reset_token;
};  // class WMFMediaPipeline::Direct3DContext

class WMFMediaPipeline::DXVAPictureBuffer {
 public:
  ~DXVAPictureBuffer();
  static std::unique_ptr<DXVAPictureBuffer> Create(
      uint32_t texture_id,
      gfx::Size texture_size,
      EGLConfig EGL_config,
      IDirect3DDevice9Ex* direct3d_device);
  bool Fill(const Direct3DContext& direct3d_context,
            IDirect3DSurface9* source_surface);
  void Reuse();

 private:
  DXVAPictureBuffer(uint32_t texture_id,
                    gfx::Size texture_size,
                    EGLConfig EGL_config)
      : texture_id_(texture_id),
        texture_size_(texture_size),
        decoding_surface_(NULL),
        use_rgb_(true) {}

  static const int kMaxIterationsForD3DFlush{10};

  uint32_t texture_id_;
  gfx::Size texture_size_;
  EGLSurface decoding_surface_;
  bool use_rgb_;
  base::win::ScopedComPtr<IDirect3DTexture9> decoding_texture_;
};  // class WMFMediaPipeline::DXVAPictureBuffer

WMFMediaPipeline::AudioTimestampCalculator::AudioTimestampCalculator()
    : channel_count_(0),
      bytes_per_sample_(0),
      samples_per_second_(0),
      frame_sum_(0),
      frame_offset_(0),
      must_recapture_position_(false) {
}

void WMFMediaPipeline::AudioTimestampCalculator::SetChannelCount(
    int channel_count) {
  channel_count_ = channel_count;
}

void WMFMediaPipeline::AudioTimestampCalculator::SetBytesPerSample(
    int bytes_per_sample) {
  bytes_per_sample_ = bytes_per_sample;
}

void WMFMediaPipeline::AudioTimestampCalculator::SetSamplesPerSecond(
    int samples_per_second) {
  samples_per_second_ = samples_per_second;
}

void WMFMediaPipeline::AudioTimestampCalculator::RecapturePosition() {
  must_recapture_position_ = true;
}

int64_t WMFMediaPipeline::AudioTimestampCalculator::GetFramesCount(
    int64_t data_size) {
  return data_size / bytes_per_sample_ / channel_count_;
}

base::TimeDelta WMFMediaPipeline::AudioTimestampCalculator::GetTimestamp(
    int64_t timestamp_hns,
    bool discontinuity) {
  // If this sample block comes after a discontinuity (i.e. a gap or seek)
  // reset the frame counters, and capture the timestamp. Future timestamps
  // will be offset from this block's timestamp.
  if (must_recapture_position_ || discontinuity != 0) {
    frame_sum_ = 0;
    frame_offset_ =
        timestamp_hns * samples_per_second_ / kHundredsOfNanosecondsPerSecond;
    must_recapture_position_ = false;
  }
  return base::TimeDelta::FromMicroseconds((frame_offset_ + frame_sum_) *
                                           kMicrosecondsPerSecond /
                                           samples_per_second_);
}

base::TimeDelta WMFMediaPipeline::AudioTimestampCalculator::GetDuration(
    int64_t frames_count) {
  return base::TimeDelta::FromMicroseconds(
      frames_count * kMicrosecondsPerSecond / samples_per_second_);
}

void WMFMediaPipeline::AudioTimestampCalculator::UpdateFrameCounter(
    int64_t frames_count) {
  frame_sum_ += frames_count;
}

WMFMediaPipeline::DXVAPictureBuffer::~DXVAPictureBuffer() {
  if (decoding_surface_) {
    EGLDisplay egl_display = gl::GLSurfaceEGL::GetHardwareDisplay();

    eglReleaseTexImage(egl_display, decoding_surface_, EGL_BACK_BUFFER);
    eglDestroySurface(egl_display, decoding_surface_);
  }
}

std::unique_ptr<WMFMediaPipeline::DXVAPictureBuffer>
WMFMediaPipeline::DXVAPictureBuffer::Create(
    uint32_t texture_id,
    gfx::Size texture_size,
    EGLConfig egl_config,
    IDirect3DDevice9Ex* direct3d_device) {
  if (!gl::GLContext::GetCurrent())
    return nullptr;

  std::unique_ptr<DXVAPictureBuffer> dxva_picture_buffer(
      new DXVAPictureBuffer(texture_id, texture_size, egl_config));

  EGLDisplay egl_display = gl::GLSurfaceEGL::GetHardwareDisplay();

  EGLint use_rgb = 1;
  eglGetConfigAttrib(egl_display, egl_config, EGL_BIND_TO_TEXTURE_RGB,
                     &use_rgb);

  EGLint attrib_list[] = {EGL_WIDTH,
                          texture_size.width(),
                          EGL_HEIGHT,
                          texture_size.height(),
                          EGL_TEXTURE_FORMAT,
                          use_rgb ? EGL_TEXTURE_RGB : EGL_TEXTURE_RGBA,
                          EGL_TEXTURE_TARGET,
                          EGL_TEXTURE_2D,
                          EGL_NONE};

  dxva_picture_buffer->decoding_surface_ =
      eglCreatePbufferSurface(egl_display, egl_config, attrib_list);
  if (!dxva_picture_buffer->decoding_surface_) {
    DLOG(ERROR) << "Failed to create surface";
    return nullptr;
  }

  dxva_picture_buffer->use_rgb_ = use_rgb != 0;

  HANDLE share_handle = nullptr;
  EGLBoolean ret = eglQuerySurfacePointerANGLE(
      egl_display, dxva_picture_buffer->decoding_surface_,
      EGL_D3D_TEXTURE_2D_SHARE_HANDLE_ANGLE, &share_handle);

  if (!share_handle || ret != EGL_TRUE) {
    DLOG(ERROR) << "Failed to query ANGLE surface pointer";
    return nullptr;
  }

  HRESULT hr = direct3d_device->CreateTexture(
      dxva_picture_buffer->texture_size_.width(),
      dxva_picture_buffer->texture_size_.height(), 1, D3DUSAGE_RENDERTARGET,
      dxva_picture_buffer->use_rgb_ ? D3DFMT_X8R8G8B8 : D3DFMT_A8R8G8B8,
      D3DPOOL_DEFAULT, dxva_picture_buffer->decoding_texture_.Receive(),
      &share_handle);

  if (FAILED(hr)) {
    DLOG(ERROR) << "Failed to create texture";
    return nullptr;
  }

  return dxva_picture_buffer;
}

bool WMFMediaPipeline::DXVAPictureBuffer::Fill(
    const Direct3DContext& direct3d_context,
    IDirect3DSurface9* source_surface) {
  if (!gl::GLContext::GetCurrent())
    return false;

  D3DSURFACE_DESC surface_desc;
  HRESULT hr = source_surface->GetDesc(&surface_desc);
  if (FAILED(hr)) {
    DLOG(ERROR) << "Failed to get surface description";
    return false;
  }

  // TODO(ggacek): check if dimensions of source and destination texture match.
  hr = direct3d_context.d3d9->CheckDeviceFormatConversion(
      D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, surface_desc.Format,
      use_rgb_ ? D3DFMT_X8R8G8B8 : D3DFMT_A8R8G8B8);
  if (FAILED(hr)) {
    DLOG(ERROR) << "Device does not support format converision";
    return false;
  }

  GLint current_texture = 0;
  glGetIntegerv(GL_TEXTURE_BINDING_2D, &current_texture);

  glBindTexture(media::kPlatformMediaPipelineTextureTarget, texture_id_);

  glTexParameteri(media::kPlatformMediaPipelineTextureTarget,
                  GL_TEXTURE_MIN_FILTER, GL_NEAREST);

  base::win::ScopedComPtr<IDirect3DSurface9> d3d_surface;
  hr = decoding_texture_->GetSurfaceLevel(0, d3d_surface.Receive());
  if (FAILED(hr)) {
    DLOG(ERROR) << "Failed to get surface from texture";
    return false;
  }

  hr = direct3d_context.device->StretchRect(
      source_surface, NULL, d3d_surface.get(), NULL, D3DTEXF_NONE);
  if (FAILED(hr)) {
    DLOG(ERROR) << "Colorspace conversion via StretchRect failed";
    return false;
  }

  // Ideally, this should be done immediately before the draw call that uses
  // the texture. Flush it once here though.
  hr = direct3d_context.query->Issue(D3DISSUE_END);
  if (FAILED(hr)) {
    DLOG(ERROR) << "Failed to issue END";
    return false;
  }

  // The DXVA decoder has its own device which it uses for decoding. ANGLE
  // has its own device which we don't have access to.
  // The above code attempts to copy the decoded picture into a surface
  // which is owned by ANGLE. As there are multiple devices involved in
  // this, the StretchRect call above is not synchronous.
  // We attempt to flush the batched operations to ensure that the picture is
  // copied to the surface owned by ANGLE.
  // We need to do this in a loop and call flush multiple times.
  // We have seen the GetData call for flushing the command buffer fail to
  // return success occassionally on multi core machines, leading to an
  // infinite loop.
  // Workaround is to have an upper limit of 10 on the number of iterations to
  // wait for the Flush to finish.
  int iterations = 0;
  while (
      (direct3d_context.query->GetData(NULL, 0, D3DGETDATA_FLUSH) == S_FALSE) &&
      ++iterations < kMaxIterationsForD3DFlush) {
    Sleep(1);  // Poor-man's Yield().
  }

  EGLDisplay egl_display = gl::GLSurfaceEGL::GetHardwareDisplay();
  eglBindTexImage(egl_display, decoding_surface_, EGL_BACK_BUFFER);
  glTexParameteri(media::kPlatformMediaPipelineTextureTarget,
                  GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glBindTexture(media::kPlatformMediaPipelineTextureTarget, current_texture);

  return true;
}

void WMFMediaPipeline::DXVAPictureBuffer::Reuse() {
  DCHECK(decoding_surface_);
  DCHECK(gl::GLContext::GetCurrent());

  EGLDisplay egl_display = gl::GLSurfaceEGL::GetHardwareDisplay();
  eglReleaseTexImage(egl_display, decoding_surface_, EGL_BACK_BUFFER);
}

bool WMFMediaPipeline::Direct3DContext::Initialize() {
  HRESULT hr = Direct3DCreate9Ex(D3D_SDK_VERSION, d3d9.Receive());
  if (FAILED(hr)) {
    DLOG(ERROR) << "Direct3DCreate9Ex failed";
    return false;
  }

  D3DPRESENT_PARAMETERS present_params = {0};
  present_params.BackBufferWidth = 1;
  present_params.BackBufferHeight = 1;
  present_params.BackBufferFormat = D3DFMT_UNKNOWN;
  present_params.BackBufferCount = 1;
  present_params.SwapEffect = D3DSWAPEFFECT_DISCARD;
  present_params.hDeviceWindow = ::GetShellWindow();
  present_params.Windowed = TRUE;
  present_params.Flags = D3DPRESENTFLAG_VIDEO;

  hr = d3d9->CreateDeviceEx(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL,
                            ::GetShellWindow(),
                            D3DCREATE_FPU_PRESERVE | D3DCREATE_MULTITHREADED |
                                D3DCREATE_MIXED_VERTEXPROCESSING,
                            &present_params, NULL, device.Receive());
  if (FAILED(hr)) {
    DLOG(ERROR) << "Failed to create D3D device";
    return false;
  }

  hr = DXVA2CreateDirect3DDeviceManager9(&dev_manager_reset_token,
                                         device_manager.Receive());
  if (FAILED(hr)) {
    DLOG(ERROR) << "DXVA2CreateDirect3DDeviceManager9 failed";
    return false;
  }

  hr = device_manager->ResetDevice(device.get(), dev_manager_reset_token);
  if (FAILED(hr)) {
    DLOG(ERROR) << "Failed to reset device";
    return false;
  }

  hr = device->CreateQuery(D3DQUERYTYPE_EVENT, query.Receive());
  if (FAILED(hr)) {
    DLOG(ERROR) << "Failed to create D3D device query";
    return false;
  }
  // Ensure query API works.
  hr = query->Issue(D3DISSUE_END);
  if (FAILED(hr)) {
    DLOG(ERROR) << "Failed to issue END test query";
    return false;
  }

  return true;
}

struct WMFMediaPipeline::InitializationResult {
  InitializationResult()
      : source_reader_output_video_format(MFVideoFormat_YV12),
        video_decoding_mode(media::PlatformMediaDecodingMode::SOFTWARE) {}

  GUID source_reader_output_video_format;
  Direct3DContext direct3d_context;
  media::PlatformMediaDecodingMode video_decoding_mode;
  base::win::ScopedComPtr<IMFSourceReader> source_reader;
};

WMFMediaPipeline::WMFMediaPipeline(
    media::DataSource* data_source,
    const AudioConfigChangedCB& audio_config_changed_cb,
    const VideoConfigChangedCB& video_config_changed_cb,
    media::PlatformMediaDecodingMode preferred_video_decoding_mode,
    const MakeGLContextCurrentCB& make_gl_context_current_cb)
    : data_source_(data_source),
      audio_config_changed_cb_(audio_config_changed_cb),
      video_config_changed_cb_(video_config_changed_cb),
      source_reader_creation_thread_("source_reader_creation_thread"),
      input_video_subtype_guid_(GUID_NULL),
      audio_timestamp_calculator_(new AudioTimestampCalculator),
      source_reader_output_video_format_(MFVideoFormat_YV12),
      make_gl_context_current_cb_(make_gl_context_current_cb),
      egl_config_(preferred_video_decoding_mode ==
                          media::PlatformMediaDecodingMode::HARDWARE
                      ? GetEGLConfig(make_gl_context_current_cb)
                      : nullptr),
      current_dxva_picture_buffer_(nullptr),
      get_stride_function_(nullptr),
      weak_ptr_factory_(this) {
  DCHECK(!audio_config_changed_cb_.is_null());
  DCHECK(!video_config_changed_cb_.is_null());
  std::fill(stream_indices_,
            stream_indices_ + media::PLATFORM_MEDIA_DATA_TYPE_COUNT,
            static_cast<DWORD>(MF_SOURCE_READER_INVALID_STREAM_INDEX));
}

WMFMediaPipeline::~WMFMediaPipeline() {
  DCHECK(thread_checker_.CalledOnValidThread());
  if (byte_stream_)
    byte_stream_->Stop();
}

// static
EGLConfig WMFMediaPipeline::GetEGLConfig(
    const MakeGLContextCurrentCB& make_gl_context_current_cb) {
  DCHECK(!make_gl_context_current_cb.is_null());
  if (!make_gl_context_current_cb.Run())
    return nullptr;

  EGLDisplay egl_display = gl::GLSurfaceEGL::GetHardwareDisplay();

  EGLint config_attribs[] = {EGL_BUFFER_SIZE, 32,
                             EGL_RED_SIZE, 8,
                             EGL_GREEN_SIZE, 8,
                             EGL_BLUE_SIZE, 8,
                             EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
                             EGL_ALPHA_SIZE, 0,
                             EGL_NONE};

  EGLConfig egl_config = nullptr;
  EGLint num_configs;
  if (!eglChooseConfig(egl_display, config_attribs, &egl_config, 1,
                       &num_configs)) {
    return nullptr;
  }

  return egl_config;
}

// static
WMFMediaPipeline::InitializationResult WMFMediaPipeline::CreateSourceReader(
    const scoped_refptr<WMFByteStream>& byte_stream,
    const base::win::ScopedComPtr<IMFAttributes>& attributes,
    media::PlatformMediaDecodingMode preferred_decoding_mode) {
  DVLOG(1) << __FUNCTION__;
  DCHECK(attributes);

  if (preferred_decoding_mode == media::PlatformMediaDecodingMode::HARDWARE) {
    InitializationResult result;
    if (CreateDXVASourceReader(byte_stream, attributes, &result))
      return result;
  }

  // Fall back to SW SourceReader.
  InitializationResult result;
  const HRESULT hr = MFCreateSourceReaderFromByteStream(
      byte_stream.get(), attributes.get(), result.source_reader.Receive());
  if (FAILED(hr)) {
    DLOG(ERROR) << "Failed to create source reader.";
    // We use (result.source_reader != NULL) as status.
    result.source_reader.Release();
  }

  return result;
}

// static
bool WMFMediaPipeline::CreateDXVASourceReader(
    const scoped_refptr<WMFByteStream>& byte_stream,
    const base::win::ScopedComPtr<IMFAttributes>& attributes,
    InitializationResult* result) {
  DVLOG(1) << __FUNCTION__;
  DCHECK(attributes);

  if (!result->direct3d_context.Initialize())
    return false;

  base::win::ScopedComPtr<IMFAttributes> attributes_hw;
  HRESULT hr = MFCreateAttributes(attributes_hw.Receive(), 1);
  if (FAILED(hr)) {
    DLOG(ERROR) << "Failed to create source reader attributes.";
    return false;
  }

  hr = attributes->CopyAllItems(attributes_hw.get());
  if (FAILED(hr)) {
    DLOG(ERROR) << "Failed to create source reader attributes.";
    return false;
  }

  hr = attributes_hw->SetUnknown(MF_SOURCE_READER_D3D_MANAGER,
                                 result->direct3d_context.device_manager.get());
  if (FAILED(hr)) {
    DLOG(ERROR) << "Failed to set d3d device manager attribute.";
    return false;
  }

  hr = attributes_hw->SetUINT32(MF_SOURCE_READER_DISABLE_DXVA, FALSE);
  if (FAILED(hr)) {
    DLOG(ERROR) << "Failed to set DXVA attribute.";
    return false;
  }

  hr = MFCreateSourceReaderFromByteStream(
      byte_stream.get(), attributes_hw.get(), result->source_reader.Receive());
  if (FAILED(hr)) {
    DLOG(ERROR) << "Failed to create source reader with DXVA support.";
    return false;
  }

  result->video_decoding_mode = media::PlatformMediaDecodingMode::HARDWARE;

  // MSDN shyly mentions that it is only preferred format for DXVA
  // decoding but in reality setting other formats results in flawless
  // configuration but MF_E_INVALIDMEDIATYPE when reading samples.
  result->source_reader_output_video_format = MFVideoFormat_NV12;

  return true;
}

void WMFMediaPipeline::Initialize(const std::string& mime_type,
                                  const InitializeCB& initialize_cb) {
  DCHECK(thread_checker_.CalledOnValidThread());
  DCHECK(!source_reader_.get());
  DCHECK(data_source_);

  // For diagnostics, the attempted video decoding mode is at least as
  // interesting on failure as it is on success.
  video_config_.decoding_mode =
      egl_config_ ? media::PlatformMediaDecodingMode::HARDWARE
                  : media::PlatformMediaDecodingMode::SOFTWARE;

  if (!InitializeImpl(mime_type, initialize_cb)) {
    initialize_cb.Run(false, -1, media::PlatformMediaTimeInfo(),
                      media::PlatformAudioConfig(), video_config_);
  }
}

bool WMFMediaPipeline::InitializeImpl(const std::string& mime_type,
                                      const InitializeCB& initialize_cb) {
  DCHECK(thread_checker_.CalledOnValidThread());

  // We've already made this check in WebMediaPlayerImpl, but that's been in
  // a different process, so let's take its result with a grain of salt.
  const bool has_platform_support = media::IsPlatformMediaPipelineAvailable(
      media::PlatformMediaCheckType::FULL);

  get_stride_function_ = reinterpret_cast<decltype(get_stride_function_)>(
      media::GetFunctionFromLibrary("MFGetStrideForBitmapInfoHeader",
                                    "evr.dll"));

  if (!has_platform_support || !get_stride_function_) {
    DVLOG(1) << "Can't access required media libraries in the system";
    return false;
  }

  media::InitializeMediaFoundation();

  base::win::ScopedComPtr<IMFAttributes> source_reader_attributes;
  if (!CreateSourceReaderCallbackAndAttributes(&source_reader_attributes)) {
    DVLOG(1) << "Failed to create source reader attributes";
    return false;
  }

  byte_stream_ = new WMFByteStream(data_source_);
  if (FAILED(byte_stream_->Initialize(
          std::wstring(mime_type.begin(), mime_type.end()).c_str()))) {
    DVLOG(1) << "Failed to create byte stream.";
    return false;
  }

  // |byte_stream_| is created and destroyed on |media_pipeline_thread| and
  // uses WeakPtr to |this| in its OnReadData callback, so we need to run it on
  // the same thread. When SourceReader is created it spawns another thread to
  // read some data (using our |byte_stream_|) and blocks current thread. As
  // we want WMFByteStream::OnReadSample to run on |media_pipeline-thread| we
  // need to move SourceReader creation to separate thread to avoid deadlock.
  if (!source_reader_creation_thread_.Start())
    return false;

  return base::PostTaskAndReplyWithResult(
      source_reader_creation_thread_.task_runner().get(), FROM_HERE,
      base::Bind(&WMFMediaPipeline::CreateSourceReader, byte_stream_,
                 source_reader_attributes, video_config_.decoding_mode),
      base::Bind(&WMFMediaPipeline::FinalizeInitialization,
                 weak_ptr_factory_.GetWeakPtr(), initialize_cb));
}

void WMFMediaPipeline::FinalizeInitialization(
    const InitializeCB& initialize_cb,
    const InitializationResult& result) {
  DVLOG(1) << __FUNCTION__;
  DCHECK(thread_checker_.CalledOnValidThread());

  source_reader_creation_thread_.Stop();

  media::PlatformMediaTimeInfo time_info;
  int bitrate = 0;
  media::PlatformAudioConfig audio_config;

  // Store the decoding mode eventually attempted (takes HW->SW fallback into
  // account).
  video_config_.decoding_mode = result.video_decoding_mode;

  if (!result.source_reader) {
    initialize_cb.Run(false, bitrate, time_info, audio_config, video_config_);
    return;
  }

  source_reader_ = result.source_reader;
  direct3d_context_.reset(new Direct3DContext(result.direct3d_context));
  source_reader_output_video_format_ = result.source_reader_output_video_format;

  if (!RetrieveStreamIndices()) {
    DVLOG(1) << "Failed to find streams";
    initialize_cb.Run(false, bitrate, time_info, audio_config, video_config_);
    return;
  }

  if (!ConfigureSourceReader()) {
    initialize_cb.Run(false, bitrate, time_info, audio_config, video_config_);
    return;
  }

  time_info.duration = GetDuration();
  bitrate = GetBitrate(time_info.duration);

  if (HasMediaStream(media::PLATFORM_MEDIA_AUDIO)) {
    if (!GetAudioDecoderConfig(&audio_config)) {
      initialize_cb.Run(false, bitrate, time_info, audio_config, video_config_);
      return;
    }
  }

  if (HasMediaStream(media::PLATFORM_MEDIA_VIDEO)) {
    if (!GetVideoDecoderConfig(&video_config_)) {
      initialize_cb.Run(false, bitrate, time_info, audio_config, video_config_);
      return;
    }
  }

  initialize_cb.Run(true, bitrate, time_info, audio_config, video_config_);
}

void WMFMediaPipeline::ReadAudioData(const ReadDataCB& read_audio_data_cb) {
  DCHECK(thread_checker_.CalledOnValidThread());
  DCHECK(read_audio_data_cb_.is_null());
  DCHECK(source_reader_.get());

  // We might have some data ready to send.
  if (pending_decoded_data_[media::PLATFORM_MEDIA_AUDIO]) {
    read_audio_data_cb.Run(pending_decoded_data_[media::PLATFORM_MEDIA_AUDIO]);
    pending_decoded_data_[media::PLATFORM_MEDIA_AUDIO] = nullptr;
    return;
  }

  // Read the next sample using asynchronous mode.
  // http://msdn.microsoft.com/en-us/library/windows/desktop/gg583871(v=vs.85).aspx
  HRESULT hr =
      source_reader_->ReadSample(stream_indices_[media::PLATFORM_MEDIA_AUDIO],
                                 0, nullptr, nullptr, nullptr, nullptr);
  if (FAILED(hr)) {
    DLOG(ERROR) << "Failed to read audio sample";
    read_audio_data_cb.Run(nullptr);
    return;
  }

  read_audio_data_cb_ = read_audio_data_cb;
}

void WMFMediaPipeline::ReadVideoData(const ReadDataCB& read_video_data_cb,
                                     uint32_t texture_id) {
  DCHECK(thread_checker_.CalledOnValidThread());
  DCHECK(read_video_data_cb_.is_null());
  DCHECK(!current_dxva_picture_buffer_);

  // We might have some data ready to send.
  if (pending_decoded_data_[media::PLATFORM_MEDIA_VIDEO]) {
    read_video_data_cb.Run(pending_decoded_data_[media::PLATFORM_MEDIA_VIDEO]);
    pending_decoded_data_[media::PLATFORM_MEDIA_VIDEO] = nullptr;
    return;
  }

  if (video_config_.decoding_mode ==
      media::PlatformMediaDecodingMode::HARDWARE) {
    current_dxva_picture_buffer_ = GetDXVAPictureBuffer(texture_id);
    if (!current_dxva_picture_buffer_) {
      DLOG(ERROR) << "Failed to create DXVAPictureBuffer.";
      read_video_data_cb.Run(nullptr);
      return;
    }
  }

  DCHECK(source_reader_.get());

  // Read the next sample using asynchronous mode.
  // http://msdn.microsoft.com/en-us/library/windows/desktop/gg583871(v=vs.85).aspx
  HRESULT hr =
      source_reader_->ReadSample(stream_indices_[media::PLATFORM_MEDIA_VIDEO],
                                 0, nullptr, nullptr, nullptr, nullptr);
  if (FAILED(hr)) {
    DLOG(ERROR) << "Failed to read video sample";
    read_video_data_cb.Run(nullptr);
    return;
  }

  read_video_data_cb_ = read_video_data_cb;
}

void WMFMediaPipeline::OnReadSample(
    media::MediaDataStatus status,
    DWORD stream_index,
    const base::win::ScopedComPtr<IMFSample>& sample) {
  DCHECK(thread_checker_.CalledOnValidThread());

  PlatformMediaPipeline::ReadDataCB* read_data_cb = nullptr;
  media::PlatformMediaDataType media_type = media::PLATFORM_MEDIA_AUDIO;
  if (stream_index == stream_indices_[media::PLATFORM_MEDIA_AUDIO]) {
    read_data_cb = &read_audio_data_cb_;
  } else if (stream_index == stream_indices_[media::PLATFORM_MEDIA_VIDEO]) {
    media_type = media::PLATFORM_MEDIA_VIDEO;
    read_data_cb = &read_video_data_cb_;
  } else {
    NOTREACHED() << "Unknown stream type";
  }
  DCHECK(!(*read_data_cb).is_null());
  DCHECK(!pending_decoded_data_[media_type]);

  scoped_refptr<media::DataBuffer> data_buffer;
  switch (status) {
    case media::kOk:
      DCHECK(sample);
      data_buffer = CreateDataBuffer(sample.get(), media_type);
      break;

    case media::kEOS:
      data_buffer = media::DataBuffer::CreateEOSBuffer();
      break;

    case media::kError:
      break;

    case media::kConfigChanged: {
      // Chromium's pipeline does not want any decoded data when we report
      // that configuration has changed. We need to buffer the sample and
      // send it during next read operation.
      pending_decoded_data_[media_type] =
          CreateDataBuffer(sample.get(), media_type);

      if (media_type == media::PLATFORM_MEDIA_AUDIO) {
        media::PlatformAudioConfig audio_config;
        if (GetAudioDecoderConfig(&audio_config)) {
          read_audio_data_cb_.Reset();
          audio_config_changed_cb_.Run(audio_config);
          return;
        }

        DLOG(ERROR) << "Error while getting decoder audio configuration.";
        status = media::kError;
        break;
      } else if (media_type == media::PLATFORM_MEDIA_VIDEO) {
        media::PlatformVideoConfig video_config;
        if (GetVideoDecoderConfig(&video_config)) {
          current_dxva_picture_buffer_ = nullptr;
          read_video_data_cb_.Reset();
          video_config_changed_cb_.Run(video_config);
          return;
        }

        DLOG(ERROR) << "Error while getting decoder video configuration.";
        status = media::kError;
        break;
      }
      // Fallthrough.
    }

    default:
      NOTREACHED();
  }

  if (stream_index == stream_indices_[media::PLATFORM_MEDIA_VIDEO]) {
    current_dxva_picture_buffer_ = nullptr;
  }
  base::ResetAndReturn(read_data_cb).Run(data_buffer);
}

scoped_refptr<media::DataBuffer> WMFMediaPipeline::CreateDataBufferFromMemory(
    IMFSample* sample) {
  // Get a pointer to the IMFMediaBuffer in the sample.
  base::win::ScopedComPtr<IMFMediaBuffer> output_buffer;
  HRESULT hr = sample->ConvertToContiguousBuffer(output_buffer.Receive());
  if (FAILED(hr)) {
    DLOG(ERROR) << "Failed to get pointer to data in sample.";
    return nullptr;
  }

  // Get the actual data from the IMFMediaBuffer
  uint8_t* data = nullptr;
  DWORD data_size = 0;
  hr = output_buffer->Lock(&data, NULL, &data_size);
  if (FAILED(hr)) {
    DLOG(ERROR) << "Failed to lock buffer.";
    return nullptr;
  }
  scoped_refptr<media::DataBuffer> data_buffer =
      media::DataBuffer::CopyFrom(data, data_size);

  // Unlock the IMFMediaBuffer buffer.
  output_buffer->Unlock();

  return data_buffer;
}

scoped_refptr<media::DataBuffer> WMFMediaPipeline::CreateDataBufferFromTexture(
    IMFSample* sample) {
  DCHECK(current_dxva_picture_buffer_);
  DCHECK(direct3d_context_);

  if (!make_gl_context_current_cb_.Run())
    return nullptr;

  base::win::ScopedComPtr<IMFMediaBuffer> output_buffer;
  HRESULT hr = sample->GetBufferByIndex(0, output_buffer.Receive());
  if (FAILED(hr)) {
    DLOG(ERROR) << "Failed to get buffer from output sample.";
    return nullptr;
  }

  base::win::ScopedComPtr<IDirect3DSurface9> surface;
  hr = MFGetService(output_buffer.get(), MR_BUFFER_SERVICE,
                    IID_PPV_ARGS(surface.Receive()));
  if (FAILED(hr)) {
    DLOG(ERROR) << "Failed to get D3D surface from output sample.";
    return nullptr;
  }

  // TODO(ggacek): verify surface size matches texture in buffer.
  if (!current_dxva_picture_buffer_->Fill(*(direct3d_context_.get()),
                                          surface.get())) {
    return nullptr;
  }

  return scoped_refptr<media::DataBuffer>(new media::DataBuffer(0));
}

scoped_refptr<media::DataBuffer> WMFMediaPipeline::CreateDataBuffer(
    IMFSample* sample,
    media::PlatformMediaDataType media_type) {
  scoped_refptr<media::DataBuffer> data_buffer;
  if (media_type == media::PLATFORM_MEDIA_VIDEO &&
      video_config_.decoding_mode ==
          media::PlatformMediaDecodingMode::HARDWARE) {
    data_buffer = CreateDataBufferFromTexture(sample);
  } else {
    data_buffer = CreateDataBufferFromMemory(sample);
  }
  if (!data_buffer)
    return nullptr;

  int64_t timestamp_hns;  // timestamp in hundreds of nanoseconds
  HRESULT hr = sample->GetSampleTime(&timestamp_hns);
  if (FAILED(hr)) {
    timestamp_hns = 0;
  }

  int64_t duration_hns;  // duration in hundreds of nanoseconds
  hr = sample->GetSampleDuration(&duration_hns);
  if (FAILED(hr)) {
    duration_hns = 0;
  }

  UINT32 discontinuity;
  hr = sample->GetUINT32(MFSampleExtension_Discontinuity, &discontinuity);
  if (FAILED(hr)) {
    discontinuity = 0;
  }

  if (media_type == media::PLATFORM_MEDIA_AUDIO) {
    // We calculate the timestamp and the duration based on the number of
    // audio frames we've already played. We don't trust the timestamp
    // stored on the IMFSample, as sometimes it's wrong, possibly due to
    // buggy encoders?
    data_buffer->set_timestamp(audio_timestamp_calculator_->GetTimestamp(
        timestamp_hns, discontinuity != 0));
    int64_t frames_count = audio_timestamp_calculator_->GetFramesCount(
        static_cast<int64_t>(data_buffer->data_size()));
    data_buffer->set_duration(
        audio_timestamp_calculator_->GetDuration(frames_count));
    audio_timestamp_calculator_->UpdateFrameCounter(frames_count);
  } else if (media_type == media::PLATFORM_MEDIA_VIDEO) {
    data_buffer->set_timestamp(
        base::TimeDelta::FromMicroseconds(timestamp_hns / 10));
    data_buffer->set_duration(
        base::TimeDelta::FromMicroseconds(duration_hns / 10));
  }

  return data_buffer;
}

void WMFMediaPipeline::Seek(base::TimeDelta time, const SeekCB& seek_cb) {
  DCHECK(thread_checker_.CalledOnValidThread());

  AutoPropVariant position;
  // IMFSourceReader::SetCurrentPosition expects position in 100-nanosecond
  // units, so we have to multiply time in microseconds by 10.
  HRESULT hr =
      InitPropVariantFromInt64(time.InMicroseconds() * 10, position.get());
  if (FAILED(hr)) {
    seek_cb.Run(false);
    return;
  }

  audio_timestamp_calculator_->RecapturePosition();
  hr = source_reader_->SetCurrentPosition(GUID_NULL, position.get_ref());
  seek_cb.Run(SUCCEEDED(hr));
}

bool WMFMediaPipeline::HasMediaStream(media::PlatformMediaDataType type) const {
  DCHECK(thread_checker_.CalledOnValidThread());
  return stream_indices_[type] != MF_SOURCE_READER_INVALID_STREAM_INDEX;
}

void WMFMediaPipeline::SetNoMediaStream(media::PlatformMediaDataType type) {
  DCHECK(thread_checker_.CalledOnValidThread());
  stream_indices_[type] =
      static_cast<DWORD>(MF_SOURCE_READER_INVALID_STREAM_INDEX);
}

bool WMFMediaPipeline::GetAudioDecoderConfig(
    media::PlatformAudioConfig* audio_config) {
  DCHECK(thread_checker_.CalledOnValidThread());
  DCHECK(source_reader_.get());

  // In case of some audio streams SourceReader might not get everything
  // right just from examining the stream (i.e. during initialization), so some
  // of the values reported here might be wrong. In such case first sample
  // shall be decoded with |MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED| status,
  // what will allow us to get proper configuration.

  audio_config->format = media::kSampleFormatF32;

  base::win::ScopedComPtr<IMFMediaType> media_type;
  HRESULT hr = source_reader_->GetCurrentMediaType(
      stream_indices_[media::PLATFORM_MEDIA_AUDIO], media_type.Receive());
  if (FAILED(hr) || !media_type) {
    DLOG(ERROR) << "Failed to obtain audio media type.";
    return false;
  }

  audio_config->channel_count =
      MFGetAttributeUINT32(media_type.get(), MF_MT_AUDIO_NUM_CHANNELS, 0);
  if (audio_config->channel_count == 0) {
    audio_config->channel_count = NumberOfSetBits(
        MFGetAttributeUINT32(media_type.get(), MF_MT_AUDIO_CHANNEL_MASK, 0));
  }

  audio_timestamp_calculator_->SetChannelCount(audio_config->channel_count);

  audio_timestamp_calculator_->SetBytesPerSample(
      MFGetAttributeUINT32(media_type.get(), MF_MT_AUDIO_BITS_PER_SAMPLE, 16) /
      8);

  audio_config->samples_per_second =
      MFGetAttributeUINT32(media_type.get(), MF_MT_AUDIO_SAMPLES_PER_SECOND, 0);
  audio_timestamp_calculator_->SetSamplesPerSecond(
      audio_config->samples_per_second);

  return true;
}

bool WMFMediaPipeline::GetVideoDecoderConfig(
    media::PlatformVideoConfig* video_config) {
  DCHECK(thread_checker_.CalledOnValidThread());
  DCHECK(source_reader_.get());

  // In case of some video streams SourceReader might not get everything
  // right just from examining the stream (i.e. during initialization), so some
  // of the values reported here might be wrong. In such case first sample
  // shall be decoded with |MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED| status,
  // what will allow us to get proper configuration.

  base::win::ScopedComPtr<IMFMediaType> media_type;
  HRESULT hr = source_reader_->GetCurrentMediaType(
      stream_indices_[media::PLATFORM_MEDIA_VIDEO], media_type.Receive());
  if (FAILED(hr)) {
    DLOG(ERROR) << "Failed to obtain video media type.";
    return false;
  }

  uint32_t frame_width = 0;
  uint32_t frame_height = 0;
  hr = MFGetAttributeSize(media_type.get(), MF_MT_FRAME_SIZE, &frame_width,
                          &frame_height);
  if (FAILED(hr)) {
    DLOG(ERROR) << "Failed to obtain width and height.";
    return false;
  }

  video_config->coded_size = gfx::Size(frame_width, frame_height);

  // The visible rect and the natural size of the video frame have to be
  // calculated with consideration of the pan scan aperture, the display
  // aperture and the pixel aspec ratio. For more info see:
  // http://msdn.microsoft.com/en-us/library/windows/desktop/bb530115(v=vs.85).aspx

  MFVideoArea video_area;
  uint32_t pan_scan_enabled =
      MFGetAttributeUINT32(media_type.get(), MF_MT_PAN_SCAN_ENABLED, FALSE);
  if (pan_scan_enabled) {
    hr = media_type->GetBlob(MF_MT_PAN_SCAN_APERTURE,
                             reinterpret_cast<uint8_t*>(&video_area),
                             sizeof(MFVideoArea),
                             NULL);
    if (SUCCEEDED(hr)) {
      // MFOffset structure consists of the integer part and the fractional
      // part, but pixels are not divisible, so we ignore the fractional part.
      video_config->visible_rect = gfx::Rect(video_area.OffsetX.value,
                                             video_area.OffsetY.value,
                                             video_area.Area.cx,
                                             video_area.Area.cy);
    }
  }

  if (!pan_scan_enabled || FAILED(hr)) {
    hr = media_type->GetBlob(MF_MT_MINIMUM_DISPLAY_APERTURE,
                             reinterpret_cast<uint8_t*>(&video_area),
                             sizeof(MFVideoArea),
                             NULL);
    if (FAILED(hr)) {
      hr = media_type->GetBlob(MF_MT_GEOMETRIC_APERTURE,
                               reinterpret_cast<uint8_t*>(&video_area),
                               sizeof(MFVideoArea),
                               NULL);
    }

    if (SUCCEEDED(hr)) {
      // MFOffset structure consists of the integer part and the fractional
      // part, but pixels are not divisible, so we ignore the fractional part.
      video_config->visible_rect = gfx::Rect(video_area.OffsetX.value,
                                             video_area.OffsetY.value,
                                             video_area.Area.cx,
                                             video_area.Area.cy);
    } else {
      video_config->visible_rect = gfx::Rect(frame_width, frame_height);
    }
  }

  uint32_t aspect_numerator = 0;
  uint32_t aspect_denominator = 0;
  hr = MFGetAttributeRatio(media_type.get(), MF_MT_PIXEL_ASPECT_RATIO,
                           &aspect_numerator, &aspect_denominator);
  if (FAILED(hr)) {
    DLOG(ERROR) << "Failed to obtain pixel aspect ratio.";
    return false;
  }

  if (aspect_numerator == aspect_denominator) {
    video_config->natural_size = gfx::Size(frame_width, frame_height);
  } else if (aspect_numerator > aspect_denominator) {
    video_config->natural_size =
        gfx::Size(MulDiv(frame_width, aspect_numerator, aspect_denominator),
                  frame_height);
  } else {
    video_config->natural_size =
        gfx::Size(frame_width,
                  MulDiv(frame_height, aspect_denominator, aspect_numerator));
  }

  int stride = -1;
  if (!GetStride(&stride))
    return false;

  video_config->planes[media::VideoFrame::kYPlane].stride = stride;
  video_config->planes[media::VideoFrame::kVPlane].stride = stride / 2;
  video_config->planes[media::VideoFrame::kUPlane].stride = stride / 2;

  int rows = frame_height;

  // Y plane is first and is not downsampled.
  video_config->planes[media::VideoFrame::kYPlane].offset = 0;
  video_config->planes[media::VideoFrame::kYPlane].size =
      rows * video_config->planes[media::VideoFrame::kYPlane].stride;

  // In YV12 V and U planes are downsampled vertically and horizontally by 2.
  rows /= 2;

  // V plane preceeds U.
  video_config->planes[media::VideoFrame::kVPlane].offset =
      video_config->planes[media::VideoFrame::kYPlane].offset +
      video_config->planes[media::VideoFrame::kYPlane].size;
  video_config->planes[media::VideoFrame::kVPlane].size =
      rows * video_config->planes[media::VideoFrame::kVPlane].stride;

  video_config->planes[media::VideoFrame::kUPlane].offset =
      video_config->planes[media::VideoFrame::kVPlane].offset +
      video_config->planes[media::VideoFrame::kVPlane].size;
  video_config->planes[media::VideoFrame::kUPlane].size =
      rows * video_config->planes[media::VideoFrame::kUPlane].stride;

  switch (MFGetAttributeUINT32(media_type.get(), MF_MT_VIDEO_ROTATION,
                               MFVideoRotationFormat_0)) {
    case MFVideoRotationFormat_90:
      video_config->rotation = media::VIDEO_ROTATION_90;
      break;
    case MFVideoRotationFormat_180:
      video_config->rotation = media::VIDEO_ROTATION_180;
      break;
    case MFVideoRotationFormat_270:
      video_config->rotation = media::VIDEO_ROTATION_270;
      break;
    default:
      video_config->rotation = media::VIDEO_ROTATION_0;
      break;
  }

  // Set when SourceReader is created.
  video_config->decoding_mode = video_config_.decoding_mode;
  video_config_ = *video_config;
  return true;
}

bool WMFMediaPipeline::CreateSourceReaderCallbackAndAttributes(
    base::win::ScopedComPtr<IMFAttributes>* attributes) {
  DCHECK(thread_checker_.CalledOnValidThread());
  DCHECK(!source_reader_callback_.get());

  source_reader_callback_ =
      new SourceReaderCallback(media::BindToCurrentLoop(base::Bind(
          &WMFMediaPipeline::OnReadSample, weak_ptr_factory_.GetWeakPtr())));

  HRESULT hr = MFCreateAttributes((*attributes).Receive(), 1);
  if (FAILED(hr)) {
    source_reader_callback_.Release();
    return false;
  }

  hr = (*attributes)
           ->SetUnknown(MF_SOURCE_READER_ASYNC_CALLBACK,
                        source_reader_callback_.get());
  if (FAILED(hr))
    return false;

  return true;
}

bool WMFMediaPipeline::RetrieveStreamIndices() {
  DCHECK(thread_checker_.CalledOnValidThread());
  DCHECK(source_reader_.get());

  DWORD stream_index = 0;
  HRESULT hr = S_OK;

  while (!(HasMediaStream(media::PLATFORM_MEDIA_AUDIO) &&
           HasMediaStream(media::PLATFORM_MEDIA_VIDEO))) {
    base::win::ScopedComPtr<IMFMediaType> media_type;
    hr = source_reader_->GetNativeMediaType(
        stream_index, 0, media_type.Receive());

    if (hr == MF_E_INVALIDSTREAMNUMBER)
      break;  // No more streams.

    if (SUCCEEDED(hr)) {
      GUID major_type;
      hr = media_type->GetMajorType(&major_type);
      if (SUCCEEDED(hr)) {
        if (major_type == MFMediaType_Audio &&
            stream_indices_[media::PLATFORM_MEDIA_AUDIO] ==
                MF_SOURCE_READER_INVALID_STREAM_INDEX) {
          stream_indices_[media::PLATFORM_MEDIA_AUDIO] = stream_index;
        } else if (major_type == MFMediaType_Video &&
                   stream_indices_[media::PLATFORM_MEDIA_VIDEO] ==
                       MF_SOURCE_READER_INVALID_STREAM_INDEX) {
          stream_indices_[media::PLATFORM_MEDIA_VIDEO] = stream_index;
        }
      }
    }
    ++stream_index;
  }

  return HasMediaStream(media::PLATFORM_MEDIA_AUDIO) ||
         HasMediaStream(media::PLATFORM_MEDIA_VIDEO);
}

bool WMFMediaPipeline::ConfigureStream(DWORD stream_index) {
  DCHECK(thread_checker_.CalledOnValidThread());
  DCHECK(source_reader_.get());
  DCHECK(stream_index == stream_indices_[media::PLATFORM_MEDIA_AUDIO] ||
         stream_index == stream_indices_[media::PLATFORM_MEDIA_VIDEO]);
  bool is_video = stream_index == stream_indices_[media::PLATFORM_MEDIA_VIDEO];

  if (is_video) {
    base::win::ScopedComPtr<IMFMediaType> input_video_type;
    HRESULT hr = source_reader_->GetCurrentMediaType(
        stream_indices_[media::PLATFORM_MEDIA_VIDEO],
        input_video_type.Receive());
    if (FAILED(hr)) {
      DVLOG(1) << "Failed to obtain video media type. No video track?";
      return false;
    }
    input_video_type->GetGUID(MF_MT_SUBTYPE, &input_video_subtype_guid_);
  }

  base::win::ScopedComPtr<IMFMediaType> new_current_media_type;
  HRESULT hr = MFCreateMediaType(new_current_media_type.Receive());
  if (FAILED(hr)) {
    DLOG(ERROR) << "Failed to create media type.";
    return false;
  }

  hr = new_current_media_type->SetGUID(
      MF_MT_MAJOR_TYPE, is_video ? MFMediaType_Video : MFMediaType_Audio);
  if (FAILED(hr)) {
    DLOG(ERROR) << "Failed to set media major type.";
    return false;
  }

  hr = new_current_media_type->SetGUID(
      MF_MT_SUBTYPE,
      is_video ? source_reader_output_video_format_ : MFAudioFormat_Float);
  if (FAILED(hr)) {
    DLOG(ERROR) << "Failed to set media subtype.";
    return false;
  }

  hr = source_reader_->SetCurrentMediaType(
      stream_index, NULL, new_current_media_type.get());
  if (FAILED(hr)) {
    DVLOG(1) << "Failed to set media type. No "
             << (is_video ? "video" : "audio") << " track?";
    return false;
  }

  // When we set the media type without providing complete media information
  // WMF tries to figure it out on its own.  But it doesn't do it until it's
  // needed -- e.g., when decoding is requested.  Since this figuring-out
  // process can fail, let's force it now by calling GetCurrentMediaType().
  base::win::ScopedComPtr<IMFMediaType> media_type;
  hr = source_reader_->GetCurrentMediaType(stream_index, media_type.Receive());
  if (FAILED(hr)) {
    DLOG(ERROR) << "Failed to obtain media type.";
    return false;
  }

  return true;
}

bool WMFMediaPipeline::ConfigureSourceReader() {
  DCHECK(thread_checker_.CalledOnValidThread());
  DCHECK(source_reader_.get());

  static const media::PlatformMediaDataType media_types[] = {
      media::PLATFORM_MEDIA_AUDIO, media::PLATFORM_MEDIA_VIDEO};
  static_assert(arraysize(media_types) == media::PLATFORM_MEDIA_DATA_TYPE_COUNT,
                "Not all media types chosen to be configured.");

  bool status = false;
  for (const auto& media_type : media_types) {
    if (!ConfigureStream(stream_indices_[media_type])) {
      SetNoMediaStream(media_type);
    } else {
      DCHECK(HasMediaStream(media_type));
      status = true;
    }
  }

  return status;
}

base::TimeDelta WMFMediaPipeline::GetDuration() {
  DCHECK(thread_checker_.CalledOnValidThread());
  DCHECK(source_reader_.get());

  AutoPropVariant var;

  HRESULT hr = source_reader_->GetPresentationAttribute(
      static_cast<DWORD>(MF_SOURCE_READER_MEDIASOURCE),
      MF_PD_DURATION,
      var.get());
  if (FAILED(hr)) {
    DLOG_IF(WARNING, !data_source_->IsStreaming())
        << "Failed to obtain media duration.";
    return media::kInfiniteDuration;
  }

  int64_t duration_int64 = 0;
  hr = var.ToInt64(&duration_int64);
  if (FAILED(hr)) {
    DLOG(ERROR) << "Failed to obtain media duration.";
    return media::kInfiniteDuration;
  }
  // Have to divide duration64 by ten to convert from
  // hundreds of nanoseconds (WMF style) to microseconds.
  return base::TimeDelta::FromMicroseconds(duration_int64 / 10);
}

int WMFMediaPipeline::GetBitrate(base::TimeDelta duration) {
  DCHECK(thread_checker_.CalledOnValidThread());
  DCHECK(source_reader_.get());
  DCHECK_GT(duration.InMicroseconds(), 0);

  AutoPropVariant var;

  // Calculating the media bitrate
  HRESULT hr = source_reader_->GetPresentationAttribute(
      static_cast<DWORD>(MF_SOURCE_READER_MEDIASOURCE),
      MF_PD_AUDIO_ENCODING_BITRATE,
      var.get());
  int audio_bitrate = 0;
  hr = var.ToInt32(&audio_bitrate);
  if (FAILED(hr))
    audio_bitrate = 0;

  hr = source_reader_->GetPresentationAttribute(
      static_cast<DWORD>(MF_SOURCE_READER_MEDIASOURCE),
      MF_PD_VIDEO_ENCODING_BITRATE,
      var.get());
  int video_bitrate = 0;
  hr = var.ToInt32(&video_bitrate);
  if (FAILED(hr))
    video_bitrate = 0;

  const int bitrate = std::max(audio_bitrate + video_bitrate, 0);
  if (bitrate == 0 && !data_source_->IsStreaming()) {
    // If we have a valid bitrate we can use it, otherwise we have to calculate
    // it from file size and duration.
    hr = source_reader_->GetPresentationAttribute(
        static_cast<DWORD>(MF_SOURCE_READER_MEDIASOURCE), MF_PD_TOTAL_FILE_SIZE,
        var.get());
    if (SUCCEEDED(hr) && duration.InMicroseconds() > 0) {
      int64_t file_size_in_bytes;
      hr = var.ToInt64(&file_size_in_bytes);
      if (SUCCEEDED(hr))
        return (8000000.0 * file_size_in_bytes) / duration.InMicroseconds();
    }
    DLOG(ERROR) << "Failed to obtain media bitrate.";
  }

  return bitrate;
}

bool WMFMediaPipeline::GetStride(int* stride) {
  DCHECK(thread_checker_.CalledOnValidThread());
  DCHECK(source_reader_.get());

  base::win::ScopedComPtr<IMFMediaType> media_type;
  HRESULT hr = source_reader_->GetCurrentMediaType(
      stream_indices_[media::PLATFORM_MEDIA_VIDEO], media_type.Receive());
  if (FAILED(hr)) {
    DLOG(ERROR) << "Failed to obtain media type.";
    return false;
  }

  UINT32 width = 0;
  UINT32 height = 0;
  hr = MFGetAttributeSize(media_type.get(), MF_MT_FRAME_SIZE, &width, &height);
  if (FAILED(hr)) {
    DLOG(ERROR) << "Failed to obtain width and height.";
    return false;
  }

  LONG stride_long = 0;
  hr = get_stride_function_(source_reader_output_video_format_.Data1, width,
                            &stride_long);
  if (FAILED(hr)) {
    DLOG(ERROR) << "Failed to obtain stride.";
    return false;
  }

  *stride = base::saturated_cast<int>(stride_long);

  return true;
}

WMFMediaPipeline::DXVAPictureBuffer* WMFMediaPipeline::GetDXVAPictureBuffer(
    uint32_t texture_id) {
  DCHECK(video_config_.decoding_mode ==
         media::PlatformMediaDecodingMode::HARDWARE);
  DCHECK(direct3d_context_);
  if (!make_gl_context_current_cb_.Run())
    return nullptr;

  DXVAPictureBuffer* dxva_picture_buffer =
      known_picture_buffers_[texture_id].get();
  if (dxva_picture_buffer) {
    dxva_picture_buffer->Reuse();
    return dxva_picture_buffer;
  }

  std::unique_ptr<DXVAPictureBuffer> new_dxva_picture_buffer =
      DXVAPictureBuffer::Create(texture_id, video_config_.coded_size,
                                egl_config_, direct3d_context_->device.get());
  if (!new_dxva_picture_buffer.get())
    return nullptr;

  dxva_picture_buffer = new_dxva_picture_buffer.get();
  known_picture_buffers_[texture_id] = std::move(new_dxva_picture_buffer);
  return dxva_picture_buffer;
}

}  // namespace content
