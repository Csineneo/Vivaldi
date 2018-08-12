// Copyright (c) 2013-2014 Vivaldi Technologies AS. All rights reserved

#include <string>
#include <vector>
#include "base/base64.h"
#include "base/memory/ptr_util.h"
#include "base/strings/string_number_conversions.h"
#include "base/values.h"
#include "base/json/json_string_value_serializer.h"
#include "base/strings/utf_string_conversions.h"

#include "notes/notesnode.h"
#include "notes/notes_codec.h"

namespace vivaldi {

const int64_t Notes_Node::kInvalidSyncTransactionVersion = -1;

Notes_Node::Notes_Node(int64_t id)
    : type_(NOTE),
      creation_time_(
          base::Time::Now()),  // This will be overwritten if read from file.
      id_(id),
      sync_transaction_version_(kInvalidSyncTransactionVersion)
{}

Notes_Node::~Notes_Node() {}

base::Value *Notes_Node::Encode(NotesCodec *checksummer,
      const std::vector<const Notes_Node *> *extra_nodes) const
{
  DCHECK(checksummer);

  base::DictionaryValue *value = new base::DictionaryValue();

  std::string node_id = base::Int64ToString(id_);
  value->SetString("id", node_id);
  checksummer->UpdateChecksum(node_id);

  base::string16 subject = GetTitle();
  value->SetString("subject", subject);
  checksummer->UpdateChecksum(subject);

  std::string type;
  switch (type_) {
    case FOLDER:
      type = "folder";
      break;
    case NOTE:
      type = "note";
      break;
    case TRASH:
      type = "trash";
      break;
    case OTHER:
      type = "other";
      break;
    default:
      NOTREACHED();
      break;
  }
  value->SetString("type", type);
  checksummer->UpdateChecksum(type);

  std::string temp = base::Int64ToString(creation_time_.ToInternalValue());
  value->SetString("date_added", temp);

  if (type_ == FOLDER || type_ == TRASH || type_ == OTHER) {
    base::ListValue *children = new base::ListValue();

    for (int i = 0; i < child_count(); i++) {
      children->Append(base::WrapUnique(GetChild(i)->Encode(checksummer)));
    }
    if (extra_nodes) {
      for (auto it : *extra_nodes) {
        children->Append(base::WrapUnique(it->Encode(checksummer)));
      }
    }
    value->Set("children", children);
  } else {
    if (!filename_.empty()) {
      value->SetString("filename", filename_);
      checksummer->UpdateChecksum(filename_);
    }

    value->SetString("content", content_);
    checksummer->UpdateChecksum(content_);

    temp = url_.possibly_invalid_spec();
    value->SetString("url", temp);
    checksummer->UpdateChecksum(temp);

    if (!note_icon_.content.empty())
      value->Set("icon", note_icon_.Encode(checksummer));

    if (attachments_.size()) {
      base::ListValue *attachments = new base::ListValue();

      std::vector<Notes_attachment>::const_iterator item;
      for (item = attachments_.begin(); item < attachments_.end(); item++) {
        base::Value *val = item->Encode(checksummer);
        attachments->Append(base::WrapUnique(val));
      }

      value->Set("attachments", attachments);
    }
  }

  if (sync_transaction_version() !=
      kInvalidSyncTransactionVersion) {
    value->SetString(NotesCodec::kSyncTransactionVersion,
                     base::Int64ToString(sync_transaction_version()));
  }
  return value;
}

bool Notes_Node::Decode(
    const base::DictionaryValue &d_input,
    int64_t &max_node_id,
    NotesCodec *checksummer) {
  DCHECK(checksummer);

  base::string16 str;
  std::string id_string;
  int64_t id = 0;
  if (checksummer->ids_valid())
  {
    if (!d_input.GetString("id", &id_string) ||
      !base::StringToInt64(id_string, &id) ||
      checksummer->count_id(id) != 0) {
      checksummer->set_ids_valid(false);
    }
    else
    {
      id_ = id;
      checksummer->register_id(id);
    }
  }
  checksummer->UpdateChecksum(id_string);

  max_node_id = std::max(max_node_id, id);


  if (d_input.GetString("subject", &str))
  {
    SetTitle(str);
    checksummer->UpdateChecksum(str);
  }

  std::string str1;
  if (d_input.GetString("date_added", &str1))
  {
    int64_t temp_time;
    base::StringToInt64(str1, &temp_time);
    // If this is a new note do not refresh the time from disk.
    if (temp_time) {
      creation_time_ = base::Time::FromInternalValue(temp_time);
    }
  }
  else
  {
    creation_time_ = base::Time::Now(); // Or should this be 1970?
  }

  std::string temp;
  if (!d_input.GetString("type", &temp)) {
    NOTREACHED();  // We must have type!
    return false;
  }

  if (temp != "folder" && temp != "note" && temp != "trash" && temp != "other")
    return false;
  checksummer->UpdateChecksum(temp);

  if (temp == "note") {
    type_ = NOTE;

    if (d_input.GetString("filename", &filename_)) {
      checksummer->UpdateChecksum(filename_);
      // OK, load later
    }

    if(!d_input.GetString("content", &content_))
      return false;
    checksummer->UpdateChecksum(content_);

    base::string16 str;

    if (d_input.GetString("url", &str))
      url_ = GURL(str);
    checksummer->UpdateChecksum(url_.possibly_invalid_spec());

    const base::DictionaryValue *attachment = NULL;
    if (d_input.GetDictionary("icon", &attachment) && attachment)
    {
      if (!note_icon_.Decode(attachment, checksummer))
        return false;
    }

    const base::ListValue *attachments = NULL;
    if (d_input.GetList("attachments", &attachments) && attachments) {
      for (size_t i = 0; i < attachments->GetSize(); i++) {
        const base::DictionaryValue *d_att = NULL;
        if (attachments->GetDictionary(i, &d_att)) {
          Notes_attachment item;
          if (!item.Decode(d_att, checksummer))
            return false;
          attachments_.push_back(item);
        }
      }
    }
  } else {
    if (temp == "trash")
      type_ = TRASH;
    else if (temp == "other")
      type_ = OTHER;
    else
      type_ = FOLDER;

    const base::ListValue *children = NULL;

    if (!d_input.GetList("children", &children))
      return false;

    for (size_t i = 0; i < children->GetSize(); i++) {
      const base::DictionaryValue *item = NULL;

      if (!children->GetDictionary(i, &item))
        return false;

      std::unique_ptr<Notes_Node> child = base::MakeUnique<Notes_Node>(0);

      child->Decode(*item, max_node_id, checksummer);

      Add(std::move(child), child_count());
    }
  }

  int64_t sync_transaction_version = kInvalidSyncTransactionVersion;
  std::string sync_transaction_version_str;
  if (d_input.GetString(NotesCodec::kSyncTransactionVersion, &sync_transaction_version_str) &&
      !base::StringToInt64(sync_transaction_version_str,
                           &sync_transaction_version))
    return false;

  set_sync_transaction_version(sync_transaction_version);

  return true;
}

}  // namespace vivaldi
