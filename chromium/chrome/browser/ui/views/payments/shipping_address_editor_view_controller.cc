// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/payments/shipping_address_editor_view_controller.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/location.h"
#include "base/memory/ptr_util.h"
#include "base/single_thread_task_runner.h"
#include "base/strings/string16.h"
#include "base/strings/utf_string_conversions.h"
#include "base/threading/thread_task_runner_handle.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/views/payments/payment_request_dialog_view.h"
#include "chrome/browser/ui/views/payments/validating_combobox.h"
#include "chrome/browser/ui/views/payments/validating_textfield.h"
#include "chrome/grit/generated_resources.h"
#include "components/autofill/core/browser/autofill_address_util.h"
#include "components/autofill/core/browser/autofill_country.h"
#include "components/autofill/core/browser/autofill_type.h"
#include "components/autofill/core/browser/country_combobox_model.h"
#include "components/autofill/core/browser/field_types.h"
#include "components/autofill/core/browser/personal_data_manager.h"
#include "components/autofill/core/browser/region_combobox_model.h"
#include "components/autofill/core/browser/validation.h"
#include "components/autofill/core/common/autofill_constants.h"
#include "components/payments/content/payment_request_state.h"
#include "components/strings/grit/components_strings.h"
#include "content/public/browser/web_contents.h"
#include "third_party/libaddressinput/messages.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/views/controls/textfield/textfield.h"

