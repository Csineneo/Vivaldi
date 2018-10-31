// Copyright (c) 2018 Vivaldi Technologies AS. All rights reserved

#ifndef UI_LAZY_LOAD_SERVICE_FACTORY_H_
#define UI_LAZY_LOAD_SERVICE_FACTORY_H_

#include "base/macros.h"
#include "base/memory/singleton.h"
#include "components/keyed_service/content/browser_context_keyed_service_factory.h"

class Profile;

namespace vivaldi {

class LazyLoadService;

class LazyLoadServiceFactory : public BrowserContextKeyedServiceFactory {
 public:
  static LazyLoadService* GetForProfile(Profile* profile);
  static LazyLoadServiceFactory* GetInstance();

 private:
  friend struct base::DefaultSingletonTraits<LazyLoadServiceFactory>;

  LazyLoadServiceFactory();
  ~LazyLoadServiceFactory() override;

  // BrowserContextKeyedBaseFactory methods:
  KeyedService* BuildServiceInstanceFor(
      content::BrowserContext* context) const override;

  DISALLOW_COPY_AND_ASSIGN(LazyLoadServiceFactory);
};

}  // namespace vivaldi

#endif  // UI_LAZY_LOAD_SERVICE_FACTORY_H_
