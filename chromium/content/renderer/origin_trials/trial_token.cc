// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/renderer/origin_trials/trial_token.h"

#include <openssl/curve25519.h>

#include <vector>

#include "base/base64.h"
#include "base/macros.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/time/time.h"
#include "url/origin.h"

namespace content {

namespace {

// Version 1 is the only token version currently supported
const uint8_t kVersion1 = 1;

const char* kFieldSeparator = "|";

}  // namespace

TrialToken::~TrialToken() {}

scoped_ptr<TrialToken> TrialToken::Parse(const std::string& token_text) {
  if (token_text.empty()) {
    return nullptr;
  }

  // Extract the version from the token. The version must be the first part of
  // the token, separated from the remainder, as:
  // version|<version-specific contents>
  size_t version_end = token_text.find(kFieldSeparator);
  if (version_end == std::string::npos) {
    return nullptr;
  }

  std::string version_string = token_text.substr(0, version_end);
  unsigned int version = 0;
  if (!base::StringToUint(version_string, &version) || version > UINT8_MAX) {
    return nullptr;
  }

  // Only version 1 currently supported
  if (version != kVersion1) {
    return nullptr;
  }

  // Extract the version-specific contents of the token
  std::string token_contents = token_text.substr(version_end + 1);

  // The contents of a valid version 1 token should resemble:
  // signature|origin|feature_name|expiry_timestamp
  std::vector<std::string> parts =
      SplitString(token_contents, kFieldSeparator, base::KEEP_WHITESPACE,
                  base::SPLIT_WANT_ALL);
  if (parts.size() != 4) {
    return nullptr;
  }

  const std::string& signature = parts[0];
  const std::string& origin_string = parts[1];
  const std::string& feature_name = parts[2];
  const std::string& expiry_string = parts[3];

  uint64_t expiry_timestamp;
  if (!base::StringToUint64(expiry_string, &expiry_timestamp)) {
    return nullptr;
  }

  // Ensure that the origin is a valid (non-unique) origin URL
  GURL origin_url(origin_string);
  if (url::Origin(origin_url).unique()) {
    return nullptr;
  }

  // Signed data is (origin + "|" + feature_name + "|" + expiry).
  std::string data = token_contents.substr(signature.length() + 1);

  return make_scoped_ptr(new TrialToken(version, signature, data, origin_url,
                                        feature_name, expiry_timestamp));
}

TrialToken::TrialToken(uint8_t version,
                       const std::string& signature,
                       const std::string& data,
                       const GURL& origin,
                       const std::string& feature_name,
                       uint64_t expiry_timestamp)
    : version_(version),
      signature_(signature),
      data_(data),
      origin_(origin),
      feature_name_(feature_name),
      expiry_timestamp_(expiry_timestamp) {}

bool TrialToken::IsAppropriate(const std::string& origin,
                               const std::string& feature_name) const {
  return ValidateOrigin(origin) && ValidateFeatureName(feature_name);
}

bool TrialToken::IsValid(const base::Time& now,
                         const base::StringPiece& public_key) const {
  // TODO(iclelland): Allow for multiple signing keys, and iterate over all
  // active keys here. https://crbug.com/543220
  return ValidateDate(now) && ValidateSignature(public_key);
}

bool TrialToken::ValidateOrigin(const std::string& origin) const {
  return GURL(origin) == origin_;
}

bool TrialToken::ValidateFeatureName(const std::string& feature_name) const {
  return feature_name == feature_name_;
}

bool TrialToken::ValidateDate(const base::Time& now) const {
  base::Time expiry_time = base::Time::FromDoubleT((double)expiry_timestamp_);
  return expiry_time > now;
}

bool TrialToken::ValidateSignature(const base::StringPiece& public_key) const {
  return ValidateSignature(signature_, data_, public_key);
}

// static
bool TrialToken::ValidateSignature(const std::string& signature_text,
                                   const std::string& data,
                                   const base::StringPiece& public_key) {
  // Public key must be 32 bytes long for Ed25519.
  CHECK_EQ(public_key.length(), 32UL);

  std::string signature;
  // signature_text is base64-encoded; decode first.
  if (!base::Base64Decode(signature_text, &signature)) {
    return false;
  }

  // Signature must be 64 bytes long
  if (signature.length() != 64) {
    return false;
  }

  int result = ED25519_verify(
      reinterpret_cast<const uint8_t*>(data.data()), data.length(),
      reinterpret_cast<const uint8_t*>(signature.data()),
      reinterpret_cast<const uint8_t*>(public_key.data()));
  return (result != 0);
}

}  // namespace content
