// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_OFFLINE_PAGES_OFFLINE_PAGE_MODEL_H_
#define COMPONENTS_OFFLINE_PAGES_OFFLINE_PAGE_MODEL_H_

#include <stdint.h>

#include <map>
#include <vector>

#include "base/callback.h"
#include "base/files/file_path.h"
#include "base/gtest_prod_util.h"
#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"
#include "base/memory/scoped_vector.h"
#include "base/memory/weak_ptr.h"
#include "base/observer_list.h"
#include "base/supports_user_data.h"
#include "components/keyed_service/core/keyed_service.h"
#include "components/offline_pages/offline_page_archiver.h"
#include "components/offline_pages/offline_page_metadata_store.h"

class GURL;
namespace base {
class SequencedTaskRunner;
class Time;
class TimeDelta;
}

namespace offline_pages {

static const char* const BOOKMARK_NAMESPACE = "bookmark";
static const int64_t INVALID_OFFLINE_ID = 0;

struct ClientId;

struct OfflinePageItem;
class OfflinePageMetadataStore;

// Service for saving pages offline, storing the offline copy and metadata, and
// retrieving them upon request.
//
// Example usage:
//   class ArchiverImpl : public OfflinePageArchiver {
//     // This is a class that knows how to create archiver
//     void CreateArchiver(...) override;
//     ...
//   }
//
//   // In code using the OfflinePagesModel to save a page:
//   scoped_ptr<ArchiverImpl> archiver(new ArchiverImpl());
//   // Callback is of type SavePageCallback.
//   model->SavePage(url, std::move(archiver), callback);
//
// TODO(fgorski): Things to describe:
// * how to cancel requests and what to expect
class OfflinePageModel : public KeyedService, public base::SupportsUserData {
 public:
  // Result of saving a page offline.
  // A Java counterpart will be generated for this enum.
  // GENERATED_JAVA_ENUM_PACKAGE: org.chromium.components.offlinepages
  enum class SavePageResult {
    SUCCESS,
    CANCELLED,
    DEVICE_FULL,
    CONTENT_UNAVAILABLE,
    ARCHIVE_CREATION_FAILED,
    STORE_FAILURE,
    ALREADY_EXISTS,
    // Certain pages, i.e. file URL or NTP, will not be saved because these
    // are already locally accisible.
    SKIPPED,
    // NOTE: always keep this entry at the end. Add new result types only
    // immediately above this line. Make sure to update the corresponding
    // histogram enum accordingly.
    RESULT_COUNT,
  };

  // Result of deleting an offline page.
  // A Java counterpart will be generated for this enum.
  // GENERATED_JAVA_ENUM_PACKAGE: org.chromium.components.offlinepages
  enum class DeletePageResult {
    SUCCESS,
    CANCELLED,
    STORE_FAILURE,
    DEVICE_FAILURE,
    NOT_FOUND,
    // NOTE: always keep this entry at the end. Add new result types only
    // immediately above this line. Make sure to update the corresponding
    // histogram enum accordingly.
    RESULT_COUNT,
  };

  // Result of loading all pages.
  enum class LoadResult {
    SUCCESS,
    CANCELLED,
    STORE_FAILURE,
  };

  // Observer of the OfflinePageModel.
  class Observer {
   public:
    // Invoked when the model has finished loading.
    virtual void OfflinePageModelLoaded(OfflinePageModel* model) = 0;

    // Invoked when the model is being updated, due to adding, removing or
    // updating an offline page.
    virtual void OfflinePageModelChanged(OfflinePageModel* model) = 0;

    // Invoked when an offline copy related to |offline_id| was deleted.
    // In can be invoked as a result of |CheckForExternalFileDeletion|, if a
    // deleted page is detected.
    virtual void OfflinePageDeleted(int64_t offline_id,
                                    const ClientId& client_id) = 0;

   protected:
    virtual ~Observer() {}
  };

  typedef base::Callback<void(SavePageResult, int64_t)> SavePageCallback;
  typedef base::Callback<void(DeletePageResult)> DeletePageCallback;

  // Generates a new offline id
  static int64_t GenerateOfflineId();

  // Returns true if an offline copy can be saved for the given URL.
  static bool CanSavePage(const GURL& url);

