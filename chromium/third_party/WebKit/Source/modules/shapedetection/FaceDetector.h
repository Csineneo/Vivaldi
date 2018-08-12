// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FaceDetector_h
#define FaceDetector_h

#include "bindings/core/v8/ScriptPromise.h"
#include "bindings/core/v8/ScriptPromiseResolver.h"
#include "bindings/core/v8/ScriptWrappable.h"
#include "modules/ModulesExport.h"
#include "public/platform/modules/shapedetection/shapedetection.mojom-blink.h"

namespace blink {

class Document;
class HTMLImageElement;
class LocalDOMWindow;
class LocalFrame;

class MODULES_EXPORT FaceDetector final
    : public GarbageCollectedFinalized<FaceDetector>,
      public ScriptWrappable {
  DEFINE_WRAPPERTYPEINFO();

 public:
  static FaceDetector* create(ScriptState*);
  ScriptPromise detect(ScriptState*, const HTMLImageElement*);
  DECLARE_TRACE();

 private:
  explicit FaceDetector(LocalFrame&);
  void onDetectFace(ScriptPromiseResolver*,
                    mojom::blink::FaceDetectionResultPtr);

  mojom::blink::ShapeDetectionPtr m_service;

  HeapHashSet<Member<ScriptPromiseResolver>> m_serviceRequests;
};

}  // namespace blink

#endif  // FaceDetector_h
