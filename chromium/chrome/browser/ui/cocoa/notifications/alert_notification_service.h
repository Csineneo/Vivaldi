// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_COCOA_NOTIFICATIONS_BANNER_NOTIFICATION_SERVICE_H_
#define CHROME_BROWSER_UI_COCOA_NOTIFICATIONS_BANNER_NOTIFICATION_SERVICE_H_

#import <Foundation/Foundation.h>

#import "chrome/browser/ui/cocoa/notifications/notification_delivery.h"

// Implementation of the NotificationDelivery protocol that can display
// notifications of type alert.
@interface AlertNotificationService : NSObject<NotificationDelivery>
@end

#endif  // CHROME_BROWSER_UI_COCOA_NOTIFICATIONS_BANNER_NOTIFICATION_SERVICE_H_