  static base::TimeDelta GetFinalDeletionDelayForTesting();

  // All blocking calls/disk access will happen on the provided |task_runner|.
  OfflinePageModel(scoped_ptr<OfflinePageMetadataStore> store,
                   const base::FilePath& archives_dir,
                   const scoped_refptr<base::SequencedTaskRunner>& task_runner);
  ~OfflinePageModel() override;

  void AddObserver(Observer* observer);
  void RemoveObserver(Observer* observer);

  // Attempts to save a page addressed by |url| offline. Requires that the model
  // is loaded.  Generates a new offline id and returns it.
  void SavePage(const GURL& url,
                const ClientId& client_id,
                scoped_ptr<OfflinePageArchiver> archiver,
                const SavePageCallback& callback);

  // Marks that the offline page related to the passed |offline_id| has been
  // accessed. Its access info, including last access time and access count,
  // will be updated. Requires that the model is loaded.
  void MarkPageAccessed(int64_t offline_id);

  // Marks that the offline page related to the passed |offline_id| was going
  // to be deleted. The deletion will occur in a short while. The undo can be
  // done before this. Requires that the model is loaded.
  void MarkPageForDeletion(int64_t offline_id,
                           const DeletePageCallback& callback);

  // Deletes an offline page related to the passed |offline_id|.
  void DeletePageByOfflineId(int64_t offline_id,
                             const DeletePageCallback& callback);

  // Deletes offline pages related to the passed |offline_ids|.
  void DeletePagesByOfflineId(const std::vector<int64_t>& offline_ids,
                              const DeletePageCallback& callback);

  // Marks pages for deletion. Actual delete is asynchronous.
  // Works even if the model isn't loaded.
  void MarkPagesForDeletion(const std::vector<int64_t>& offline_ids,
                            const DeletePageCallback& callback);

  // Wipes out all the data by deleting all saved files and clearing the store.
  void ClearAll(const base::Closure& callback);

  // Returns true if there're offline pages.
  bool HasOfflinePages() const;

  // Gets all available offline pages. Requires that the model is loaded.
  const std::vector<OfflinePageItem> GetAllPages() const;

  // Gets pages that should be removed to clean up storage. Requires that the
  // model is loaded.
  const std::vector<OfflinePageItem> GetPagesToCleanUp() const;

  // Gets all offline ids where the offline page has the matching client id
  // If |include_deleted| is true, include pages that are marked for deletion
  // but not actually deleted yet.  Default is false.
  const std::vector<int64_t> GetOfflineIdsForClientId(
      const ClientId& client_id,
      bool include_deleted = false) const;

  // Returns an offline page associated with a specified |offline_id|. nullptr
  // is returned if not found.
  const OfflinePageItem* GetPageByOfflineId(int64_t offline_id) const;

  // Returns an offline page that is stored as |offline_url|. A nullptr is
  // returned if not found.
  const OfflinePageItem* GetPageByOfflineURL(const GURL& offline_url) const;

  // Returns an offline page saved for |online_url|. A nullptr is returned if
  // not found.
  const OfflinePageItem* GetPageByOnlineURL(const GURL& online_url) const;

  // Checks that all of the offline pages have corresponding offline copies.
  // If a page is discovered to be missing an offline copy, its offline page
  // metadata will be removed and |OfflinePageDeleted| will be sent to model
  // observers.
  void CheckForExternalFileDeletion();

  // Reports the storage histograms related to total size of all stored offline
  // pages. Method is to be called after a page was saved or some pages are
  // deleted. In the latter case |reporting_after_delete| is set to true.
  // Caller is supposed to provide the current |total_space_bytes| on drive
  // where the pages are stored, as well as |free_space_bytes| after the
  // operation was taken. The method will report total size of all pages, and
  // percentage of size of pages as compared to total space and free space.
  void RecordStorageHistograms(int64_t total_space_bytes,
                               int64_t free_space_bytes,
                               bool reporting_after_delete);

  // Undo a deletion of a page.  Pages that are marked for deletion can
  // be restored prior to when they are actually deleted.
  void UndoPageDeletion(int64_t offline_id);

  // Methods for testing only:
  OfflinePageMetadataStore* GetStoreForTesting();

  bool is_loaded() const { return is_loaded_; }

