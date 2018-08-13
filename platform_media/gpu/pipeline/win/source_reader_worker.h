// -*- Mode: c++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
//
// Copyright (c) 2017 Vivaldi Technologies AS. All rights reserved.
// Copyright (C) 2014 Opera Software ASA.  All rights reserved.
//
// This file is an original work developed by Opera Software ASA

#ifndef PLATFORM_MEDIA_GPU_PIPELINE_WIN_SOURCE_READER_WORKER_H
#define PLATFORM_MEDIA_GPU_PIPELINE_WIN_SOURCE_READER_WORKER_H

#include <mfidl.h>
#include <d3d9.h>  // if included before |mfidl.h| breaks <propvarutil.h>
// Work around bug in this header by disabling the relevant warning for it.
// https://connect.microsoft.com/VisualStudio/feedback/details/911260/dxva2api-h-in-win8-sdk-triggers-c4201-with-w4
#pragma warning(push)
#pragma warning(disable : 4201)
#include <dxva2api.h>  // if included before |mfidl.h| breaks <propvarutil.h>
#pragma warning(pop)
#include <mfreadwrite.h>

#include "base/time/time.h"
#include "base/win/scoped_comptr.h"

namespace platform_media {

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

class SourceReaderWorker
{
public:
  bool hasReader() { return !!source_reader_.Get(); }

  void SetReader(const base::win::ScopedComPtr<IMFSourceReader>& source_reader);

  HRESULT ReadSampleAsync(DWORD index);

  HRESULT SetCurrentPosition(AutoPropVariant & position);
  HRESULT SetCurrentMediaType(
      DWORD index,
      base::win::ScopedComPtr<IMFMediaType> & media_type);

  HRESULT GetCurrentMediaType(
      DWORD index,
      base::win::ScopedComPtr<IMFMediaType> & media_type);
  HRESULT GetNativeMediaType(
      DWORD index,
      base::win::ScopedComPtr<IMFMediaType> & media_type);
  HRESULT GetDuration(AutoPropVariant & var);
  HRESULT GetAudioBitrate(AutoPropVariant & var);
  HRESULT GetVideoBitrate(AutoPropVariant & var);
  HRESULT GetFileSize(AutoPropVariant & var);

private:

  base::win::ScopedComPtr<IMFSourceReader> source_reader_;
};

}

#endif // PLATFORM_MEDIA_GPU_PIPELINE_WIN_SOURCE_READER_WORKER_H
