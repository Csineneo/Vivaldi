// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_COMMON_GPU_MEDIA_VT_VIDEO_ENCODE_ACCELERATOR_MAC_H_
#define CONTENT_COMMON_GPU_MEDIA_VT_VIDEO_ENCODE_ACCELERATOR_MAC_H_

#include "base/mac/scoped_cftyperef.h"
#include "content/common/content_export.h"
#include "media/base/mac/videotoolbox_glue.h"
#include "media/base/mac/videotoolbox_helpers.h"
#include "media/video/video_encode_accelerator.h"

namespace content {

// VideoToolbox.framework implementation of the VideoEncodeAccelerator
// interface for MacOSX. VideoToolbox makes no guarantees that it is thread
// safe, so this object is pinned to the thread on which it is constructed.
class CONTENT_EXPORT VTVideoEncodeAccelerator
    : public media::VideoEncodeAccelerator {
 public:
  VTVideoEncodeAccelerator();
  ~VTVideoEncodeAccelerator() override;

  // media::VideoEncodeAccelerator implementation.
  media::VideoEncodeAccelerator::SupportedProfiles GetSupportedProfiles()
      override;
  bool Initialize(media::VideoPixelFormat format,
                  const gfx::Size& input_visible_size,
                  media::VideoCodecProfile output_profile,
                  uint32_t initial_bitrate,
                  Client* client) override;
  void Encode(const scoped_refptr<media::VideoFrame>& frame,
              bool force_keyframe) override;
  void UseOutputBitstreamBuffer(const media::BitstreamBuffer& buffer) override;
  void RequestEncodingParametersChange(uint32_t bitrate,
                                       uint32_t framerate) override;
  void Destroy() override;

 private:
  using CMSampleBufferRef = CoreMediaGlue::CMSampleBufferRef;
  using VTCompressionSessionRef = VideoToolboxGlue::VTCompressionSessionRef;
  using VTEncodeInfoFlags = VideoToolboxGlue::VTEncodeInfoFlags;

  // Holds the associated data of a video frame being processed.
  struct InProgressFrameEncode;

  // Holds output buffers coming from the encoder.
  struct EncodeOutput;

  // Holds output buffers coming from the client ready to be filled.
  struct BitstreamBufferRef;

  // Encoding tasks to be run on |encoder_thread_|.
  void EncodeTask(const scoped_refptr<media::VideoFrame>& frame,
                  bool force_keyframe);
  void UseOutputBitstreamBufferTask(scoped_ptr<BitstreamBufferRef> buffer_ref);
  void RequestEncodingParametersChangeTask(uint32_t bitrate,
                                           uint32_t framerate);
  void DestroyTask();

  // Helper function to notify the client of an error on |client_task_runner_|.
  void NotifyError(media::VideoEncodeAccelerator::Error error);

  // Compression session callback function to handle compressed frames.
  static void CompressionCallback(void* encoder_opaque,
                                  void* request_opaque,
                                  OSStatus status,
                                  VTEncodeInfoFlags info,
                                  CMSampleBufferRef sbuf);
  void CompressionCallbackTask(OSStatus status,
                               scoped_ptr<EncodeOutput> encode_output);

  // Copy CMSampleBuffer into a BitstreamBuffer and return it to the |client_|.
  void ReturnBitstreamBuffer(
      scoped_ptr<EncodeOutput> encode_output,
      scoped_ptr<VTVideoEncodeAccelerator::BitstreamBufferRef> buffer_ref);

  // Reset the encoder's compression session by destroying the existing one
  // using DestroyCompressionSession() and creating a new one. The new session
  // is configured using ConfigureCompressionSession().
  bool ResetCompressionSession();

  // Create a compression session, with HW encoder enforced if
  // |require_hw_encoding| is set.
  bool CreateCompressionSession(
      base::ScopedCFTypeRef<CFDictionaryRef> attributes,
      const gfx::Size& input_size,
      bool require_hw_encoding);

  // Configure the current compression session using current encoder settings.
  bool ConfigureCompressionSession();

  // Destroy the current compression session if any. Blocks until all pending
  // frames have been flushed out (similar to EmitFrames without doing any
  // encoding work).
  void DestroyCompressionSession();

  // VideoToolboxGlue provides access to VideoToolbox at runtime.
  const VideoToolboxGlue* videotoolbox_glue_;
  base::ScopedCFTypeRef<VTCompressionSessionRef> compression_session_;

  gfx::Size input_visible_size_;
  size_t bitstream_buffer_size_;
  int32_t frame_rate_;
  int32_t target_bitrate_;

  // Bitstream buffers ready to be used to return encoded output as a FIFO.
  std::deque<scoped_ptr<BitstreamBufferRef>> bitstream_buffer_queue_;

  // EncodeOutput needs to be copied into a BitstreamBufferRef as a FIFO.
  std::deque<scoped_ptr<EncodeOutput>> encoder_output_queue_;

  // Our original calling task runner for the child thread.
  const scoped_refptr<base::SingleThreadTaskRunner> client_task_runner_;

  // To expose client callbacks from VideoEncodeAccelerator.
  // NOTE: all calls to this object *MUST* be executed on
  // |client_task_runner_|.
  base::WeakPtr<Client> client_;
  scoped_ptr<base::WeakPtrFactory<Client> > client_ptr_factory_;

  // Thread checker to enforce that this object is used on a specific thread.
  // It is pinned on |client_task_runner_| thread.
  base::ThreadChecker thread_checker_;

  // This thread services tasks posted from the VEA API entry points by the
  // GPU child thread and CompressionCallback() posted from device thread.
  base::Thread encoder_thread_;
  scoped_refptr<base::SingleThreadTaskRunner> encoder_thread_task_runner_;

  // Declared last to ensure that all weak pointers are invalidated before
  // other destructors run.
  base::WeakPtr<VTVideoEncodeAccelerator> encoder_weak_ptr_;
  base::WeakPtrFactory<VTVideoEncodeAccelerator> encoder_task_weak_factory_;

  DISALLOW_COPY_AND_ASSIGN(VTVideoEncodeAccelerator);
};

} // namespace content

#endif  // CONTENT_COMMON_GPU_MEDIA_VT_VIDEO_ENCODE_ACCELERATOR_MAC_H_