 private:
  FRIEND_TEST_ALL_PREFIXES(OfflinePageModelTest, MarkPageForDeletion);

  typedef ScopedVector<OfflinePageArchiver> PendingArchivers;

  // Callback for ensuring archive directory is created.
  void OnEnsureArchivesDirCreatedDone();

  // Callback for loading pages from the offline page metadata store.
  void OnLoadDone(OfflinePageMetadataStore::LoadStatus load_status,
                  const std::vector<OfflinePageItem>& offline_pages);

  // Steps for saving a page offline.
  void OnCreateArchiveDone(const GURL& requested_url,
                           int64_t offline_id,
                           const ClientId& client_id,
                           const base::Time& start_time,
                           const SavePageCallback& callback,
                           OfflinePageArchiver* archiver,
                           OfflinePageArchiver::ArchiverResult result,
                           const GURL& url,
                           const base::FilePath& file_path,
                           int64_t file_size);
  void OnAddOfflinePageDone(OfflinePageArchiver* archiver,
                            const SavePageCallback& callback,
                            const OfflinePageItem& offline_page,
                            bool success);
  void InformSavePageDone(const SavePageCallback& callback,
                          SavePageResult result,
                          int64_t offline_id);
  void DeletePendingArchiver(OfflinePageArchiver* archiver);

  // Steps for deleting files and data for an offline page.
  void OnDeleteArchiveFilesDone(const std::vector<int64_t>& offline_ids,
                                const DeletePageCallback& callback,
                                const bool* success);
  void OnRemoveOfflinePagesDone(const std::vector<int64_t>& offline_ids,
                                const DeletePageCallback& callback,
                                bool success);
  void InformDeletePageDone(const DeletePageCallback& callback,
                            DeletePageResult result);

  void OnMarkPageAccesseDone(const OfflinePageItem& offline_page_item,
                             bool success);

  // Steps for marking an offline page for deletion that can be undone.
  void OnMarkPageForDeletionDone(const OfflinePageItem& offline_page_item,
                                 const DeletePageCallback& callback,
                                 bool success);
  void FinalizePageDeletion();

  // Steps for undoing an offline page deletion.
  void OnUndoOfflinePageDone(const OfflinePageItem& offline_page, bool success);

  // Callbacks for checking if offline pages are missing archive files.
  void OnFindPagesMissingArchiveFile(
      const std::vector<int64_t>* ids_of_pages_missing_archive_file);
  void OnRemoveOfflinePagesMissingArchiveFileDone(
      const std::vector<std::pair<int64_t, ClientId>>& offline_client_id_pairs,
      OfflinePageModel::DeletePageResult result);

  // Steps for clearing all.
  void OnRemoveAllFilesDoneForClearAll(const base::Closure& callback,
                                       DeletePageResult result);
  void OnResetStoreDoneForClearAll(const base::Closure& callback, bool success);
  void OnReloadStoreDoneForClearAll(
      const base::Closure& callback,
      OfflinePageMetadataStore::LoadStatus load_status,
      const std::vector<OfflinePageItem>& offline_pages);

  void CacheLoadedData(const std::vector<OfflinePageItem>& offline_pages);

  // Actually does the work of deleting, requires the model is loaded.
  void DoDeletePagesByOfflineId(const std::vector<int64_t>& offline_ids,
                                const DeletePageCallback& callback);

  // Persistent store for offline page metadata.
  scoped_ptr<OfflinePageMetadataStore> store_;

  // Location where all of the archive files will be stored.
  base::FilePath archives_dir_;

  // The observers.
  base::ObserverList<Observer> observers_;

  bool is_loaded_;

  // In memory copy of the offline page metadata, keyed by bookmark IDs.
  std::map<int64_t, OfflinePageItem> offline_pages_;

  scoped_refptr<base::SequencedTaskRunner> task_runner_;

  // Pending archivers owned by this model.
  PendingArchivers pending_archivers_;

  // Delayed tasks that should be invoked after the loading is done.
  std::vector<base::Closure> delayed_tasks_;

  base::WeakPtrFactory<OfflinePageModel> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(OfflinePageModel);
};

}  // namespace offline_pages

#endif  // COMPONENTS_OFFLINE_PAGES_OFFLINE_PAGE_MODEL_H_
