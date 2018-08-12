// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "modules/bluetooth/BluetoothRemoteGATTCharacteristic.h"

#include "bindings/core/v8/CallbackPromiseAdapter.h"
#include "bindings/core/v8/ScriptPromise.h"
#include "bindings/core/v8/ScriptPromiseResolver.h"
#include "core/dom/DOMDataView.h"
#include "core/dom/DOMException.h"
#include "core/dom/ExceptionCode.h"
#include "core/events/Event.h"
#include "modules/bluetooth/BluetoothCharacteristicProperties.h"
#include "modules/bluetooth/BluetoothError.h"
#include "modules/bluetooth/BluetoothSupplement.h"
#include "public/platform/modules/bluetooth/WebBluetooth.h"

namespace blink {

namespace {

PassRefPtr<DOMDataView> ConvertWebVectorToDataView(
    const WebVector<uint8_t>& webVector)
{
    static_assert(sizeof(*webVector.data()) == 1, "uint8_t should be a single byte");
    RefPtr<DOMArrayBuffer> domBuffer = DOMArrayBuffer::create(webVector.data(), webVector.size());
    RefPtr<DOMDataView> domDataView = DOMDataView::create(domBuffer, 0, webVector.size());
    return domDataView;
}

} // anonymous namespace

BluetoothRemoteGATTCharacteristic::BluetoothRemoteGATTCharacteristic(ExecutionContext* context, PassOwnPtr<WebBluetoothRemoteGATTCharacteristicInit> webCharacteristic)
    : ActiveDOMObject(context)
    , m_webCharacteristic(webCharacteristic)
    , m_stopped(false)
{
    m_properties = BluetoothCharacteristicProperties::create(m_webCharacteristic->characteristicProperties);
    // See example in Source/platform/heap/ThreadState.h
    ThreadState::current()->registerPreFinalizer(this);
}

BluetoothRemoteGATTCharacteristic* BluetoothRemoteGATTCharacteristic::take(ScriptPromiseResolver* resolver, PassOwnPtr<WebBluetoothRemoteGATTCharacteristicInit> webCharacteristic)
{
    if (!webCharacteristic) {
        return nullptr;
    }
    BluetoothRemoteGATTCharacteristic* characteristic = new BluetoothRemoteGATTCharacteristic(resolver->executionContext(), webCharacteristic);
    // See note in ActiveDOMObject about suspendIfNeeded.
    characteristic->suspendIfNeeded();
    return characteristic;
}

void BluetoothRemoteGATTCharacteristic::setValue(
    const PassRefPtr<DOMDataView>& domDataView)
{
    m_value = domDataView;
}

void BluetoothRemoteGATTCharacteristic::dispatchCharacteristicValueChanged(
    const WebVector<uint8_t>& value)
{
    RefPtr<DOMDataView> domDataView = ConvertWebVectorToDataView(value);
    this->setValue(domDataView);
    dispatchEvent(Event::create(EventTypeNames::characteristicvaluechanged));
}

void BluetoothRemoteGATTCharacteristic::stop()
{
    notifyCharacteristicObjectRemoved();
}

void BluetoothRemoteGATTCharacteristic::dispose()
{
    notifyCharacteristicObjectRemoved();
}

void BluetoothRemoteGATTCharacteristic::notifyCharacteristicObjectRemoved()
{
    if (!m_stopped) {
        m_stopped = true;
        WebBluetooth* webbluetooth = BluetoothSupplement::fromExecutionContext(ActiveDOMObject::executionContext());
        webbluetooth->characteristicObjectRemoved(m_webCharacteristic->characteristicInstanceID, this);
    }
}

const WTF::AtomicString& BluetoothRemoteGATTCharacteristic::interfaceName() const
{
    return EventTargetNames::BluetoothRemoteGATTCharacteristic;
}

ExecutionContext* BluetoothRemoteGATTCharacteristic::executionContext() const
{
    return ActiveDOMObject::executionContext();
}

bool BluetoothRemoteGATTCharacteristic::addEventListenerInternal(const AtomicString& eventType, PassRefPtrWillBeRawPtr<EventListener> listener, const EventListenerOptions& options)
{
    // We will also need to unregister a characteristic once all the event
    // listeners have been removed. See http://crbug.com/541390
    if (eventType == EventTypeNames::characteristicvaluechanged) {
        WebBluetooth* webbluetooth = BluetoothSupplement::fromExecutionContext(executionContext());
        webbluetooth->registerCharacteristicObject(m_webCharacteristic->characteristicInstanceID, this);
    }
    return EventTarget::addEventListenerInternal(eventType, listener, options);
}

class ReadValueCallback : public WebBluetoothReadValueCallbacks {
public:
    ReadValueCallback(BluetoothRemoteGATTCharacteristic* characteristic, ScriptPromiseResolver* resolver) : m_webCharacteristic(characteristic), m_resolver(resolver) {}

