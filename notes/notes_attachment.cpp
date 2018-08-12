// Copyright (c) 2013-2015 Vivaldi Technologies AS. All rights reserved

#include <string>
#include "base/base64.h"
#include "base/strings/string_number_conversions.h"
#include "base/values.h"
#include "base/json/json_string_value_serializer.h"

#include "notes/notes_attachment.h"
#include "notes/notes_codec.h"

namespace vivaldi {

base::Value *Notes_attachment::Encode(NotesCodec *checksummer) const {
  DCHECK(checksummer);

  base::DictionaryValue *attachment_value = new base::DictionaryValue();
  attachment_value->SetString("filename", filename);
  checksummer->UpdateChecksum(filename);
  attachment_value->SetString("content-type", content_type);
  checksummer->UpdateChecksum(content_type);
  attachment_value->SetString("content", content);
  checksummer->UpdateChecksum(content);

  return attachment_value;
}

bool Notes_attachment::Decode(const base::DictionaryValue *input,
                              NotesCodec *checksummer) {
  DCHECK(input);
  DCHECK(checksummer);

  if (!input->GetString("filename", &filename))
    return false;
  if (!input->GetString("content-type", &content_type))
    return false;
  if (!input->GetString("content", &content))
    return false;
  checksummer->UpdateChecksum(filename);
  checksummer->UpdateChecksum(content_type);
  checksummer->UpdateChecksum(content);

  return true;
}

bool Notes_attachment::GetContent(base::string16 *fname,
                                  base::string16 *cnt_type, std::string *cnt) {
  if (fname)
    *fname = filename;

  if (cnt_type)
    *cnt_type = content_type;

  if (cnt) {
    std::string temp;
    base::Base64Encode(content, &temp);
    if (!temp.empty())
      cnt->swap(temp);
    else
      return false;
  }
  return true;
}

}  // namespace vivaldi
