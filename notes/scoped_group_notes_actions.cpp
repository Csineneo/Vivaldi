// Copyright (c) 2015-2017 Vivaldi Technologies AS. All rights reserved
// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "notes/scoped_group_notes_actions.h"

#include "notes/notes_model.h"

namespace vivaldi {

ScopedGroupNotesActions::ScopedGroupNotesActions(Notes_Model* model)
    : model_(model) {
  if (model_)
    model_->BeginGroupedChanges();
}

ScopedGroupNotesActions::~ScopedGroupNotesActions() {
  if (model_)
    model_->EndGroupedChanges();
}
}
