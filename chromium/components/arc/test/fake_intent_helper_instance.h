// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_ARC_TEST_FAKE_INTENT_HELPER_INSTANCE_H_
#define COMPONENTS_ARC_TEST_FAKE_INTENT_HELPER_INSTANCE_H_

#include <string>
#include <vector>

#include "base/callback.h"
#include "components/arc/common/intent_helper.mojom.h"

namespace arc {

class FakeIntentHelperInstance : public mojom::IntentHelperInstance {
 public:
  FakeIntentHelperInstance();

  class Broadcast {
   public:
    Broadcast(const std::string& action,
              const std::string& package_name,
              const std::string& cls,
              const std::string& extras);

    ~Broadcast();

    Broadcast(const Broadcast& broadcast);

    std::string action;
    std::string package_name;
    std::string cls;
    std::string extras;
  };

  void clear_broadcasts() { broadcasts_.clear(); }

  const std::vector<Broadcast>& broadcasts() const { return broadcasts_; }

  // mojom::IntentHelperInstance:
  ~FakeIntentHelperInstance() override;

  void AddPreferredPackage(const std::string& package_name) override;

  void GetFileSize(const std::string& url,
                   const GetFileSizeCallback& callback) override;

  void HandleIntent(mojom::IntentInfoPtr intent,
                    mojom::ActivityNamePtr activity) override;

  void HandleUrl(const std::string& url,
                 const std::string& package_name) override;

  void HandleUrlList(std::vector<mojom::UrlWithMimeTypePtr> urls,
                     mojom::ActivityNamePtr activity,
                     mojom::ActionType action) override;

  void Init(mojom::IntentHelperHostPtr host_ptr) override;

  void OpenFileToRead(const std::string& url,
                      const OpenFileToReadCallback& callback) override;

  void RequestActivityIcons(
      std::vector<mojom::ActivityNamePtr> activities,
      ::arc::mojom::ScaleFactor scale_factor,
      const RequestActivityIconsCallback& callback) override;

  void RequestIntentHandlerList(
      mojom::IntentInfoPtr intent,
      const RequestIntentHandlerListCallback& callback) override;

  void RequestUrlHandlerList(
      const std::string& url,
      const RequestUrlHandlerListCallback& callback) override;

  void RequestUrlListHandlerList(
      std::vector<mojom::UrlWithMimeTypePtr> urls,
      const RequestUrlListHandlerListCallback& callback) override;

  void SendBroadcast(const std::string& action,
                     const std::string& package_name,
                     const std::string& cls,
                     const std::string& extras) override;

 private:
  std::vector<Broadcast> broadcasts_;

  DISALLOW_COPY_AND_ASSIGN(FakeIntentHelperInstance);
};

}  // namespace arc

#endif  // COMPONENTS_ARC_TEST_FAKE_INTENT_HELPER_INSTANCE_H_
