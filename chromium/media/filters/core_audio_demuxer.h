// -*- Mode: c++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
//
// Copyright (C) 2014 Opera Software ASA.  All rights reserved.
//
// This file is an original work developed by Opera Software ASA

#ifndef MEDIA_FILTERS_CORE_AUDIO_DEMUXER_H_
#define MEDIA_FILTERS_CORE_AUDIO_DEMUXER_H_

#include <AudioToolbox/AudioFileStream.h>

#include <string>

#include "base/memory/weak_ptr.h"
#include "base/threading/thread.h"
#include "media/base/data_source.h"
#include "media/base/demuxer.h"

class GURL;

namespace media {

class BlockingUrlProtocol;
class CoreAudioDemuxerStream;

class MEDIA_EXPORT CoreAudioDemuxer : public Demuxer {
 public:
  enum { kStreamInfoBufferSize = 64 * 1024 };

  explicit CoreAudioDemuxer(DataSource* data_source);
  ~CoreAudioDemuxer() override;

  // Demuxer implementation.
  std::string GetDisplayName() const override;
  void Initialize(DemuxerHost* host,
                  const PipelineStatusCB& status_cb,
                  bool enable_text_tracks) override;
  void Seek(base::TimeDelta time,
            const PipelineStatusCB& status_cb) override;
  void Stop() override;
  void AbortPendingReads() override;
  DemuxerStream* GetStream(DemuxerStream::Type type) override;
  base::TimeDelta GetStartTime() const override;
  base::Time GetTimelineOffset() const override;
  int64_t GetMemoryUsage() const override;
  void OnEnabledAudioTracksChanged(
      const std::vector<MediaTrack::Id>& track_ids,
      base::TimeDelta currTime) override;
  void OnSelectedVideoTrackChanged(
      const std::vector<MediaTrack::Id>& track_ids,
      base::TimeDelta currTime) override;

  void ResetDataSourceOffset();
  void ReadDataSourceIfNeeded();

  static bool IsSupported(const std::string& content_type, const GURL& url);

  void StartWaitingForSeek(base::TimeDelta seek_time) override;
  void CancelPendingSeek(base::TimeDelta seek_time) override;

 private:
  // Creates and configures DemuxerStream with Audio stream if it is possible.
  CoreAudioDemuxerStream* CreateAudioDemuxerStream();

  void OnReadAudioFormatInfoDone(const PipelineStatusCB& status_cb,
                                 int read_size);
  void OnReadDataSourceDone(int read_size);
  void OnDataSourceError();

  static void AudioPropertyListenerProc(void* client_data,
                                        AudioFileStreamID audio_file_stream,
                                        AudioFileStreamPropertyID property_id,
                                        UInt32* io_flags);
  static void AudioPacketsProc(
      void* client_data,
      UInt32 number_bytes,
      UInt32 number_packets,
      const void* input_data,
      AudioStreamPacketDescription* packet_descriptions);
  void SetAudioDuration(int64_t duration);

  void ReadDataSourceWithCallback(const DataSource::ReadCB& read_cb);
  void ReadAudioFormatInfo(const PipelineStatusCB& status_cb);
  int ReadDataSource();

  DemuxerHost* host_;         // Weak, owned by WebMediaPlayerImpl
  DataSource* data_source_;   // Weak, owned by WebMediaPlayerImpl

  std::unique_ptr<CoreAudioDemuxerStream> audio_stream_;

  // Thread on which all blocking operations are executed.
  base::Thread blocking_thread_;
  std::unique_ptr<BlockingUrlProtocol> url_protocol_;
  AudioStreamBasicDescription input_format_info_;
  AudioFileStreamID audio_stream_id_;
  uint8_t buffer_[kStreamInfoBufferSize];
  UInt32 bit_rate_;
  bool input_format_found_;

  base::WeakPtrFactory<CoreAudioDemuxer> weak_factory_;
};

}  // namespace media

#endif  // MEDIA_FILTERS_CORE_AUDIO_DEMUXER_H_
