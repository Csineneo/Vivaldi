// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PaymentRequest_h
#define PaymentRequest_h

#include "bindings/core/v8/ScriptPromise.h"
#include "bindings/core/v8/ScriptValue.h"
#include "bindings/core/v8/ScriptWrappable.h"
#include "core/events/EventTarget.h"
#include "modules/ModulesExport.h"
#include "modules/payments/PaymentCompleter.h"
#include "modules/payments/PaymentDetails.h"
#include "modules/payments/PaymentOptions.h"
#include "mojo/public/cpp/bindings/binding.h"
#include "platform/heap/Handle.h"
#include "public/platform/modules/payments/payment_request.mojom-wtf.h"
#include "wtf/Compiler.h"
#include "wtf/Noncopyable.h"
#include "wtf/RefPtr.h"
#include "wtf/Vector.h"
#include "wtf/text/WTFString.h"

namespace blink {

class ExceptionState;
class ScriptPromiseResolver;
class ScriptState;
class ShippingAddress;

class MODULES_EXPORT PaymentRequest final : public RefCountedGarbageCollectedEventTargetWithInlineData<PaymentRequest>, WTF_NON_EXPORTED_BASE(public mojom::wtf::PaymentRequestClient), public PaymentCompleter {
    DEFINE_WRAPPERTYPEINFO();
    REFCOUNTED_GARBAGE_COLLECTED_EVENT_TARGET(PaymentRequest);
    USING_GARBAGE_COLLECTED_MIXIN(PaymentRequest)
    WTF_MAKE_NONCOPYABLE(PaymentRequest);

public:
    static PaymentRequest* create(ScriptState*, const Vector<String>& supportedMethods, const PaymentDetails&, ExceptionState&);
    static PaymentRequest* create(ScriptState*, const Vector<String>& supportedMethods, const PaymentDetails&, const PaymentOptions&, ExceptionState&);
    static PaymentRequest* create(ScriptState*, const Vector<String>& supportedMethods, const PaymentDetails&, const PaymentOptions&, const ScriptValue& data, ExceptionState&);

    virtual ~PaymentRequest();

    ScriptPromise show(ScriptState*);
    void abort(ExceptionState&);

    ShippingAddress* getShippingAddress() const { return m_shippingAddress.get(); }
    const String& shippingOption() const { return m_shippingOption; }

    DEFINE_ATTRIBUTE_EVENT_LISTENER(shippingaddresschange);
    DEFINE_ATTRIBUTE_EVENT_LISTENER(shippingoptionchange);

    // EventTargetWithInlineData:
    const AtomicString& interfaceName() const override;
    ExecutionContext* getExecutionContext() const override;

    // PaymentCompleter:
    ScriptPromise complete(ScriptState*, bool success) override;

    DECLARE_TRACE();

private:
    PaymentRequest(ScriptState*, const Vector<String>& supportedMethods, const PaymentDetails&, const PaymentOptions&, const ScriptValue& data, ExceptionState&);

    // mojom::wtf::PaymentRequestClient:
    void OnShippingAddressChange(mojom::wtf::ShippingAddressPtr) override;
    void OnShippingOptionChange(const String& shippingOptionId) override;
    void OnPaymentResponse(mojom::wtf::PaymentResponsePtr) override;
    void OnError() override;
    void OnComplete() override;

    // Clears the promise resolvers and closes the Mojo connection.
    void cleanUp();

    RefPtr<ScriptState> m_scriptState;
    Vector<String> m_supportedMethods;
    PaymentDetails m_details;
    PaymentOptions m_options;
    String m_stringifiedData;
    Member<ShippingAddress> m_shippingAddress;
    String m_shippingOption;
    Member<ScriptPromiseResolver> m_showResolver;
    Member<ScriptPromiseResolver> m_completeResolver;
    mojom::wtf::PaymentRequestPtr m_paymentProvider;
    mojo::Binding<mojom::wtf::PaymentRequestClient> m_clientBinding;
};

} // namespace blink

#endif // PaymentRequest_h
