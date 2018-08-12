// Copyright (c) 2013-2015 Vivaldi Technologies AS. All rights reserved

#ifndef NOTES_NOTESNODE_H_
#define NOTES_NOTESNODE_H_

#include <string>
#include <vector>
#include "base/strings/string16.h"
#include "base/synchronization/waitable_event.h"
#include "base/time/time.h"
#include "content/public/browser/notification_observer.h"
#include "ui/base/models/tree_node_model.h"

#include "notes/notes_attachment.h"

class Profile;

namespace base {
class Value;
class DictionaryValue;
class SequencedTaskRunner;
}

namespace vivaldi {
class NotesStorage;
class NotesLoadDetails;

class Notes_Node : public ui::TreeNode<Notes_Node> {
 public:
  enum Type { NOTE, FOLDER, OTHER, TRASH };

  static const int64_t kInvalidSyncTransactionVersion;

  explicit Notes_Node(int64_t id);
  ~Notes_Node() override;

  base::Value *Encode(NotesCodec *checksummer,
      const std::vector<const Notes_Node *> *extra_nodes=NULL) const;
  bool Decode(const base::DictionaryValue &input, int64_t &max_node_id,
              NotesCodec *checksummer);

  void SetType(Type type) { type_ = type; }
  Type type() const {return type_;}
  bool is_folder() const { return type_ == FOLDER || type_ == TRASH ||
                              type_ == OTHER; }
  bool is_note() const {return type_ == NOTE;}
  bool is_other() const {return type_ == OTHER;}
  bool is_trash() const { return type_ == TRASH; }

  // Returns an unique id for this node.
  // For notes nodes that are managed by the notes model, the IDs are
  // persisted across sessions.
  int64_t id() const { return id_; }
  void set_id(int64_t id) { id_ = id; }

  // Get the creation time for the node.
  base::Time GetCreationTime() const { return creation_time_; }
  base::string16 GetFilename() { return filename_; }

  const base::string16 &GetContent() const { return content_; }

  const GURL &GetURL() const { return url_; }

  const Notes_attachment &GetAttachment(int index) const {
    return attachments_[index];
  }
  const std::vector<Notes_attachment> &GetAttachments() const {
    return attachments_;
  }
  void SetContent(const base::string16 &content) { content_ = content; }
  void SetURL(const GURL &url) { url_ = url; }
  void SetCreationTime(const base::Time creation_time) {
    creation_time_ = creation_time;
  }

  void AddAttachment(const Notes_attachment &attachment) {
    attachments_.push_back(attachment);
  }
  void SetAttachments(const Notes_attachments &attachments) {
    attachments_ = attachments;
  }
  void DeleteAttachment(int index) {
    attachments_.erase(attachments_.begin() + index);
  }

  void set_sync_transaction_version(int64_t sync_transaction_version) {
    sync_transaction_version_ = sync_transaction_version;
  }
  int64_t sync_transaction_version() const {
    return sync_transaction_version_;
  }


 private:
  friend class Notes_Model;

  // Type of node, folder or note.
  Type type_;
  // Filename of attached content.
  base::string16 filename_;
  // Time of creation.
  base::Time creation_time_;
  // Time of modification.
  base::Time modified_time_;
  // Actual note text.
  base::string16 content_;
  // Attached URL.
  GURL url_;
  // Icon data.
  Notes_attachment note_icon_;
  // List of attached data.
  std::vector<Notes_attachment> attachments_;

  // The unique identifier for this node.
  int64_t id_;

  // The sync transaction version. Defaults to kInvalidSyncTransactionVersion.
  int64_t sync_transaction_version_;

  DISALLOW_COPY_AND_ASSIGN(Notes_Node);
};


}  // namespace vivaldi

#endif  // NOTES_NOTESNODE_H_