namespace payments {

namespace {

// Converts a field type in string format as returned by
// autofill::GetAddressComponents into the appropriate autofill::ServerFieldType
// enum.
autofill::ServerFieldType GetFieldTypeFromString(const std::string& type) {
  if (type == autofill::kFullNameField)
    return autofill::NAME_FULL;
  if (type == autofill::kCompanyNameField)
    return autofill::COMPANY_NAME;
  if (type == autofill::kAddressLineField)
    return autofill::ADDRESS_HOME_STREET_ADDRESS;
  if (type == autofill::kDependentLocalityField)
    return autofill::ADDRESS_HOME_DEPENDENT_LOCALITY;
  if (type == autofill::kCityField)
    return autofill::ADDRESS_HOME_CITY;
  if (type == autofill::kStateField)
    return autofill::ADDRESS_HOME_STATE;
  if (type == autofill::kPostalCodeField)
    return autofill::ADDRESS_HOME_ZIP;
  if (type == autofill::kSortingCodeField)
    return autofill::ADDRESS_HOME_SORTING_CODE;
  if (type == autofill::kCountryField)
    return autofill::ADDRESS_HOME_COUNTRY;
  NOTREACHED();
  return autofill::UNKNOWN_TYPE;
}

}  // namespace

ShippingAddressEditorViewController::ShippingAddressEditorViewController(
    PaymentRequestSpec* spec,
    PaymentRequestState* state,
    PaymentRequestDialogView* dialog,
    autofill::AutofillProfile* profile)
    : EditorViewController(spec, state, dialog),
      profile_to_edit_(profile),
      chosen_country_index_(0),
      failed_to_load_region_data_(false) {
  UpdateEditorFields();
}

ShippingAddressEditorViewController::~ShippingAddressEditorViewController() {}

std::unique_ptr<views::View>
ShippingAddressEditorViewController::CreateHeaderView() {
  return base::MakeUnique<views::View>();
}

std::vector<EditorField>
ShippingAddressEditorViewController::GetFieldDefinitions() {
  return editor_fields_;
}

base::string16 ShippingAddressEditorViewController::GetInitialValueForType(
    autofill::ServerFieldType type) {
  if (!profile_to_edit_)
    return base::string16();

  return profile_to_edit_->GetInfo(autofill::AutofillType(type),
                                   state()->GetApplicationLocale());
}

bool ShippingAddressEditorViewController::ValidateModelAndSave() {
  const std::string& locale = state()->GetApplicationLocale();
  // To validate the profile first, we use a temporary object.
  autofill::AutofillProfile profile;
  for (const auto& field : text_fields()) {
    // Force a blur in case the value was left untouched.
    field.first->OnBlur();
    // ValidatingTextfield* is the key, EditorField is the value.
    if (field.first->invalid())
      return false;

    profile.SetInfo(autofill::AutofillType(field.second.type),
                    field.first->text(), locale);
  }
  for (const auto& field : comboboxes()) {
    // ValidatingCombobox* is the key, EditorField is the value.
    ValidatingCombobox* combobox = field.first;
    if (combobox->invalid())
      return false;

    if (combobox->id() == autofill::ADDRESS_HOME_COUNTRY) {
      profile.SetInfo(
          autofill::AutofillType(field.second.type),
          base::UTF8ToUTF16(country_codes_[combobox->selected_index()]),
          locale);
    } else {
      profile.SetInfo(autofill::AutofillType(field.second.type),
                      combobox->GetTextForRow(combobox->selected_index()),
                      locale);
    }
  }

  if (!profile_to_edit_) {
    // Add the profile (will not add a duplicate).
    profile.set_origin(autofill::kSettingsOrigin);
    state()->GetPersonalDataManager()->AddProfile(profile);
  } else {
    // Copy the temporary object's data to the object to be edited. Prefer this
    // method to copying |profile| into |profile_to_edit_|, because the latter
    // object needs to retain other properties (use count, use date, guid,
    // etc.).
    for (const auto& field : text_fields()) {
      profile_to_edit_->SetInfo(
          autofill::AutofillType(field.second.type),
          profile.GetInfo(autofill::AutofillType(field.second.type), locale),
          locale);
    }
    for (const auto& field : comboboxes()) {
      profile_to_edit_->SetInfo(
          autofill::AutofillType(field.second.type),
          profile.GetInfo(autofill::AutofillType(field.second.type), locale),
          locale);
    }
    profile_to_edit_->set_origin(autofill::kSettingsOrigin);
    state()->GetPersonalDataManager()->UpdateProfile(*profile_to_edit_);
  }

  return true;
}

std::unique_ptr<ValidationDelegate>
ShippingAddressEditorViewController::CreateValidationDelegate(
    const EditorField& field) {
  return base::MakeUnique<
      ShippingAddressEditorViewController::ShippingAddressValidationDelegate>(
      this, field);
}

std::unique_ptr<ui::ComboboxModel>
ShippingAddressEditorViewController::GetComboboxModelForType(
    const autofill::ServerFieldType& type) {
  switch (type) {
    case autofill::ADDRESS_HOME_COUNTRY: {
      std::unique_ptr<autofill::CountryComboboxModel> model =
          base::MakeUnique<autofill::CountryComboboxModel>();
      model->SetCountries(*state()->GetPersonalDataManager(),
                          base::Callback<bool(const std::string&)>(),
                          state()->GetApplicationLocale());
      country_codes_.clear();
      for (size_t i = 0; i < model->countries().size(); ++i) {
        if (model->countries()[i].get())
          country_codes_.push_back(model->countries()[i]->country_code());
        else
          country_codes_.push_back("");  // Separator.
      }
      return std::move(model);
    }
    case autofill::ADDRESS_HOME_STATE: {
      std::unique_ptr<autofill::RegionComboboxModel> model =
          base::MakeUnique<autofill::RegionComboboxModel>(
              state()->GetAddressInputSource(),
              state()->GetAddressInputStorage(),
              state()->GetApplicationLocale(),
              country_codes_[chosen_country_index_]);
      // If the data was already pre-loaded, the observer won't get notified so
      // we have to check for failure here.
      if (!model->pending_region_data_load()) {
        failed_to_load_region_data_ = model->failed_to_load_data();
        if (failed_to_load_region_data_)
          OnDataChanged();
      }
      return std::move(model);
    }
    default:
      NOTREACHED();
      break;
  }
  return std::unique_ptr<ui::ComboboxModel>();
}

void ShippingAddressEditorViewController::OnPerformAction(
    views::Combobox* sender) {
  EditorViewController::OnPerformAction(sender);
  if (sender->id() != autofill::ADDRESS_HOME_COUNTRY)
    return;
  DCHECK_GE(sender->selected_index(), 0);
  if (chosen_country_index_ != static_cast<size_t>(sender->selected_index())) {
    chosen_country_index_ = sender->selected_index();
    failed_to_load_region_data_ = false;
    OnDataChanged();
  }
}

void ShippingAddressEditorViewController::UpdateEditorView() {
  EditorViewController::UpdateEditorView();
  if (chosen_country_index_ > 0UL) {
    views::Combobox* country_combo_box = static_cast<views::Combobox*>(
        dialog()->GetViewByID(autofill::ADDRESS_HOME_COUNTRY));
    DCHECK(country_combo_box);
    country_combo_box->SetSelectedIndex(chosen_country_index_);
  }
}

base::string16 ShippingAddressEditorViewController::GetSheetTitle() {
  return l10n_util::GetStringUTF16(
      IDS_PAYMENT_REQUEST_ADDRESS_EDITOR_ADD_TITLE);
}

void ShippingAddressEditorViewController::UpdateEditorFields() {
  editor_fields_.clear();
  std::string chosen_country_code;
  if (chosen_country_index_ < country_codes_.size())
    chosen_country_code = country_codes_[chosen_country_index_];

  std::unique_ptr<base::ListValue> components(new base::ListValue);
  std::string unused;
  autofill::GetAddressComponents(chosen_country_code,
                                 state()->GetApplicationLocale(),
                                 components.get(), &unused);
  for (size_t line_index = 0; line_index < components->GetSize();
       ++line_index) {
    const base::ListValue* line = nullptr;
    if (!components->GetList(line_index, &line)) {
      NOTREACHED();
      return;
    }
    DCHECK_NE(nullptr, line);
    for (size_t component_index = 0; component_index < line->GetSize();
         ++component_index) {
      const base::DictionaryValue* component = nullptr;
      if (!line->GetDictionary(component_index, &component)) {
        NOTREACHED();
        return;
      }
      std::string field_type;
      if (!component->GetString(autofill::kFieldTypeKey, &field_type)) {
        NOTREACHED();
        return;
      }
      std::string field_name;
      if (!component->GetString(autofill::kFieldNameKey, &field_name)) {
        NOTREACHED();
        return;
      }
      std::string field_length;
      if (!component->GetString(autofill::kFieldLengthKey, &field_length)) {
        NOTREACHED();
        return;
      }
      EditorField::LengthHint length_hint = EditorField::LengthHint::HINT_LONG;
      if (field_length == autofill::kShortField)
        length_hint = EditorField::LengthHint::HINT_SHORT;
      else
        DCHECK_EQ(autofill::kLongField, field_length);
      autofill::ServerFieldType server_field_type =
          GetFieldTypeFromString(field_type);
      EditorField::ControlType control_type =
          EditorField::ControlType::TEXTFIELD;
      if (server_field_type == autofill::ADDRESS_HOME_COUNTRY ||
          (server_field_type == autofill::ADDRESS_HOME_STATE &&
           !failed_to_load_region_data_)) {
        control_type = EditorField::ControlType::COMBOBOX;
      }
      editor_fields_.emplace_back(
          server_field_type, base::UTF8ToUTF16(field_name), length_hint,
          /*required=*/server_field_type != autofill::COMPANY_NAME,
          control_type);
      // Insert the Country combobox right after NAME_FULL.
      if (server_field_type == autofill::NAME_FULL) {
        editor_fields_.emplace_back(
            autofill::ADDRESS_HOME_COUNTRY,
            l10n_util::GetStringUTF16(
                IDS_LIBADDRESSINPUT_COUNTRY_OR_REGION_LABEL),
            EditorField::LengthHint::HINT_LONG, /*required=*/true,
            EditorField::ControlType::COMBOBOX);
      }
    }
  }
  // Always add phone number at the end.
  editor_fields_.emplace_back(
      autofill::PHONE_HOME_NUMBER,
      l10n_util::GetStringUTF16(IDS_AUTOFILL_FIELD_LABEL_PHONE),
      EditorField::LengthHint::HINT_LONG, /*required=*/false,
      EditorField::ControlType::TEXTFIELD);
}

void ShippingAddressEditorViewController::OnDataChanged() {
  // TODO(crbug.com/703764): save the current state so we can map it to the new
  // country fields as best we can.
  UpdateEditorFields();

  // The editor can't be updated while in the middle of a combobox event.
  base::ThreadTaskRunnerHandle::Get()->PostTask(
      FROM_HERE,
      base::Bind(&ShippingAddressEditorViewController::UpdateEditorView,
                 base::Unretained(this)));
}

void ShippingAddressEditorViewController::OnComboboxModelChanged(
    views::Combobox* combobox) {
  if (combobox->id() != autofill::ADDRESS_HOME_STATE)
    return;
  autofill::RegionComboboxModel* model =
      static_cast<autofill::RegionComboboxModel*>(combobox->model());
  if (model->pending_region_data_load())
    return;
  if (model->failed_to_load_data()) {
    failed_to_load_region_data_ = true;
    OnDataChanged();
  }
}

ShippingAddressEditorViewController::ShippingAddressValidationDelegate::
    ShippingAddressValidationDelegate(
        ShippingAddressEditorViewController* controller,
        const EditorField& field)
    : field_(field), controller_(controller) {}

ShippingAddressEditorViewController::ShippingAddressValidationDelegate::
    ~ShippingAddressValidationDelegate() {}

bool ShippingAddressEditorViewController::ShippingAddressValidationDelegate::
    ValidateTextfield(views::Textfield* textfield) {
  return ValidateValue(textfield->text());
}

bool ShippingAddressEditorViewController::ShippingAddressValidationDelegate::
    ValidateCombobox(views::Combobox* combobox) {
  return ValidateValue(combobox->GetTextForRow(combobox->selected_index()));
}

void ShippingAddressEditorViewController::ShippingAddressValidationDelegate::
    ComboboxModelChanged(views::Combobox* combobox) {
  controller_->OnComboboxModelChanged(combobox);
}

bool ShippingAddressEditorViewController::ShippingAddressValidationDelegate::
    ValidateValue(const base::string16& value) {
  if (!value.empty()) {
    if (field_.type == autofill::PHONE_HOME_NUMBER &&
        !autofill::IsValidPhoneNumber(
            value,
            controller_->country_codes_[controller_->chosen_country_index_])) {
      controller_->DisplayErrorMessageForField(
          field_, l10n_util::GetStringUTF16(
                      IDS_PAYMENTS_PHONE_INVALID_VALIDATION_MESSAGE));
      return false;
    }
    // As long as other field types are non-empty, they are valid.
    controller_->DisplayErrorMessageForField(field_, base::ASCIIToUTF16(""));
    return true;
  }

  bool is_required_valid = !field_.required;
  const base::string16 displayed_message =
      is_required_valid ? base::ASCIIToUTF16("")
                        : l10n_util::GetStringUTF16(
                              IDS_PAYMENTS_FIELD_REQUIRED_VALIDATION_MESSAGE);
  controller_->DisplayErrorMessageForField(field_, displayed_message);
  return is_required_valid;
}

}  // namespace payments
