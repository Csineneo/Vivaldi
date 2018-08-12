#!/usr/bin/python
# Copyright (c) 2015 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Certificate chain with 1 intermediate, but the trust anchor used is
incorrect (neither subject nor signature matches). Verification is expected to
fail."""

import common

# Self-signed root certificate, which is NOT saved as the trust anchor.
root = common.create_self_signed_root_certificate('Root')

# Intermediate certificate.
intermediate = common.create_intermediate_certificate('Intermediate', root)

# Target certificate.
target = common.create_end_entity_certificate('Target', intermediate)

# Self-signed root certificate, not part of chain, which is saved as trust
# anchor.
bogus_root = common.create_self_signed_root_certificate('BogusRoot')

chain = [target, intermediate]
trusted = common.TrustAnchor(bogus_root, constrained=False)
time = common.DEFAULT_TIME
key_purpose = common.DEFAULT_KEY_PURPOSE
verify_result = False
errors = """----- Certificate i=1 (CN=Intermediate) -----
ERROR: Signature verification failed
ERROR: VerifySignedData failed
ERROR: subject does not match issuer

"""

common.write_test_file(__doc__, chain, trusted, time, key_purpose,
                       verify_result, errors)
