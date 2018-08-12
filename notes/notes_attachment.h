// Copyright (c) 2013-2017 Vivaldi Technologies AS. All rights reserved

#ifndef VIVALDI_NOTES_ATTACHMENT_H_
#define VIVALDI_NOTES_ATTACHMENT_H_

#include <string>
#include <vector>
#include "url/gurl.h"
#include "base/time/time.h"

namespace base {
class Value;
class DictionaryValue;
}  // namespace base

namespace vivaldi {

class NotesCodec;

struct Notes_attachment {
  base::string16 filename;

  base::string16 content_type;
  std::string content;

  base::Value *Encode(NotesCodec *checksummer) const;
  bool Decode(const base::DictionaryValue *, NotesCodec *checksummer);

  bool GetContent(base::string16 *fname,
    base::string16 *cnt_type,
    std::string *cnt);
};

typedef std::vector<Notes_attachment>  Notes_attachments;

}  // namespace vivaldi

#endif  // VIVALDI_NOTES_ATTACHMENT_H_
