// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef Sensor_h
#define Sensor_h

#include "bindings/core/v8/ActiveScriptWrappable.h"
#include "bindings/core/v8/ScriptWrappable.h"
#include "core/dom/ActiveDOMObject.h"
#include "core/dom/ContextLifecycleObserver.h"
#include "core/frame/PlatformEventController.h"
#include "core/page/PageVisibilityObserver.h"
#include "modules/EventTargetModules.h"
#include "modules/sensor/SensorOptions.h"
#include "modules/sensor/SensorProxy.h"
#include "platform/heap/Handle.h"

namespace blink {

class ExceptionState;
class ScriptState;
class SensorReading;
class SensorPollingStrategy;

class Sensor : public EventTargetWithInlineData,
               public ActiveScriptWrappable,
               public ContextLifecycleObserver,
               public PageVisibilityObserver,
               public SensorProxy::Observer {
  USING_GARBAGE_COLLECTED_MIXIN(Sensor);
  DEFINE_WRAPPERTYPEINFO();

 public:
  enum class SensorState { IDLE, ACTIVATING, ACTIVE, ERRORED };

  ~Sensor() override;

  void start(ScriptState*, ExceptionState&);
  void stop(ScriptState*, ExceptionState&);

  // EventTarget overrides.
  const AtomicString& interfaceName() const override {
    return EventTargetNames::Sensor;
  }
  ExecutionContext* getExecutionContext() const override {
    return ContextLifecycleObserver::getExecutionContext();
  }

  // Getters
  String state() const;
  // TODO(riju): crbug.com/614797 .
  SensorReading* reading() const;

  DEFINE_ATTRIBUTE_EVENT_LISTENER(error);
  DEFINE_ATTRIBUTE_EVENT_LISTENER(change);
  DEFINE_ATTRIBUTE_EVENT_LISTENER(statechange);

  // ActiveScriptWrappable overrides.
  bool hasPendingActivity() const override;

  DECLARE_VIRTUAL_TRACE();

 protected:
  Sensor(ScriptState*,
         const SensorOptions&,
         ExceptionState&,
         device::mojom::blink::SensorType);
  virtual SensorReading* createSensorReading(SensorProxy*) = 0;

  using SensorConfigurationPtr = device::mojom::blink::SensorConfigurationPtr;
  using SensorConfiguration = device::mojom::blink::SensorConfiguration;
  virtual SensorConfigurationPtr createSensorConfig(
      const SensorOptions&,
      const SensorConfiguration& defaultConfiguration) = 0;

 private:
  void initSensorProxyIfNeeded();

  // ContextLifecycleObserver overrides.
  void contextDestroyed() override;

  // SensorController::Observer overrides.
  void onSensorInitialized() override;
  void onSensorReadingChanged() override;
  void onSensorError(ExceptionCode,
                     const String& sanitizedMessage,
                     const String& unsanitizedMessage) override;

  void onStartRequestCompleted(bool);
  void onStopRequestCompleted(bool);

  // PageVisibilityObserver overrides.
  void pageVisibilityChanged() override;

  void startListening();
  void stopListening();

  // Makes sensor reading refresh its values from the shared buffer.
  void pollForData();

  void updateState(SensorState newState);
  void reportError(ExceptionCode = UnknownError,
                   const String& sanitizedMessage = String(),
                   const String& unsanitizedMessage = String());

  void updatePollingStatus();

  void notifySensorReadingChanged();
  void notifyStateChanged();
  void notifyError(DOMException* error);

 private:
  Member<SensorReading> m_sensorReading;
  SensorOptions m_sensorOptions;
  device::mojom::blink::SensorType m_type;
  SensorState m_state;
  Member<SensorProxy> m_sensorProxy;
  std::unique_ptr<SensorPollingStrategy> m_polling;
  SensorProxy::Reading m_storedData;
  SensorConfigurationPtr m_configuration;
};

}  // namespace blink

#endif  // Sensor_h
