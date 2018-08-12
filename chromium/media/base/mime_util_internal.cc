// Copyright 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media/base/mime_util_internal.h"

#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "build/build_config.h"
#include "media/base/media.h"
#include "media/base/video_codecs.h"
#include "media/media_features.h"

#include "media/ffmpeg/ffmpeg_common.h"
#include "media/filters/ffmpeg_glue.h"

#if defined(OS_ANDROID)
#include "base/android/build_info.h"
#include "media/base/android/media_codec_util.h"
#endif

namespace media {
namespace internal {

enum MediaFormatType { COMMON, PROPRIETARY };

struct MediaFormat {
  const char* const mime_type;
  MediaFormatType format_type;
  const char* const codecs_list;
};

#if defined(USE_PROPRIETARY_CODECS)
// Following is the list of RFC 6381 compliant codecs:
//   mp4a.66     - MPEG-2 AAC MAIN
//   mp4a.67     - MPEG-2 AAC LC
//   mp4a.68     - MPEG-2 AAC SSR
//   mp4a.69     - MPEG-2 extension to MPEG-1
//   mp4a.6B     - MPEG-1 audio
//   mp4a.40.2   - MPEG-4 AAC LC
//   mp4a.40.02  - MPEG-4 AAC LC (leading 0 in aud-oti for compatibility)
//   mp4a.40.5   - MPEG-4 HE-AAC v1 (AAC LC + SBR)
//   mp4a.40.05  - MPEG-4 HE-AAC v1 (AAC LC + SBR) (leading 0 in aud-oti for
//                 compatibility)
//   mp4a.40.29  - MPEG-4 HE-AAC v2 (AAC LC + SBR + PS)
//
//   avc1.42E0xx - H.264 Baseline
//   avc1.4D40xx - H.264 Main
//   avc1.6400xx - H.264 High
static const char kMP4AudioCodecsExpression[] =
    "mp4a.66,mp4a.67,mp4a.68,mp4a.69,mp4a.6B,mp4a.40.2,mp4a.40.02,mp4a.40.5,"
#if BUILDFLAG(ENABLE_AC3_EAC3_AUDIO_DEMUXING)
    // Only one variant each of ac3 and eac3 codec string is sufficient here,
    // since these strings are parsed and mapped to MimeUtil::Codec enum values.
    "ac-3,ec-3,"
#endif
    "mp4a.40.05,mp4a.40.29";
static const char kMP4VideoCodecsExpression[] =
    // This is not a complete list of supported avc1 codecs. It is simply used
    // to register support for the corresponding Codec enum. Instead of using
    // strings in these three arrays, we should use the Codec enum values.
    // This will avoid confusion and unnecessary parsing at runtime.
    // kUnambiguousCodecStringMap/kAmbiguousCodecStringMap should be the only
    // mapping from strings to codecs. See crbug.com/461009.
    "avc1.42E00A,avc1.4D400A,avc1.64000A,"
#if BUILDFLAG(ENABLE_HEVC_DEMUXING)
    // Any valid unambiguous HEVC codec id will work here, since these strings
    // are parsed and mapped to MimeUtil::Codec enum values.
    "hev1.1.6.L93.B0,"
#endif
    "mp4a.66,mp4a.67,mp4a.68,mp4a.69,mp4a.6B,mp4a.40.2,mp4a.40.02,mp4a.40.5,"
#if BUILDFLAG(ENABLE_AC3_EAC3_AUDIO_DEMUXING)
    // Only one variant each of ac3 and eac3 codec string is sufficient here,
    // since these strings are parsed and mapped to MimeUtil::Codec enum values.
    "ac-3,ec-3,"
#endif
    "mp4a.40.05,mp4a.40.29";
#endif  // USE_PROPRIETARY_CODECS

// A list of media types (https://en.wikipedia.org/wiki/Media_type) and
// corresponding media codecs supported by these types/containers.
// Media formats marked as PROPRIETARY are not supported by Chromium, only
// Google Chrome browser supports them.
static const MediaFormat kFormatCodecMappings[] = {
    {"video/webm", COMMON, "opus,vorbis,vp8,vp8.0,vp9,vp9.0"},
    {"audio/webm", COMMON, "opus,vorbis"},
    {"audio/wav", COMMON, "1"},
    {"audio/x-wav", COMMON, "1"},
#if !defined(OS_ANDROID)
    // Note: Android does not support Theora and thus video/ogg.
    {"video/ogg", COMMON, "opus,theora,vorbis"},
#endif
    {"audio/ogg", COMMON, "opus,vorbis"},
    // Note: Theora is not supported on Android and will be rejected during the
    // call to IsCodecSupportedOnPlatform().
    {"application/ogg", COMMON, "opus,theora,vorbis"},
#if defined(USE_PROPRIETARY_CODECS)
    {"audio/mpeg", PROPRIETARY, "mp3"},
    {"audio/mp3", PROPRIETARY, ""},
    {"audio/x-mp3", PROPRIETARY, ""},
    {"audio/aac", PROPRIETARY, ""},  // AAC / ADTS
    {"audio/mp4", PROPRIETARY, kMP4AudioCodecsExpression},
    {"audio/x-m4a", PROPRIETARY, kMP4AudioCodecsExpression},
    {"video/mp4", PROPRIETARY, kMP4VideoCodecsExpression},
    {"video/x-m4v", PROPRIETARY, kMP4VideoCodecsExpression},
#if BUILDFLAG(ENABLE_MSE_MPEG2TS_STREAM_PARSER)
    {"video/mp2t", PROPRIETARY, kMP4VideoCodecsExpression},
#endif
#if defined(OS_ANDROID)
    // HTTP Live Streaming (HLS)
    {"application/x-mpegurl", PROPRIETARY, kMP4VideoCodecsExpression},
    {"application/vnd.apple.mpegurl", PROPRIETARY, kMP4VideoCodecsExpression}
#endif
#endif  // USE_PROPRIETARY_CODECS
};

struct CodecIDMappings {
  const char* const codec_id;
  MimeUtil::Codec codec;
};

// List of codec IDs that provide enough information to determine the
// codec and profile being requested.
//
// The "mp4a" strings come from RFC 6381.
static const CodecIDMappings kUnambiguousCodecStringMap[] = {
    {"1", MimeUtil::PCM},  // We only allow this for WAV so it isn't ambiguous.
    // avc1/avc3.XXXXXX may be unambiguous; handled by ParseAVCCodecId().
    // hev1/hvc1.XXXXXX may be unambiguous; handled by ParseHEVCCodecID().
    {"mp3", MimeUtil::MP3},
    {"mp4a.66", MimeUtil::MPEG2_AAC_MAIN},
    {"mp4a.67", MimeUtil::MPEG2_AAC_LC},
    {"mp4a.68", MimeUtil::MPEG2_AAC_SSR},
    {"mp4a.69", MimeUtil::MP3},
    {"mp4a.6B", MimeUtil::MP3},
    {"mp4a.40.2", MimeUtil::MPEG4_AAC_LC},
    {"mp4a.40.02", MimeUtil::MPEG4_AAC_LC},
    {"mp4a.40.5", MimeUtil::MPEG4_AAC_SBR_v1},
    {"mp4a.40.05", MimeUtil::MPEG4_AAC_SBR_v1},
    {"mp4a.40.29", MimeUtil::MPEG4_AAC_SBR_PS_v2},
#if BUILDFLAG(ENABLE_AC3_EAC3_AUDIO_DEMUXING)
    // TODO(servolk): Strictly speaking only mp4a.A5 and mp4a.A6 codec ids are
    // valid according to RFC 6381 section 3.3, 3.4. Lower-case oti (mp4a.a5 and
    // mp4a.a6) should be rejected. But we used to allow those in older versions
    // of Chromecast firmware and some apps (notably MPL) depend on those codec
    // types being supported, so they should be allowed for now
    // (crbug.com/564960).
    {"ac-3", MimeUtil::AC3},
    {"mp4a.a5", MimeUtil::AC3},
    {"mp4a.A5", MimeUtil::AC3},
    {"ec-3", MimeUtil::EAC3},
    {"mp4a.a6", MimeUtil::EAC3},
    {"mp4a.A6", MimeUtil::EAC3},
#endif
    {"vorbis", MimeUtil::VORBIS},
    {"opus", MimeUtil::OPUS},
    {"vp8", MimeUtil::VP8},
    {"vp8.0", MimeUtil::VP8},
    {"vp9", MimeUtil::VP9},
    {"vp9.0", MimeUtil::VP9},
    {"theora", MimeUtil::THEORA}};

// List of codec IDs that are ambiguous and don't provide
// enough information to determine the codec and profile.
// The codec in these entries indicate the codec and profile
// we assume the user is trying to indicate.
static const CodecIDMappings kAmbiguousCodecStringMap[] = {
    {"mp4a.40", MimeUtil::MPEG4_AAC_LC},
    {"avc1", MimeUtil::H264},
    {"avc3", MimeUtil::H264},
    // avc1/avc3.XXXXXX may be ambiguous; handled by ParseAVCCodecId().
};

#if BUILDFLAG(ENABLE_MSE_MPEG2TS_STREAM_PARSER)
static const char kHexString[] = "0123456789ABCDEF";
static char IntToHex(int i) {
  DCHECK_GE(i, 0) << i << " not a hex value";
  DCHECK_LE(i, 15) << i << " not a hex value";
  return kHexString[i];
}

static std::string TranslateLegacyAvc1CodecIds(const std::string& codec_id) {
  // Special handling for old, pre-RFC 6381 format avc1 strings, which are still
  // being used by some HLS apps to preserve backward compatibility with older
  // iOS devices. The old format was avc1.<profile>.<level>
  // Where <profile> is H.264 profile_idc encoded as a decimal number, i.e.
  // 66 is baseline profile (0x42)
  // 77 is main profile (0x4d)
  // 100 is high profile (0x64)
  // And <level> is H.264 level multiplied by 10, also encoded as decimal number
  // E.g. <level> 31 corresponds to H.264 level 3.1
  // See, for example, http://qtdevseed.apple.com/qadrift/testcases/tc-0133.php
  uint32_t level_start = 0;
  std::string result;
  if (base::StartsWith(codec_id, "avc1.66.", base::CompareCase::SENSITIVE)) {
    level_start = 8;
    result = "avc1.4200";
  } else if (base::StartsWith(codec_id, "avc1.77.",
                              base::CompareCase::SENSITIVE)) {
    level_start = 8;
    result = "avc1.4D00";
  } else if (base::StartsWith(codec_id, "avc1.100.",
                              base::CompareCase::SENSITIVE)) {
    level_start = 9;
    result = "avc1.6400";
  }

  uint32_t level = 0;
  if (level_start > 0 &&
      base::StringToUint(codec_id.substr(level_start), &level) && level < 256) {
    // This is a valid legacy avc1 codec id - return the codec id translated
    // into RFC 6381 format.
    result.push_back(IntToHex(level >> 4));
    result.push_back(IntToHex(level & 0xf));
    return result;
  }

  // This is not a valid legacy avc1 codec id - return the original codec id.
  return codec_id;
}
#endif

static bool IsValidH264Level(uint8_t level_idc) {
  // Valid levels taken from Table A-1 in ISO/IEC 14496-10.
  // Level_idc represents the standard level represented as decimal number
  // multiplied by ten, e.g. level_idc==32 corresponds to level==3.2
  return ((level_idc >= 10 && level_idc <= 13) ||
          (level_idc >= 20 && level_idc <= 22) ||
          (level_idc >= 30 && level_idc <= 32) ||
          (level_idc >= 40 && level_idc <= 42) ||
          (level_idc >= 50 && level_idc <= 51));
}

#if BUILDFLAG(ENABLE_HEVC_DEMUXING)
// ISO/IEC FDIS 14496-15 standard section E.3 describes the syntax of codec ids
// reserved for HEVC. According to that spec HEVC codec id must start with
// either "hev1." or "hvc1.". We don't yet support full parsing of HEVC codec
// ids, but since no other codec id starts with those string we'll just treat
// any string starting with "hev1." or "hvc1." as valid HEVC codec ids.
// crbug.com/482761
static bool ParseHEVCCodecID(const std::string& codec_id,
                             MimeUtil::Codec* codec,
                             bool* is_ambiguous) {
  if (base::StartsWith(codec_id, "hev1.", base::CompareCase::SENSITIVE) ||
      base::StartsWith(codec_id, "hvc1.", base::CompareCase::SENSITIVE)) {
    *codec = MimeUtil::HEVC_MAIN;

    // TODO(servolk): Full HEVC codec id parsing is not implemented yet (see
    // crbug.com/482761). So treat HEVC codec ids as ambiguous for now.
    *is_ambiguous = true;

    // TODO(servolk): Most HEVC codec ids are treated as ambiguous (see above),
    // but we need to recognize at least one valid unambiguous HEVC codec id,
    // which is added into kMP4VideoCodecsExpression. We need it to be
    // unambiguous to avoid DCHECK(!is_ambiguous) in InitializeMimeTypeMaps. We
    // also use these in unit tests (see
    // content/browser/media/media_canplaytype_browsertest.cc).
    // Remove this workaround after crbug.com/482761 is fixed.
    if (codec_id == "hev1.1.6.L93.B0" || codec_id == "hvc1.1.6.L93.B0") {
      *is_ambiguous = false;
    }

    return true;
  }

  return false;
}
#endif

MimeUtil::MimeUtil() : allow_proprietary_codecs_(false) {
#if defined(OS_ANDROID)
  platform_info_.is_unified_media_pipeline_enabled =
      IsUnifiedMediaPipelineEnabled();
  // When the unified media pipeline is enabled, we need support for both GPU
  // video decoders and MediaCodec; indicated by HasPlatformDecoderSupport().
  // When the Android pipeline is used, we only need access to MediaCodec.
  platform_info_.has_platform_decoders =
      platform_info_.is_unified_media_pipeline_enabled
          ? HasPlatformDecoderSupport()
          : MediaCodecUtil::IsMediaCodecAvailable();
  platform_info_.has_platform_vp8_decoder =
      MediaCodecUtil::IsVp8DecoderAvailable();
  platform_info_.has_platform_vp9_decoder =
      MediaCodecUtil::IsVp9DecoderAvailable();
  platform_info_.supports_opus = PlatformHasOpusSupport();
#endif

  InitializeMimeTypeMaps();
}

MimeUtil::~MimeUtil() {}

SupportsType MimeUtil::AreSupportedCodecs(
    const CodecSet& supported_codecs,
    const std::vector<std::string>& codecs,
    const std::string& mime_type_lower_case,
    bool is_encrypted) const {
  DCHECK(!supported_codecs.empty());
  DCHECK(!codecs.empty());

  SupportsType result = IsSupported;
  for (size_t i = 0; i < codecs.size(); ++i) {
    bool is_ambiguous = true;
    Codec codec = INVALID_CODEC;
    if (!StringToCodec(codecs[i], &codec, &is_ambiguous))
      return IsNotSupported;

    if (!IsCodecSupported(codec, mime_type_lower_case, is_encrypted) ||
        supported_codecs.find(codec) == supported_codecs.end()) {
      return IsNotSupported;
    }

    if (is_ambiguous)
      result = MayBeSupported;
  }

  return result;
}

void MimeUtil::InitializeMimeTypeMaps() {
// Initialize the supported media types.
#if defined(USE_SYSTEM_PROPRIETARY_CODECS)
  allow_proprietary_codecs_ = true;
#elif defined(USE_PROPRIETARY_CODECS)
  FFmpegGlue::InitializeFFmpeg();
  if (avcodec_find_decoder(AV_CODEC_ID_H264)) {
  // assume the rest of the proprietary codecs are in as well
  allow_proprietary_codecs_ = true;
  }
#endif

  for (size_t i = 0; i < arraysize(kUnambiguousCodecStringMap); ++i) {
    string_to_codec_map_[kUnambiguousCodecStringMap[i].codec_id] =
        CodecEntry(kUnambiguousCodecStringMap[i].codec, false);
  }

  for (size_t i = 0; i < arraysize(kAmbiguousCodecStringMap); ++i) {
    string_to_codec_map_[kAmbiguousCodecStringMap[i].codec_id] =
        CodecEntry(kAmbiguousCodecStringMap[i].codec, true);
  }

  // Initialize the supported media formats.
  for (size_t i = 0; i < arraysize(kFormatCodecMappings); ++i) {
    std::vector<std::string> mime_type_codecs;
    ParseCodecString(kFormatCodecMappings[i].codecs_list, &mime_type_codecs,
                     false);

    CodecSet codecs;
    for (size_t j = 0; j < mime_type_codecs.size(); ++j) {
      Codec codec = INVALID_CODEC;
      bool is_ambiguous = true;
      CHECK(StringToCodec(mime_type_codecs[j], &codec, &is_ambiguous));
      DCHECK(!is_ambiguous);
      codecs.insert(codec);
    }

    media_format_map_[kFormatCodecMappings[i].mime_type] = codecs;
  }
}

bool MimeUtil::IsSupportedMediaMimeType(const std::string& mime_type) const {
  return media_format_map_.find(base::ToLowerASCII(mime_type)) !=
         media_format_map_.end();
}

void MimeUtil::ParseCodecString(const std::string& codecs,
                                std::vector<std::string>* codecs_out,
                                bool strip) {
  *codecs_out =
      base::SplitString(base::TrimString(codecs, "\"", base::TRIM_ALL), ",",
                        base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL);

  // Convert empty or all-whitespace input to 0 results.
  if (codecs_out->size() == 1 && (*codecs_out)[0].empty())
    codecs_out->clear();

  if (!strip)
    return;

  // Strip everything past the first '.'
  for (std::vector<std::string>::iterator it = codecs_out->begin();
       it != codecs_out->end(); ++it) {
    size_t found = it->find_first_of('.');
    if (found != std::string::npos)
      it->resize(found);
  }
}

SupportsType MimeUtil::IsSupportedMediaFormat(
    const std::string& mime_type,
    const std::vector<std::string>& codecs,
    bool is_encrypted) const {
  const std::string mime_type_lower_case = base::ToLowerASCII(mime_type);
  MediaFormatMappings::const_iterator it_media_format_map =
      media_format_map_.find(mime_type_lower_case);
  if (it_media_format_map == media_format_map_.end())
    return IsNotSupported;

  if (it_media_format_map->second.empty()) {
    // We get here if the mimetype does not expect a codecs parameter.
    return (codecs.empty() && IsDefaultCodecSupportedLowerCase(
                                  mime_type_lower_case, is_encrypted))
               ? IsSupported
               : IsNotSupported;
  }

  if (codecs.empty()) {
    // We get here if the mimetype expects to get a codecs parameter,
    // but didn't get one. If |mime_type_lower_case| does not have a default
    // codec the best we can do is say "maybe" because we don't have enough
    // information.
    Codec default_codec = INVALID_CODEC;
    if (!GetDefaultCodecLowerCase(mime_type_lower_case, &default_codec))
      return MayBeSupported;

    return IsCodecSupported(default_codec, mime_type_lower_case, is_encrypted)
               ? IsSupported
               : IsNotSupported;
  }

#if BUILDFLAG(ENABLE_MSE_MPEG2TS_STREAM_PARSER)
  if (mime_type_lower_case == "video/mp2t") {
    std::vector<std::string> codecs_to_check;
    for (const auto& codec_id : codecs) {
      codecs_to_check.push_back(TranslateLegacyAvc1CodecIds(codec_id));
    }
    return AreSupportedCodecs(it_media_format_map->second, codecs_to_check,
                              mime_type_lower_case, is_encrypted);
  }
#endif

  return AreSupportedCodecs(it_media_format_map->second, codecs,
                            mime_type_lower_case, is_encrypted);
}

void MimeUtil::RemoveProprietaryMediaTypesAndCodecs() {
  for (size_t i = 0; i < arraysize(kFormatCodecMappings); ++i)
    if (kFormatCodecMappings[i].format_type == PROPRIETARY)
      media_format_map_.erase(kFormatCodecMappings[i].mime_type);
  allow_proprietary_codecs_ = false;
}

// static
bool MimeUtil::IsCodecSupportedOnPlatform(
    Codec codec,
    const std::string& mime_type_lower_case,
    bool is_encrypted,
    const PlatformInfo& platform_info) {
  DCHECK_NE(mime_type_lower_case, "");

  // Encrypted block support is never available without platform decoders.
  if (is_encrypted && !platform_info.has_platform_decoders)
    return false;

  // NOTE: We do not account for Media Source Extensions (MSE) within these
  // checks since it has its own isTypeSupported() which will handle platform
  // specific codec rejections.  See http://crbug.com/587303.

  switch (codec) {
    // ----------------------------------------------------------------------
    // The following codecs are never supported.
    // ----------------------------------------------------------------------
    case INVALID_CODEC:
    case AC3:
    case EAC3:
    case THEORA:
      return false;

    // ----------------------------------------------------------------------
    // The remaining codecs may be supported depending on platform abilities.
    // ----------------------------------------------------------------------

    case PCM:
    case MP3:
    case MPEG4_AAC_LC:
    case MPEG4_AAC_SBR_v1:
    case MPEG4_AAC_SBR_PS_v2:
    case VORBIS:
      // These codecs are always supported; via a platform decoder (when used
      // with MSE/EME), a software decoder (the unified pipeline), or with
      // MediaPlayer.
      DCHECK(!is_encrypted || platform_info.has_platform_decoders);
      return true;

    case MPEG2_AAC_LC:
    case MPEG2_AAC_MAIN:
    case MPEG2_AAC_SSR:
      // MPEG-2 variants of AAC are not supported on Android unless the unified
      // media pipeline can be used. These codecs will be decoded in software.
      return !is_encrypted && platform_info.is_unified_media_pipeline_enabled;

    case OPUS:
      // If clear, the unified pipeline can always decode Opus in software.
      if (!is_encrypted && platform_info.is_unified_media_pipeline_enabled)
        return true;

      // Otherwise, platform support is required.
      if (!platform_info.supports_opus)
        return false;

      // MediaPlayer does not support Opus in ogg containers.
      if (base::EndsWith(mime_type_lower_case, "ogg",
                         base::CompareCase::SENSITIVE)) {
        return false;
      }

      DCHECK(!is_encrypted || platform_info.has_platform_decoders);
      return true;

    case H264:
      // The unified pipeline requires platform support for h264.
      if (platform_info.is_unified_media_pipeline_enabled)
        return platform_info.has_platform_decoders;

      // When MediaPlayer or MediaCodec is used, h264 is always supported.
      DCHECK(!is_encrypted || platform_info.has_platform_decoders);
      return true;

    case HEVC_MAIN:
#if BUILDFLAG(ENABLE_HEVC_DEMUXING)
      if (platform_info.is_unified_media_pipeline_enabled &&
          !platform_info.has_platform_decoders) {
        return false;
      }

#if defined(OS_ANDROID)
      // HEVC/H.265 is supported in Lollipop+ (API Level 21), according to
      // http://developer.android.com/reference/android/media/MediaFormat.html
      return base::android::BuildInfo::GetInstance()->sdk_int() >= 21;
#else
      return true;
#endif  // defined(OS_ANDROID)
#else
      return false;
#endif  // BUILDFLAG(ENABLE_HEVC_DEMUXING)

    case VP8:
      // If clear, the unified pipeline can always decode VP8 in software.
      if (!is_encrypted && platform_info.is_unified_media_pipeline_enabled)
        return true;

      if (is_encrypted)
        return platform_info.has_platform_vp8_decoder;

      // MediaPlayer can always play VP8. Note: This is incorrect for MSE, but
      // MSE does not use this code. http://crbug.com/587303.
      return true;

    case VP9: {
      // If clear, the unified pipeline can always decode VP9 in software.
      if (!is_encrypted && platform_info.is_unified_media_pipeline_enabled)
        return true;

      // Otherwise, platform support is required.
      return platform_info.has_platform_vp9_decoder;
    }
  }

  return false;
}

bool MimeUtil::StringToCodec(const std::string& codec_id,
                             Codec* codec,
                             bool* is_ambiguous) const {
  StringToCodecMappings::const_iterator itr =
      string_to_codec_map_.find(codec_id);
  if (itr != string_to_codec_map_.end()) {
    *codec = itr->second.codec;
    *is_ambiguous = itr->second.is_ambiguous;
    return true;
  }

// If |codec_id| is not in |string_to_codec_map_|, then we assume that it is
// either H.264 or HEVC/H.265 codec ID because currently those are the only
// ones that are not added to the |string_to_codec_map_| and require parsing.

#if BUILDFLAG(ENABLE_HEVC_DEMUXING)
  if (ParseHEVCCodecID(codec_id, codec, is_ambiguous)) {
    return true;
  }
#endif

  VideoCodecProfile profile = VIDEO_CODEC_PROFILE_UNKNOWN;
  uint8_t level_idc = 0;
  if (ParseAVCCodecId(codec_id, &profile, &level_idc)) {
    *codec = MimeUtil::H264;
    *is_ambiguous =
        (profile != H264PROFILE_BASELINE && profile != H264PROFILE_MAIN &&
         profile != H264PROFILE_HIGH) ||
        !IsValidH264Level(level_idc);
    return true;
  }

  DVLOG(4) << __FUNCTION__ << ": Unrecognized codec id " << codec_id;
  return false;
}

bool MimeUtil::IsCodecSupported(Codec codec,
                                const std::string& mime_type_lower_case,
                                bool is_encrypted) const {
  DCHECK_NE(codec, INVALID_CODEC);

#if defined(OS_ANDROID)
  if (!IsCodecSupportedOnPlatform(codec, mime_type_lower_case, is_encrypted,
                                  platform_info_)) {
    return false;
  }
#endif

  return allow_proprietary_codecs_ || !IsCodecProprietary(codec);
}

bool MimeUtil::IsCodecProprietary(Codec codec) const {
  switch (codec) {
    case INVALID_CODEC:
    case AC3:
    case EAC3:
    case MP3:
    case MPEG2_AAC_LC:
    case MPEG2_AAC_MAIN:
    case MPEG2_AAC_SSR:
    case MPEG4_AAC_LC:
    case MPEG4_AAC_SBR_v1:
    case MPEG4_AAC_SBR_PS_v2:
    case H264:
    case HEVC_MAIN:
      return true;

    case PCM:
    case VORBIS:
    case OPUS:
    case VP8:
    case VP9:
    case THEORA:
      return false;
  }

  return true;
}

bool MimeUtil::GetDefaultCodecLowerCase(const std::string& mime_type_lower_case,
                                        Codec* default_codec) const {
  if (mime_type_lower_case == "audio/mpeg" ||
      mime_type_lower_case == "audio/mp3" ||
      mime_type_lower_case == "audio/x-mp3") {
    *default_codec = MimeUtil::MP3;
    return true;
  }

  if (mime_type_lower_case == "audio/aac") {
    *default_codec = MimeUtil::MPEG4_AAC_LC;
    return true;
  }

  return false;
}

bool MimeUtil::IsDefaultCodecSupportedLowerCase(
    const std::string& mime_type_lower_case,
    bool is_encrypted) const {
  Codec default_codec = Codec::INVALID_CODEC;
  if (!GetDefaultCodecLowerCase(mime_type_lower_case, &default_codec))
    return false;
  return IsCodecSupported(default_codec, mime_type_lower_case, is_encrypted);
}

}  // namespace internal
}  // namespace media
