// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MASH_SCREENLOCK_SCREENLOCK_H_
#define MASH_SCREENLOCK_SCREENLOCK_H_

#include <map>
#include <memory>

#include "base/macros.h"
#include "mash/session/public/interfaces/session.mojom.h"
#include "mojo/public/cpp/bindings/binding_set.h"
#include "mojo/services/tracing/public/cpp/tracing_impl.h"
#include "mojo/shell/public/cpp/shell_client.h"

namespace views {
class AuraInit;
}

namespace mash {
namespace screenlock {

class Screenlock : public mojo::ShellClient,
                   public session::mojom::ScreenlockStateListener {
 public:
  Screenlock();
  ~Screenlock() override;

 private:
  // mojo::ShellClient:
  void Initialize(mojo::Connector* connector,
                  const mojo::Identity& identity,
                  uint32_t id) override;

  // session::mojom::ScreenlockStateListener:
  void ScreenlockStateChanged(bool locked) override;

  mojo::TracingImpl tracing_;
  std::unique_ptr<views::AuraInit> aura_init_;
  mojo::BindingSet<session::mojom::ScreenlockStateListener> bindings_;

  DISALLOW_COPY_AND_ASSIGN(Screenlock);
};

}  // namespace screenlock
}  // namespace mash

#endif  // MASH_SCREENLOCK_SCREENLOCK_H_
