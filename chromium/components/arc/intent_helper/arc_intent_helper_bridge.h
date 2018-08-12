// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_ARC_INTENT_HELPER_ARC_INTENT_HELPER_BRIDGE_H_
#define COMPONENTS_ARC_INTENT_HELPER_ARC_INTENT_HELPER_BRIDGE_H_

#include <memory>

#include "ash/link_handler_model_factory.h"
#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "components/arc/arc_bridge_service.h"
#include "components/arc/arc_service.h"
#include "components/arc/common/intent_helper.mojom.h"
#include "mojo/public/cpp/bindings/binding.h"

namespace ash {

class LinkHandlerModel;

}  // namespace ash

namespace arc {

class ActivityIconLoader;

// Receives intents from ARC.
class ArcIntentHelperBridge : public ArcService,
                              public ArcBridgeService::Observer,
                              public mojom::IntentHelperHost,
                              public ash::LinkHandlerModelFactory {
 public:
  explicit ArcIntentHelperBridge(ArcBridgeService* bridge_service);
  ~ArcIntentHelperBridge() override;

  // ArcBridgeService::Observer
  void OnIntentHelperInstanceReady() override;
  void OnIntentHelperInstanceClosed() override;

  // arc::IntentHelperHost
  void OnOpenDownloads() override;

  // arc::mojom::IntentHelperHost
  void OnIconInvalidated(const mojo::String& package_name) override;
  void OnOpenUrl(const mojo::String& url) override;

  // ash::LinkHandlerModelFactory
  std::unique_ptr<ash::LinkHandlerModel> CreateModel(const GURL& url) override;

 private:
  mojo::Binding<mojom::IntentHelperHost> binding_;
  scoped_refptr<ActivityIconLoader> icon_loader_;

  DISALLOW_COPY_AND_ASSIGN(ArcIntentHelperBridge);
};

}  // namespace arc

#endif  // COMPONENTS_ARC_INTENT_HELPER_ARC_INTENT_HELPER_BRIDGE_H_
