// Copyright (c) 2017 Vivaldi Technologies AS. All rights reserved
//
// Based on code that is:
//
// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "contact_database.h"

#include <stdint.h>

#include <algorithm>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "base/command_line.h"
#include "base/containers/hash_tables.h"
#include "base/files/file_util.h"
#include "base/metrics/histogram_macros.h"
#include "base/numerics/safe_conversions.h"
#include "base/rand_util.h"
#include "base/strings/string_util.h"
#include "base/time/time.h"
#include "build/build_config.h"
#include "sql/meta_table.h"
#include "sql/statement.h"
#include "sql/transaction.h"

#if defined(OS_MACOSX) && !defined(OS_IOS)
#include "base/mac/mac_util.h"
#endif

namespace contact {

namespace {

// Current version number. We write databases at the "current" version number,
// but any previous version that can read the "compatible" one can make do with
// our database without *too* many bad effects.
const int kCurrentVersionNumber = 1;
const int kCompatibleVersionNumber = 1;

}  // namespace

ContactDatabase::~ContactDatabase() {}

sql::InitStatus ContactDatabase::Init(const base::FilePath& contact_db_name) {
  db_.set_histogram_tag("Contact");

  // Set the database page size to something a little larger to give us
  // better performance (we're typically seek rather than bandwidth limited).
  // This only has an effect before any tables have been created, otherwise
  // this is a NOP. Must be a power of 2 and a max of 8192.
  db_.set_page_size(4096);

  // Set the cache size. The page size, plus a little extra, times this
  // value, tells us how much memory the cache will use maximum.
  // 1000 * 4kB = 4MB
  db_.set_cache_size(1000);

  // Note that we don't set exclusive locking here. That's done by
  // BeginExclusiveMode below which is called later (we have to be in shared
  // mode to start out for the in-memory backend to read the data).

  if (!db_.Open(contact_db_name))
    return sql::INIT_FAILURE;

  // Wrap the rest of init in a tranaction. This will prevent the database from
  // getting corrupted if we crash in the middle of initialization or migration.
  sql::Transaction committer(&db_);
  if (!committer.Begin())
    return sql::INIT_FAILURE;

#if defined(OS_MACOSX) && !defined(OS_IOS)
  // Exclude the contact file from backups.
  base::mac::SetFileBackupExclusion(contact_db_name);
#endif

  // Prime the cache.
  db_.Preload();

  // Create the tables and indices.
  // NOTE: If you add something here, also add it to
  //       RecreateAllButStarAndURLTables.
  if (!meta_table_.Init(&db_, GetCurrentVersion(), kCompatibleVersionNumber))
    return sql::INIT_FAILURE;

  if (!CreateContactTable() || !CreateEmailTable() || !CreatePhonenumberTable())
    return sql::INIT_FAILURE;

  return committer.Commit() ? sql::INIT_OK : sql::INIT_FAILURE;
}

void ContactDatabase::BeginExclusiveMode() {
  // We can't use set_exclusive_locking() since that only has an effect before
  // the DB is opened.
  ignore_result(db_.Execute("PRAGMA locking_mode=EXCLUSIVE"));
}

// static
int ContactDatabase::GetCurrentVersion() {
  return kCurrentVersionNumber;
}

void ContactDatabase::BeginTransaction() {
  db_.BeginTransaction();
}

void ContactDatabase::CommitTransaction() {
  db_.CommitTransaction();
}

void ContactDatabase::RollbackTransaction() {
  // If Init() returns with a failure status, the Transaction created there will
  // be destructed and rolled back. ContactBackend might try to kill the
  // database after that, at which point it will try to roll back a non-existing
  // transaction. This will crash on a DCHECK. So transaction_nesting() is
  // checked first.
  if (db_.transaction_nesting())
    db_.RollbackTransaction();
}

void ContactDatabase::Vacuum() {
  DCHECK_EQ(0, db_.transaction_nesting())
      << "Can not have a transaction when vacuuming.";
  ignore_result(db_.Execute("VACUUM"));
}

void ContactDatabase::TrimMemory(bool aggressively) {
  db_.TrimMemory(aggressively);
}

bool ContactDatabase::Raze() {
  return db_.Raze();
}

sql::Connection& ContactDatabase::GetDB() {
  return db_;
}

}  // namespace contact
