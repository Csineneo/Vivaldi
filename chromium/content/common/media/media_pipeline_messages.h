// -*- Mode: c++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
//
// Copyright (C) 2014 Opera Software ASA.  All rights reserved.
//
// This file is an original work developed by Opera Software ASA

#include "base/memory/shared_memory.h"
#include "base/time/time.h"
#include "content/common/content_export.h"
#include "ipc/ipc_message_macros.h"
#include "media/filters/platform_media_pipeline_types.h"

#undef IPC_MESSAGE_EXPORT
#define IPC_MESSAGE_EXPORT CONTENT_EXPORT

#define IPC_MESSAGE_START MediaPipelineMsgStart

IPC_ENUM_TRAITS_MAX_VALUE(media::PlatformMediaDataType,
                          media::PLATFORM_MEDIA_DATA_TYPE_COUNT - 1)

IPC_ENUM_TRAITS_MAX_VALUE(media::MediaDataStatus,
                          media::kMediaDataStatusCount - 1)

IPC_ENUM_TRAITS_MAX_VALUE(
    media::PlatformMediaDecodingMode,
    static_cast<int>(media::PlatformMediaDecodingMode::COUNT) - 1)

IPC_ENUM_TRAITS_MAX_VALUE(media::VideoRotation, media::VIDEO_ROTATION_MAX)

IPC_STRUCT_TRAITS_BEGIN(media::PlatformMediaTimeInfo)
  IPC_STRUCT_TRAITS_MEMBER(duration)
  IPC_STRUCT_TRAITS_MEMBER(start_time)
IPC_STRUCT_TRAITS_END()

IPC_STRUCT_TRAITS_BEGIN(media::PlatformAudioConfig)
  IPC_STRUCT_TRAITS_MEMBER(format)
  IPC_STRUCT_TRAITS_MEMBER(channel_count)
  IPC_STRUCT_TRAITS_MEMBER(samples_per_second)
IPC_STRUCT_TRAITS_END()

IPC_STRUCT_TRAITS_BEGIN(media::PlatformVideoConfig::Plane)
  IPC_STRUCT_TRAITS_MEMBER(stride)
  IPC_STRUCT_TRAITS_MEMBER(offset)
  IPC_STRUCT_TRAITS_MEMBER(size)
IPC_STRUCT_TRAITS_END()

IPC_STRUCT_TRAITS_BEGIN(media::PlatformVideoConfig)
  IPC_STRUCT_TRAITS_MEMBER(coded_size)
  IPC_STRUCT_TRAITS_MEMBER(visible_rect)
  IPC_STRUCT_TRAITS_MEMBER(natural_size)
  IPC_STRUCT_TRAITS_MEMBER(planes)
  IPC_STRUCT_TRAITS_MEMBER(rotation)
  IPC_STRUCT_TRAITS_MEMBER(decoding_mode)
IPC_STRUCT_TRAITS_END()

IPC_STRUCT_BEGIN(MediaPipelineMsg_DecodedDataReady_Params)
  IPC_STRUCT_MEMBER(media::PlatformMediaDataType, type)
  IPC_STRUCT_MEMBER(media::MediaDataStatus, status)
  IPC_STRUCT_MEMBER(int, size)
  IPC_STRUCT_MEMBER(base::TimeDelta, timestamp)
  IPC_STRUCT_MEMBER(base::TimeDelta, duration)
  // Relevant only when hardware accelerated video decoding is used.
  IPC_STRUCT_MEMBER(uint32_t, client_texture_id)
IPC_STRUCT_END()

IPC_SYNC_MESSAGE_CONTROL2_0(
    MediaPipelineMsg_New,
    int32_t /* route_id */,
    int32_t /* gpu_video_accelerator_factories_route_id */)

IPC_MESSAGE_CONTROL1(MediaPipelineMsg_Destroy,
                     int32_t /* route_id */)

IPC_MESSAGE_ROUTED1(MediaPipelineMsg_RequestBufferForRawData,
                    size_t /* requested_size */)

IPC_MESSAGE_ROUTED2(MediaPipelineMsg_BufferForRawDataReady,
                    size_t /* buffer_size */,
                    base::SharedMemoryHandle /* handle */)

IPC_MESSAGE_ROUTED2(MediaPipelineMsg_RequestBufferForDecodedData,
                    media::PlatformMediaDataType /* type */,
                    size_t /* requested_size */)

IPC_MESSAGE_ROUTED3(MediaPipelineMsg_BufferForDecodedDataReady,
                    media::PlatformMediaDataType /* type */,
                    size_t /* buffer_size */,
                    base::SharedMemoryHandle /* handle */)

IPC_MESSAGE_ROUTED2(MediaPipelineMsg_ReadRawData,
                    int64_t /* position */,
                    int /* size */)

IPC_MESSAGE_ROUTED1(MediaPipelineMsg_RawDataReady,
                    int /* size (DataSource::kReadError on error, 0 on EOS) */)

IPC_MESSAGE_ROUTED2(MediaPipelineMsg_ReadDecodedData,
                    media::PlatformMediaDataType /* type */,
                    // Relevant only for hardware accelarated video decoding.
                    uint32_t /* texture_id */)

IPC_MESSAGE_ROUTED1(MediaPipelineMsg_DecodedDataReady,
                    MediaPipelineMsg_DecodedDataReady_Params /* data */)

IPC_MESSAGE_ROUTED3(MediaPipelineMsg_Initialize,
                    int64_t /* data_source_size (<0 means "unknown") */,
                    bool /* is_data_source_streaming */,
                    std::string /* mime_type */)

IPC_MESSAGE_ROUTED5(MediaPipelineMsg_Initialized,
                    bool /* status */,
                    int /* bitrate */,
                    media::PlatformMediaTimeInfo /* time_info */,
                    media::PlatformAudioConfig /* audio_config */,
                    media::PlatformVideoConfig /* video_config */)

IPC_MESSAGE_ROUTED1(MediaPipelineMsg_AudioConfigChanged,
                    media::PlatformAudioConfig /* audio_config */)

IPC_MESSAGE_ROUTED1(MediaPipelineMsg_VideoConfigChanged,
                    media::PlatformVideoConfig /* video_config */)

IPC_MESSAGE_ROUTED0(MediaPipelineMsg_WillSeek)

IPC_MESSAGE_ROUTED1(MediaPipelineMsg_Seek,
                    base::TimeDelta /* time */)

IPC_MESSAGE_ROUTED1(MediaPipelineMsg_Sought,
                    bool /* success */)

IPC_MESSAGE_ROUTED0(MediaPipelineMsg_Stop)
