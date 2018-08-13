/*
 * Copyright (C) 2013 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef Stream_h
#define Stream_h

#include "core/CoreExport.h"
#include "core/dom/SuspendableObject.h"
#include "platform/heap/Handle.h"
#include "platform/weborigin/KURL.h"
#include "platform/wtf/text/WTFString.h"

namespace blink {

class ExecutionContext;

class CORE_EXPORT Stream final : public GarbageCollectedFinalized<Stream>,
                                 public SuspendableObject {
  USING_GARBAGE_COLLECTED_MIXIN(Stream);

 public:
  static Stream* Create(ExecutionContext* context, const String& media_type) {
    Stream* stream = new Stream(context, media_type);
    stream->SuspendIfNeeded();
    return stream;
  }

  ~Stream() override;

  // Returns the internal URL referring to this stream.
  const KURL& Url() const { return internal_url_; }
  // Returns the media type of this stream.
  const String& GetType() const { return media_type_; }

  // Appends data to this stream.
  void AddData(const char* data, size_t len);
  // Flushes contents buffered in the stream.
  void Flush();
  // Mark this stream finalized so that a reader of this stream is notified
  // of EOF.
  void Finalize();
  // Mark this stream finalized due to an error so that a reader of this
  // stream is notified of EOF due to the error.
  void Abort();

  // Allow an external reader class to mark this object neutered so that they
  // won't load the corresponding stream again. All stream objects are
  // read-once for now.
  void Neuter() { is_neutered_ = true; }
  bool IsNeutered() const { return is_neutered_; }

  // Implementation of SuspendableObject.
  //
  // FIXME: Implement suspend() and resume() when necessary.
  void Suspend() override;
  void Resume() override;
  void ContextDestroyed(ExecutionContext*) override;

  DECLARE_VIRTUAL_TRACE();

 protected:
  Stream(ExecutionContext*, const String& media_type);

  // This is an internal URL referring to the blob data associated with this
  // object. It serves as an identifier for this blob. The internal URL is never
  // used to source the blob's content into an HTML or for FileRead'ing, public
  // blob URLs must be used for those purposes.
  KURL internal_url_;

  String media_type_;

  bool is_neutered_;
};

}  // namespace blink

#endif  // Stream_h
