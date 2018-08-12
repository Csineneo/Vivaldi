// -*- Mode: c++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
//
// Copyright (C) 2014 Opera Software ASA.  All rights reserved.
//
// This file is an original work developed by Opera Software ASA

#ifndef MEDIA_FILTERS_IPC_MEDIA_PIPELINE_HOST_H_
#define MEDIA_FILTERS_IPC_MEDIA_PIPELINE_HOST_H_

#include <string>

#include "base/callback_forward.h"
#include "base/sequenced_task_runner.h"
#include "base/time/time.h"
#include "media/base/demuxer_stream.h"
#include "media/base/pipeline_status.h"
#include "media/filters/platform_media_pipeline_types.h"

namespace media {

class DataSource;
class GpuVideoAcceleratorFactories;

// Represents the renderer side of the IPC connection between the IPCDemuxer
// and the IPCMediaPipeline in the GPU process. It is responsible for
// establishing the IPC connection. It provides methods needed by the demuxer
// and the demuxer stream to work - talk to the decoders over the IPC. As well
// as the methods for responding on the requests recived over IPC for the data
// from the data source.
class IPCMediaPipelineHost {
 public:
  using Creator = base::Callback<std::unique_ptr<IPCMediaPipelineHost>(
      const scoped_refptr<base::SequencedTaskRunner>& task_runner,
      media::DataSource* data_source)>;

  using InitializeCB =
      base::Callback<void(bool success,
                          int bitrate,
                          const PlatformMediaTimeInfo& time_info,
                          const PlatformAudioConfig& audio_config,
                          const PlatformVideoConfig& video_config)>;

  virtual ~IPCMediaPipelineHost() {}

  virtual void Initialize(const std::string& mimetype,
                          const InitializeCB& callback) = 0;

  // Used to inform the platform side of the pipeline that a seek request is
  // about to arrive.  This lets the platform drop everything it was doing and
  // become ready to handle the seek request quickly.
  virtual void StartWaitingForSeek() = 0;

  // Performs the seek over the IPC.
  virtual void Seek(base::TimeDelta time,
                    const PipelineStatusCB& status_cb) = 0;

  // Stops the demuxer.
  virtual void Stop() = 0;

  // Starts an asynchronous read of decoded media data over the IPC.
  virtual void ReadDecodedData(PlatformMediaDataType type,
                               const DemuxerStream::ReadCB& read_cb) = 0;

  // Wrapper for PlatformMediaPipeline::EnlargesBuffersOnUnderflow()
  // (to let code in media module access it without breaking dependencies).
  virtual bool PlatformEnlargesBuffersOnUnderflow() const = 0;

  // Returns the target capacity of the raw media data buffer, in the backward
  // and forward directions.  A return value of base::TimeDelta() means "use
  // the default value".
  virtual base::TimeDelta GetTargetBufferDurationBehind() const = 0;
  virtual base::TimeDelta GetTargetBufferDurationAhead() const = 0;

  virtual PlatformAudioConfig audio_config() const = 0;
  virtual PlatformVideoConfig video_config() const = 0;
};

}  // namespace media

#endif  // MEDIA_FILTERS_IPC_MEDIA_PIPELINE_HOST_H_
