// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/system/chromeos/power/power_status.h"

#include <algorithm>
#include <cmath>

#include "ash/shell.h"
#include "ash/shell_delegate.h"
#include "base/logging.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "chromeos/dbus/dbus_thread_manager.h"
#include "chromeos/dbus/power_manager_client.h"
#include "grit/ash_resources.h"
#include "grit/ash_strings.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/l10n/time_format.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/image/image.h"
#include "ui/gfx/image/image_skia_operations.h"

namespace ash {
namespace {

// Updates |proto| to ensure that its fields are consistent.
void SanitizeProto(power_manager::PowerSupplyProperties* proto) {
  DCHECK(proto);

  if (proto->battery_state() ==
      power_manager::PowerSupplyProperties_BatteryState_FULL)
    proto->set_battery_percent(100.0);

  if (!proto->is_calculating_battery_time()) {
    const bool on_line_power = proto->external_power() !=
        power_manager::PowerSupplyProperties_ExternalPower_DISCONNECTED;
    if ((on_line_power && proto->battery_time_to_full_sec() < 0) ||
        (!on_line_power && proto->battery_time_to_empty_sec() < 0))
      proto->set_is_calculating_battery_time(true);
  }
}

base::string16 GetBatteryTimeAccessibilityString(int hour, int min) {
  DCHECK(hour || min);
  if (hour && !min) {
    return ui::TimeFormat::Simple(ui::TimeFormat::FORMAT_DURATION,
                                  ui::TimeFormat::LENGTH_LONG,
                                  base::TimeDelta::FromHours(hour));
  }
  if (min && !hour) {
    return ui::TimeFormat::Simple(ui::TimeFormat::FORMAT_DURATION,
                                  ui::TimeFormat::LENGTH_LONG,
                                  base::TimeDelta::FromMinutes(min));
  }
  return l10n_util::GetStringFUTF16(
      IDS_ASH_STATUS_TRAY_BATTERY_TIME_ACCESSIBLE,
      ui::TimeFormat::Simple(ui::TimeFormat::FORMAT_DURATION,
                             ui::TimeFormat::LENGTH_LONG,
                             base::TimeDelta::FromHours(hour)),
      ui::TimeFormat::Simple(ui::TimeFormat::FORMAT_DURATION,
                             ui::TimeFormat::LENGTH_LONG,
                             base::TimeDelta::FromMinutes(min)));
}

int PowerSourceToMessageID(
    const power_manager::PowerSupplyProperties_PowerSource& source) {
  switch (source.port()) {
    case power_manager::PowerSupplyProperties_PowerSource_Port_UNKNOWN:
      return IDS_ASH_POWER_SOURCE_PORT_UNKNOWN;
    case power_manager::PowerSupplyProperties_PowerSource_Port_LEFT:
      return IDS_ASH_POWER_SOURCE_PORT_LEFT;
    case power_manager::PowerSupplyProperties_PowerSource_Port_RIGHT:
      return IDS_ASH_POWER_SOURCE_PORT_RIGHT;
    case power_manager::PowerSupplyProperties_PowerSource_Port_BACK:
      return IDS_ASH_POWER_SOURCE_PORT_BACK;
    case power_manager::PowerSupplyProperties_PowerSource_Port_FRONT:
      return IDS_ASH_POWER_SOURCE_PORT_FRONT;
    case power_manager::PowerSupplyProperties_PowerSource_Port_LEFT_FRONT:
      return IDS_ASH_POWER_SOURCE_PORT_LEFT_FRONT;
    case power_manager::PowerSupplyProperties_PowerSource_Port_LEFT_BACK:
      return IDS_ASH_POWER_SOURCE_PORT_LEFT_BACK;
    case power_manager::PowerSupplyProperties_PowerSource_Port_RIGHT_FRONT:
      return IDS_ASH_POWER_SOURCE_PORT_RIGHT_FRONT;
    case power_manager::PowerSupplyProperties_PowerSource_Port_RIGHT_BACK:
      return IDS_ASH_POWER_SOURCE_PORT_RIGHT_BACK;
    case power_manager::PowerSupplyProperties_PowerSource_Port_BACK_LEFT:
      return IDS_ASH_POWER_SOURCE_PORT_BACK_LEFT;
    case power_manager::PowerSupplyProperties_PowerSource_Port_BACK_RIGHT:
      return IDS_ASH_POWER_SOURCE_PORT_BACK_RIGHT;
  }
  NOTREACHED();
  return 0;
}

static PowerStatus* g_power_status = NULL;

// Minimum battery percentage rendered in UI.
const int kMinBatteryPercent = 1;

// Width and height of battery images.
const int kBatteryImageHeight = 25;
const int kBatteryImageWidth = 25;

// Number of different power states.
const int kNumPowerImages = 15;

}  // namespace

const int PowerStatus::kMaxBatteryTimeToDisplaySec = 24 * 60 * 60;

// static
void PowerStatus::Initialize() {
  CHECK(!g_power_status);
  g_power_status = new PowerStatus();
}

// static
void PowerStatus::Shutdown() {
  CHECK(g_power_status);
  delete g_power_status;
  g_power_status = NULL;
}

// static
bool PowerStatus::IsInitialized() {
  return g_power_status != NULL;
}

// static
PowerStatus* PowerStatus::Get() {
  CHECK(g_power_status) << "PowerStatus::Get() called before Initialize().";
  return g_power_status;
}

// static
bool PowerStatus::ShouldDisplayBatteryTime(const base::TimeDelta& time) {
  return time >= base::TimeDelta::FromMinutes(1) &&
      time.InSeconds() <= kMaxBatteryTimeToDisplaySec;
}

// static
void PowerStatus::SplitTimeIntoHoursAndMinutes(const base::TimeDelta& time,
                                               int* hours,
                                               int* minutes) {
  DCHECK(hours);
  DCHECK(minutes);
  const int total_minutes = static_cast<int>(time.InSecondsF() / 60 + 0.5);
  *hours = total_minutes / 60;
  *minutes = total_minutes % 60;
}

void PowerStatus::AddObserver(Observer* observer) {
  DCHECK(observer);
  observers_.AddObserver(observer);
}

void PowerStatus::RemoveObserver(Observer* observer) {
  DCHECK(observer);
  observers_.RemoveObserver(observer);
}

void PowerStatus::RequestStatusUpdate() {
  chromeos::DBusThreadManager::Get()->GetPowerManagerClient()->
      RequestStatusUpdate();
}

void PowerStatus::SetPowerSource(const std::string& id) {
  chromeos::DBusThreadManager::Get()->GetPowerManagerClient()->SetPowerSource(
      id);
}

bool PowerStatus::IsBatteryPresent() const {
  return proto_.battery_state() !=
      power_manager::PowerSupplyProperties_BatteryState_NOT_PRESENT;
}

bool PowerStatus::IsBatteryFull() const {
  return proto_.battery_state() ==
      power_manager::PowerSupplyProperties_BatteryState_FULL;
}

bool PowerStatus::IsBatteryCharging() const {
  return proto_.battery_state() ==
      power_manager::PowerSupplyProperties_BatteryState_CHARGING;
}

bool PowerStatus::IsBatteryDischargingOnLinePower() const {
  return IsLinePowerConnected() && proto_.battery_state() ==
      power_manager::PowerSupplyProperties_BatteryState_DISCHARGING;
}

double PowerStatus::GetBatteryPercent() const {
  return proto_.battery_percent();
}

int PowerStatus::GetRoundedBatteryPercent() const {
  return std::max(kMinBatteryPercent,
      static_cast<int>(GetBatteryPercent() + 0.5));
}

bool PowerStatus::IsBatteryTimeBeingCalculated() const {
  return proto_.is_calculating_battery_time();
}

base::TimeDelta PowerStatus::GetBatteryTimeToEmpty() const {
  return base::TimeDelta::FromSeconds(proto_.battery_time_to_empty_sec());
}

base::TimeDelta PowerStatus::GetBatteryTimeToFull() const {
  return base::TimeDelta::FromSeconds(proto_.battery_time_to_full_sec());
}

bool PowerStatus::IsLinePowerConnected() const {
  return proto_.external_power() !=
      power_manager::PowerSupplyProperties_ExternalPower_DISCONNECTED;
}

bool PowerStatus::IsMainsChargerConnected() const {
  return proto_.external_power() ==
      power_manager::PowerSupplyProperties_ExternalPower_AC;
}

bool PowerStatus::IsUsbChargerConnected() const {
  return proto_.external_power() ==
      power_manager::PowerSupplyProperties_ExternalPower_USB;
}

bool PowerStatus::SupportsDualRoleDevices() const {
  return proto_.supports_dual_role_devices();
}

bool PowerStatus::HasDualRoleDevices() const {
  if (!SupportsDualRoleDevices())
    return false;

  for (int i = 0; i < proto_.available_external_power_source_size(); i++) {
    if (!proto_.available_external_power_source(i).active_by_default())
      return true;
  }
  return false;
}

std::vector<PowerStatus::PowerSource> PowerStatus::GetPowerSources() const {
  std::vector<PowerSource> sources;
  for (int i = 0; i < proto_.available_external_power_source_size(); i++) {
    const auto& source = proto_.available_external_power_source(i);
    sources.push_back(
        {source.id(),
         source.active_by_default() ? DEDICATED_CHARGER : DUAL_ROLE_USB,
         PowerSourceToMessageID(source)});
  }
  return sources;
}

std::string PowerStatus::GetCurrentPowerSourceID() const {
  return proto_.external_power_source_id();
}

PowerStatus::BatteryImageInfo PowerStatus::GetBatteryImageInfo(
    IconSet icon_set) const {
  BatteryImageInfo info;

  if (IsUsbChargerConnected()) {
    info.resource_id =
        (icon_set == ICON_DARK)
            ? IDR_AURA_UBER_TRAY_POWER_SMALL_CHARGING_UNRELIABLE_DARK
            : IDR_AURA_UBER_TRAY_POWER_SMALL_CHARGING_UNRELIABLE;
  } else {
    info.resource_id = (icon_set == ICON_DARK)
                           ? IDR_AURA_UBER_TRAY_POWER_SMALL_DARK
                           : IDR_AURA_UBER_TRAY_POWER_SMALL;
  }

  info.offset = IsUsbChargerConnected() ? 0 : (IsLinePowerConnected() ? 1 : 0);

  if (GetBatteryPercent() >= 100.0) {
    info.index = kNumPowerImages - 1;
  } else if (!IsBatteryPresent()) {
    info.index = kNumPowerImages;
  } else {
    info.index =
        static_cast<int>(GetBatteryPercent() / 100.0 * (kNumPowerImages - 1));
    info.index = std::max(std::min(info.index, kNumPowerImages - 2), 0);
  }

  return info;
}

gfx::ImageSkia PowerStatus::GetBatteryImage(IconSet icon_set) const {
  const BatteryImageInfo info = GetBatteryImageInfo(icon_set);
  gfx::Image all;
  all = ui::ResourceBundle::GetSharedInstance().GetImageNamed(info.resource_id);
  gfx::Rect region(info.offset * kBatteryImageWidth,
                   info.index * kBatteryImageHeight, kBatteryImageWidth,
                   kBatteryImageHeight);
  return gfx::ImageSkiaOperations::ExtractSubset(*all.ToImageSkia(), region);
}

base::string16 PowerStatus::GetAccessibleNameString(
    bool full_description) const {
  ui::ResourceBundle& rb = ui::ResourceBundle::GetSharedInstance();
  if (IsBatteryFull()) {
    return rb.GetLocalizedString(
        IDS_ASH_STATUS_TRAY_BATTERY_FULL_CHARGE_ACCESSIBLE);
  }

  base::string16 battery_percentage_accessible = l10n_util::GetStringFUTF16(
      IsBatteryCharging() ?
      IDS_ASH_STATUS_TRAY_BATTERY_PERCENT_CHARGING_ACCESSIBLE :
      IDS_ASH_STATUS_TRAY_BATTERY_PERCENT_ACCESSIBLE,
      base::IntToString16(GetRoundedBatteryPercent()));
  if (!full_description)
    return battery_percentage_accessible;

  base::string16 battery_time_accessible = base::string16();
  const base::TimeDelta time = IsBatteryCharging() ? GetBatteryTimeToFull() :
      GetBatteryTimeToEmpty();

  if (IsUsbChargerConnected()) {
    battery_time_accessible = rb.GetLocalizedString(
        IDS_ASH_STATUS_TRAY_BATTERY_CHARGING_UNRELIABLE_ACCESSIBLE);
  } else if (IsBatteryTimeBeingCalculated()) {
    battery_time_accessible = rb.GetLocalizedString(
        IDS_ASH_STATUS_TRAY_BATTERY_CALCULATING_ACCESSIBLE);
  } else if (ShouldDisplayBatteryTime(time) &&
             !IsBatteryDischargingOnLinePower()) {
    int hour = 0, min = 0;
    PowerStatus::SplitTimeIntoHoursAndMinutes(time, &hour, &min);
    base::string16 minute = min < 10 ?
        base::ASCIIToUTF16("0") + base::IntToString16(min) :
        base::IntToString16(min);
    battery_time_accessible =
        l10n_util::GetStringFUTF16(
            IsBatteryCharging() ?
            IDS_ASH_STATUS_TRAY_BATTERY_TIME_UNTIL_FULL_ACCESSIBLE :
            IDS_ASH_STATUS_TRAY_BATTERY_TIME_LEFT_ACCESSIBLE,
            GetBatteryTimeAccessibilityString(hour, min));
  }
  return battery_time_accessible.empty() ?
      battery_percentage_accessible :
      battery_percentage_accessible + base::ASCIIToUTF16(". ") +
      battery_time_accessible;
}

PowerStatus::PowerStatus() {
  chromeos::DBusThreadManager::Get()->GetPowerManagerClient()->
      AddObserver(this);
  chromeos::DBusThreadManager::Get()->GetPowerManagerClient()->
      RequestStatusUpdate();
}

PowerStatus::~PowerStatus() {
  chromeos::DBusThreadManager::Get()->GetPowerManagerClient()->
      RemoveObserver(this);
}

void PowerStatus::SetProtoForTesting(
    const power_manager::PowerSupplyProperties& proto) {
  proto_ = proto;
  SanitizeProto(&proto_);
}

void PowerStatus::PowerChanged(
    const power_manager::PowerSupplyProperties& proto) {
  proto_ = proto;
  SanitizeProto(&proto_);
  FOR_EACH_OBSERVER(Observer, observers_, OnPowerStatusChanged());
}

}  // namespace ash