    void onSuccess(const WebVector<uint8_t>& value) override
    {
        if (!m_resolver->executionContext() || m_resolver->executionContext()->activeDOMObjectsAreStopped())
            return;

        RefPtr<DOMDataView> domDataView = ConvertWebVectorToDataView(value);
        if (m_webCharacteristic) {
            m_webCharacteristic->setValue(domDataView);
        }
        m_resolver->resolve(domDataView);
    }

    void onError(const WebBluetoothError& e) override
    {
        if (!m_resolver->executionContext() || m_resolver->executionContext()->activeDOMObjectsAreStopped())
            return;
        m_resolver->reject(BluetoothError::take(m_resolver, e));
    }

private:
    WeakPersistent<BluetoothRemoteGATTCharacteristic> m_webCharacteristic;
    Persistent<ScriptPromiseResolver> m_resolver;
};

ScriptPromise BluetoothRemoteGATTCharacteristic::readValue(ScriptState* scriptState)
{
    WebBluetooth* webbluetooth = BluetoothSupplement::fromScriptState(scriptState);

    ScriptPromiseResolver* resolver = ScriptPromiseResolver::create(scriptState);
    ScriptPromise promise = resolver->promise();
    webbluetooth->readValue(m_webCharacteristic->characteristicInstanceID, new ReadValueCallback(this, resolver));

    return promise;
}

class WriteValueCallback : public WebBluetoothWriteValueCallbacks {
public:
    WriteValueCallback(BluetoothRemoteGATTCharacteristic* characteristic, ScriptPromiseResolver* resolver) : m_webCharacteristic(characteristic), m_resolver(resolver) {}

    void onSuccess(const WebVector<uint8_t>& value) override
    {
        if (!m_resolver->executionContext() || m_resolver->executionContext()->activeDOMObjectsAreStopped())
            return;

        if (m_webCharacteristic) {
            m_webCharacteristic->setValue(ConvertWebVectorToDataView(value));
        }
        m_resolver->resolve();
    }

    void onError(const WebBluetoothError& e) override
    {
        if (!m_resolver->executionContext() || m_resolver->executionContext()->activeDOMObjectsAreStopped())
            return;
        m_resolver->reject(BluetoothError::take(m_resolver, e));
    }

private:
    WeakPersistent<BluetoothRemoteGATTCharacteristic> m_webCharacteristic;
    Persistent<ScriptPromiseResolver> m_resolver;
};

ScriptPromise BluetoothRemoteGATTCharacteristic::writeValue(ScriptState* scriptState, const DOMArrayPiece& value)
{
    WebBluetooth* webbluetooth = BluetoothSupplement::fromScriptState(scriptState);
    // Partial implementation of writeValue algorithm:
    // https://webbluetoothchrome.github.io/web-bluetooth/#dom-bluetoothgattcharacteristic-writevalue

    // If bytes is more than 512 bytes long (the maximum length of an attribute
    // value, per Long Attribute Values) return a promise rejected with an
    // InvalidModificationError and abort.
    if (value.byteLength() > 512)
        return ScriptPromise::rejectWithDOMException(scriptState, DOMException::create(InvalidModificationError, "Value can't exceed 512 bytes."));

    // Let valueVector be a copy of the bytes held by value.
    WebVector<uint8_t> valueVector(value.bytes(), value.byteLength());

    ScriptPromiseResolver* resolver = ScriptPromiseResolver::create(scriptState);

    ScriptPromise promise = resolver->promise();
    webbluetooth->writeValue(m_webCharacteristic->characteristicInstanceID, valueVector, new WriteValueCallback(this, resolver));

    return promise;
}

ScriptPromise BluetoothRemoteGATTCharacteristic::startNotifications(ScriptState* scriptState)
{
    WebBluetooth* webbluetooth = BluetoothSupplement::fromScriptState(scriptState);
    ScriptPromiseResolver* resolver = ScriptPromiseResolver::create(scriptState);
    ScriptPromise promise = resolver->promise();
    webbluetooth->startNotifications(m_webCharacteristic->characteristicInstanceID, this, new CallbackPromiseAdapter<void, BluetoothError>(resolver));
    return promise;
}

ScriptPromise BluetoothRemoteGATTCharacteristic::stopNotifications(ScriptState* scriptState)
{
    WebBluetooth* webbluetooth = BluetoothSupplement::fromScriptState(scriptState);
    ScriptPromiseResolver* resolver = ScriptPromiseResolver::create(scriptState);
    ScriptPromise promise = resolver->promise();
    webbluetooth->stopNotifications(m_webCharacteristic->characteristicInstanceID, this, new CallbackPromiseAdapter<void, BluetoothError>(resolver));
    return promise;
}

DEFINE_TRACE(BluetoothRemoteGATTCharacteristic)
{
    RefCountedGarbageCollectedEventTargetWithInlineData<BluetoothRemoteGATTCharacteristic>::trace(visitor);
    ActiveDOMObject::trace(visitor);
    visitor->trace(m_properties);
}

} // namespace blink
