// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "modules/notifications/NotificationData.h"

#include "bindings/core/v8/ExceptionState.h"
#include "bindings/core/v8/SerializedScriptValue.h"
#include "bindings/core/v8/SerializedScriptValueFactory.h"
#include "core/dom/ExecutionContext.h"
#include "modules/notifications/Notification.h"
#include "modules/notifications/NotificationOptions.h"
#include "modules/vibration/NavigatorVibration.h"
#include "platform/weborigin/KURL.h"
#include "wtf/CurrentTime.h"

namespace blink {
namespace {

WebNotificationData::Direction toDirectionEnumValue(const String& direction)
{
    if (direction == "ltr")
        return WebNotificationData::DirectionLeftToRight;
    if (direction == "rtl")
        return WebNotificationData::DirectionRightToLeft;

    return WebNotificationData::DirectionAuto;
}

} // namespace

WebNotificationData createWebNotificationData(ExecutionContext* executionContext, const String& title, const NotificationOptions& options, ExceptionState& exceptionState)
{
    // If silent is true, the notification must not have a vibration pattern.
    if (options.hasVibrate() && options.silent()) {
        exceptionState.throwTypeError("Silent notifications must not specify vibration patterns.");
        return WebNotificationData();
    }

    // If renotify is true, the notification must have a tag.
    if (options.renotify() && options.tag().isEmpty()) {
        exceptionState.throwTypeError("Notifications which set the renotify flag must specify a non-empty tag.");
        return WebNotificationData();
    }

    WebNotificationData webData;

    webData.title = title;
    webData.direction = toDirectionEnumValue(options.dir());
    webData.lang = options.lang();
    webData.body = options.body();
    webData.tag = options.tag();

    KURL iconUrl;

    if (options.hasIcon() && !options.icon().isEmpty()) {
        iconUrl = executionContext->completeURL(options.icon());
        if (!iconUrl.isValid())
            iconUrl = KURL();
    }

    webData.icon = iconUrl;
    webData.vibrate = NavigatorVibration::sanitizeVibrationPattern(options.vibrate());
    webData.timestamp = options.hasTimestamp() ? static_cast<double>(options.timestamp()) : WTF::currentTimeMS();
    webData.renotify = options.renotify();
    webData.silent = options.silent();
    webData.requireInteraction = options.requireInteraction();

    if (options.hasData()) {
        RefPtr<SerializedScriptValue> serializedScriptValue = SerializedScriptValueFactory::instance().create(options.data().isolate(), options.data(), nullptr, exceptionState);
        if (exceptionState.hadException())
            return WebNotificationData();

        Vector<char> serializedData;
        serializedScriptValue->toWireBytes(serializedData);

        webData.data = serializedData;
    }

    Vector<WebNotificationAction> actions;

    const size_t maxActions = Notification::maxActions();
    for (const NotificationAction& action : options.actions()) {
        if (actions.size() >= maxActions)
            break;

        WebNotificationAction webAction;
        webAction.action = action.action();
        webAction.title = action.title();

        KURL iconUrl;
        if (action.hasIcon() && !action.icon().isEmpty()) {
            iconUrl = executionContext->completeURL(action.icon());
            if (!iconUrl.isValid())
                iconUrl = KURL();
        }
        webAction.icon = iconUrl;

        actions.append(webAction);
    }

    webData.actions = actions;

    return webData;
}

} // namespace blink
