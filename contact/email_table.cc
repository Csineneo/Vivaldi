// Copyright (c) 2017 Vivaldi Technologies AS. All rights reserved
//
// Based on code that is:
//
// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "contact/email_table.h"

#include <string>
#include <vector>
#include "base/strings/utf_string_conversions.h"
#include "contact/contact_type.h"
#include "sql/statement.h"

namespace contact {

namespace {

void FillEmailRow(sql::Statement& statement, EmailAddressRow* email_row) {
  int email_address_id = statement.ColumnInt(0);
  int contact_id = statement.ColumnInt(1);
  base::string16 email = statement.ColumnString16(2);
  std::string type = statement.ColumnString(3);
  bool trusted = statement.ColumnInt(4) == 1 ? true : false;
  bool is_default = statement.ColumnInt(5) == 1 ? true : false;
  bool obsolete = statement.ColumnInt(6) == 1 ? true : false;

  email_row->set_email_address_id(email_address_id);
  email_row->set_contact_id(contact_id);
  email_row->set_email_address(email);
  email_row->set_type(type);
  email_row->set_trusted(trusted);
  email_row->set_is_default(is_default);
  email_row->set_obsolete(obsolete);
}

// static
bool FillEmailVector(sql::Statement& statement, EmailAddressRows* emails) {
  if (!statement.is_valid())
    return false;

  while (statement.Step()) {
    EmailAddressRow email;
    FillEmailRow(statement, &email);
    emails->push_back(email);
  }

  return statement.Succeeded();
}

}  // namespace

EmailTable::EmailTable() {}

EmailTable::~EmailTable() {}

bool EmailTable::CreateEmailTable() {
  const char* name = "email_addresses";
  if (GetDB().DoesTableExist(name))
    return true;

  std::string sql;
  sql.append("CREATE TABLE ");
  sql.append(name);
  sql.append(
      "("
      "email_address_id INTEGER PRIMARY KEY AUTOINCREMENT,"
      // Using AUTOINCREMENT is for sync propose. Sync uses this |id| as an
      // unique key to identify the Email. If here did not use
      // AUTOINCREMEclNT, and Sync was not working somehow, a ROWID could be
      // deleted and re-used during this period. Once Sync come back, Sync would
      // use ROWIDs and timestamps to see if there are any updates need to be
      // synced. And sync
      //  will only see the new Email, but missed the deleted Email.
      "contact_id INTEGER,"
      "email LONGVARCHAR,"
      "type LONGVARCHAR,"
      "trusted INTEGER,"
      "is_default INTEGER,"
      "obsolete INTEGER,"
      "created INTEGER,"
      "last_modified INTEGER"
      ")");

  bool res = GetDB().Execute(sql.c_str());

  return res;
}

EmailAddressID EmailTable::AddEmailAddress(EmailAddressRow row) {
  sql::Statement statement(GetDB().GetCachedStatement(
      SQL_FROM_HERE,
      "INSERT INTO email_addresses "
      "(contact_id, email, type, trusted, is_default, obsolete, created, "
      "last_modified) "
      "VALUES (?, ?, ?, ?, ?, ?, ?, ?)"));

  statement.BindInt64(0, row.contact_id());
  statement.BindString16(1, row.email_address());
  statement.BindString(2, row.type());
  statement.BindInt(3, row.trusted() ? 1 : 0);
  statement.BindInt(4, row.is_default() ? 1 : 0);
  statement.BindInt(5, row.obsolete() ? 1 : 0);

  int created = base::Time().Now().ToInternalValue();
  statement.BindInt64(6, created);
  statement.BindInt64(7, created);

  if (!statement.Run()) {
    return 0;
  }
  return GetDB().GetLastInsertRowId();
}

bool EmailTable::UpdateEmailAddress(EmailAddressRow row) {
  sql::Statement statement(
      GetDB().GetCachedStatement(SQL_FROM_HERE,
                                 "UPDATE email_addresses SET \
        email=?, type=?, trusted=?, is_default=?, obsolete=?, last_modified=? \
        WHERE email_address_id=? and contact_id=?"));

  int modified = base::Time().Now().ToInternalValue();
  statement.BindString16(0, row.email_address());
  statement.BindString(1, row.type());
  statement.BindInt(2, row.trusted() ? 1 : 0);
  statement.BindInt(3, row.is_default() ? 1 : 0);
  statement.BindInt(4, row.obsolete() ? 1 : 0);
  statement.BindInt64(5, modified);
  statement.BindInt64(6, row.email_address_id());
  statement.BindInt64(7, row.contact_id());

  return statement.Run();
}

bool EmailTable::DeleteEmail(EmailAddressID email_id, ContactID contact_id) {
  sql::Statement statement(GetDB().GetCachedStatement(
      SQL_FROM_HERE,
      "DELETE from email_addresses WHERE email_address_id=? and contact_id=?"));
  statement.BindInt64(0, email_id);
  statement.BindInt64(1, contact_id);

  return statement.Run();
}

bool EmailTable::GetEmailsForContact(ContactID contact_id,
                                     EmailAddressRows* emails) {
  emails->clear();

  sql::Statement statement(
      GetDB().GetCachedStatement(SQL_FROM_HERE,
                                 "SELECT email_address_id, contact_id, email, "
                                 "type, trusted, is_default, obsolete "
                                 "FROM email_addresses WHERE contact_id=?"));
  statement.BindInt64(0, contact_id);
  return FillEmailVector(statement, emails);
}

bool EmailTable::GetAllEmailAddresses(EmailAddressRows* emails) {
  emails->clear();

  sql::Statement statement(
      GetDB().GetCachedStatement(SQL_FROM_HERE,
                                 "SELECT email_address_id, contact_id, email, "
                                 "type, trusted, is_default, obsolete "
                                 "FROM email_addresses"));
  return FillEmailVector(statement, emails);
}

bool EmailTable::DoesEmailAddressIdExist(EmailAddressID email_address_id,
                                         ContactID contact_id) {
  sql::Statement statement(GetDB().GetUniqueStatement(
      "select count(*) as count from email_addresses \
        WHERE email_address_id=? and contact_id=?"));
  statement.BindInt64(0, email_address_id);
  statement.BindInt64(1, contact_id);

  if (!statement.Step())
    return false;

  return statement.ColumnInt(0) == 1;
}

}  // namespace contact
