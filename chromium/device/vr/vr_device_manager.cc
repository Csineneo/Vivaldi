// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device/vr/vr_device_manager.h"

#include <utility>

#include "base/bind.h"
#include "base/memory/ptr_util.h"
#include "base/memory/singleton.h"
#include "build/build_config.h"
#include "device/vr/features.h"

#if defined(OS_ANDROID)
#include "device/vr/android/gvr/gvr_device_provider.h"
#endif

#if BUILDFLAG(ENABLE_OPENVR)
#include "device/vr/openvr/openvr_device_provider.h"
#endif

namespace device {

namespace {
VRDeviceManager* g_vr_device_manager = nullptr;
}

VRDeviceManager::VRDeviceManager()
    : vr_initialized_(false),
      keep_alive_(false),
      has_scheduled_poll_(false),
      has_activate_listeners_(false) {
// Register VRDeviceProviders for the current platform
#if defined(OS_ANDROID)
  RegisterProvider(base::MakeUnique<GvrDeviceProvider>());
#endif

#if BUILDFLAG(ENABLE_OPENVR)
  RegisterProvider(base::MakeUnique<OpenVRDeviceProvider>());
#endif
}

VRDeviceManager::VRDeviceManager(std::unique_ptr<VRDeviceProvider> provider)
    : vr_initialized_(false), keep_alive_(true), has_scheduled_poll_(false) {
  thread_checker_.DetachFromThread();
  RegisterProvider(std::move(provider));
  SetInstance(this);
}

VRDeviceManager::~VRDeviceManager() {
  DCHECK(thread_checker_.CalledOnValidThread());
  StopSchedulingPollEvents();
  g_vr_device_manager = nullptr;
}

VRDeviceManager* VRDeviceManager::GetInstance() {
  if (!g_vr_device_manager)
    g_vr_device_manager = new VRDeviceManager();
  return g_vr_device_manager;
}

void VRDeviceManager::SetInstance(VRDeviceManager* instance) {
  // Unit tests can create multiple instances but only one should exist at any
  // given time so g_vr_device_manager should only go from nullptr to
  // non-nullptr and vice versa.
  CHECK_NE(!!instance, !!g_vr_device_manager);
  g_vr_device_manager = instance;
}

bool VRDeviceManager::HasInstance() {
  // For testing. Checks to see if a VRDeviceManager instance is active.
  return !!g_vr_device_manager;
}

void VRDeviceManager::AddService(VRServiceImpl* service) {
  // Loop through any currently active devices and send Connected messages to
  // the service. Future devices that come online will send a Connected message
  // when they are created.
  DCHECK(thread_checker_.CalledOnValidThread());

  InitializeProviders();

  std::vector<VRDevice*> devices;
  for (const auto& provider : providers_) {
    provider->GetDevices(&devices);
  }

  for (auto* device : devices) {
    if (device->id() == VR_DEVICE_LAST_ID) {
      continue;
    }

    if (devices_.find(device->id()) == devices_.end()) {
      devices_[device->id()] = device;
    }

    service->ConnectDevice(device);
  }

  services_.insert(service);
}

void VRDeviceManager::RemoveService(VRServiceImpl* service) {

  if (service->listening_for_activate()) {
    ListeningForActivateChanged(false, service);
  }

  services_.erase(service);

  if (services_.empty() && !keep_alive_) {
    // Delete the device manager when it has no active connections.
    delete g_vr_device_manager;
  }
}

unsigned int VRDeviceManager::GetNumberOfConnectedDevices() {
  DCHECK(thread_checker_.CalledOnValidThread());

  return static_cast<unsigned int>(devices_.size());
}

void VRDeviceManager::ListeningForActivateChanged(bool listening,
                                                  VRServiceImpl* service) {
  DCHECK(thread_checker_.CalledOnValidThread());

  if (listening) {
    most_recently_listening_for_activate_ = service;
  }

  bool activate_listeners = listening;
  if (!activate_listeners) {
    for (auto* service : services_) {
      if (service->listening_for_activate()) {
        activate_listeners = true;
        break;
      }
    }
  }

  // Notify all the providers if this changes
  if (has_activate_listeners_ != activate_listeners) {
    has_activate_listeners_ = activate_listeners;
    for (const auto& provider : providers_)
      provider->SetListeningForActivate(has_activate_listeners_);
  }
}

bool VRDeviceManager::IsMostRecentlyListeningForActivate(
    VRServiceImpl* service) {
  if (!service)
    return false;
  return service == most_recently_listening_for_activate_;
}

VRDevice* VRDeviceManager::GetDevice(unsigned int index) {
  DCHECK(thread_checker_.CalledOnValidThread());

  if (index == 0) {
    return NULL;
  }

  DeviceMap::iterator iter = devices_.find(index);
  if (iter == devices_.end()) {
    return nullptr;
  }
  return iter->second;
}

void VRDeviceManager::InitializeProviders() {
  if (vr_initialized_) {
    return;
  }

  for (const auto& provider : providers_) {
    provider->Initialize();
  }

  vr_initialized_ = true;
}

void VRDeviceManager::RegisterProvider(
    std::unique_ptr<VRDeviceProvider> provider) {
  providers_.push_back(std::move(provider));
}

void VRDeviceManager::SchedulePollEvents() {
  if (has_scheduled_poll_)
    return;

  has_scheduled_poll_ = true;

  timer_.Start(FROM_HERE, base::TimeDelta::FromMilliseconds(500), this,
               &VRDeviceManager::PollEvents);
}

void VRDeviceManager::PollEvents() {
  for (const auto& provider : providers_)
    provider->PollEvents();
}

void VRDeviceManager::StopSchedulingPollEvents() {
  if (has_scheduled_poll_)
    timer_.Stop();
}

}  // namespace device
