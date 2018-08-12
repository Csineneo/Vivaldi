// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/android/offline_pages/offline_page_bridge.h"

#include <memory>
#include <utility>

#include "base/android/jni_array.h"
#include "base/android/jni_string.h"
#include "chrome/browser/android/offline_pages/offline_page_mhtml_archiver.h"
#include "chrome/browser/android/offline_pages/offline_page_model_factory.h"
#include "chrome/browser/android/offline_pages/offline_page_utils.h"
#include "chrome/browser/android/offline_pages/request_coordinator_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_android.h"
#include "components/offline_pages/background/request_coordinator.h"
#include "components/offline_pages/background/save_page_request.h"
#include "components/offline_pages/offline_page_feature.h"
#include "components/offline_pages/offline_page_item.h"
#include "components/offline_pages/offline_page_model.h"
#include "components/offline_pages/proto/offline_pages.pb.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/web_contents.h"
#include "jni/OfflinePageBridge_jni.h"
#include "net/base/filename_util.h"

using base::android::ConvertJavaStringToUTF8;
using base::android::ConvertUTF8ToJavaString;
using base::android::ScopedJavaGlobalRef;
using base::android::ScopedJavaLocalRef;

namespace offline_pages {
namespace android {

namespace {

const char kOfflinePageBridgeKey[] = "offline-page-bridge";

void ToJavaOfflinePageList(JNIEnv* env,
                           jobject j_result_obj,
                           const std::vector<OfflinePageItem>& offline_pages) {
  for (const OfflinePageItem& offline_page : offline_pages) {
    Java_OfflinePageBridge_createOfflinePageAndAddToList(
        env, j_result_obj,
        ConvertUTF8ToJavaString(env, offline_page.url.spec()).obj(),
        offline_page.offline_id,
        ConvertUTF8ToJavaString(env, offline_page.client_id.name_space).obj(),
        ConvertUTF8ToJavaString(env, offline_page.client_id.id).obj(),
        ConvertUTF8ToJavaString(env, offline_page.GetOfflineURL().spec()).obj(),
        offline_page.file_size, offline_page.creation_time.ToJavaTime(),
        offline_page.access_count, offline_page.last_access_time.ToJavaTime());
  }
}

ScopedJavaLocalRef<jobject> ToJavaOfflinePageItem(
    JNIEnv* env,
    const OfflinePageItem& offline_page) {
  return Java_OfflinePageBridge_createOfflinePageItem(
      env, ConvertUTF8ToJavaString(env, offline_page.url.spec()).obj(),
      offline_page.offline_id,
      ConvertUTF8ToJavaString(env, offline_page.client_id.name_space).obj(),
      ConvertUTF8ToJavaString(env, offline_page.client_id.id).obj(),
      ConvertUTF8ToJavaString(env, offline_page.GetOfflineURL().spec()).obj(),
      offline_page.file_size, offline_page.creation_time.ToJavaTime(),
      offline_page.access_count, offline_page.last_access_time.ToJavaTime());
}

void CheckPagesExistOfflineCallback(
    const ScopedJavaGlobalRef<jobject>& j_callback_obj,
    const OfflinePageModel::CheckPagesExistOfflineResult& offline_pages) {
  JNIEnv* env = base::android::AttachCurrentThread();

  std::vector<std::string> offline_pages_vector;
  for (const GURL& page : offline_pages)
    offline_pages_vector.push_back(page.spec());

  ScopedJavaLocalRef<jobjectArray> j_result_array =
      base::android::ToJavaArrayOfStrings(env, offline_pages_vector);
  DCHECK(j_result_array.obj());

  Java_CheckPagesExistOfflineCallbackInternal_onResult(
      env, j_callback_obj.obj(), j_result_array.obj());
}

void GetAllPagesCallback(
    const ScopedJavaGlobalRef<jobject>& j_result_obj,
    const ScopedJavaGlobalRef<jobject>& j_callback_obj,
    const OfflinePageModel::MultipleOfflinePageItemResult& result) {
  JNIEnv* env = base::android::AttachCurrentThread();
  ToJavaOfflinePageList(env, j_result_obj.obj(), result);

  Java_MultipleOfflinePageItemCallback_onResult(env, j_callback_obj.obj(),
                                                j_result_obj.obj());
}

void HasPagesCallback(const ScopedJavaGlobalRef<jobject>& j_callback_obj,
                      bool result) {
  JNIEnv* env = base::android::AttachCurrentThread();
  Java_HasPagesCallback_onResult(env, j_callback_obj.obj(), result);
}

void SavePageCallback(const ScopedJavaGlobalRef<jobject>& j_callback_obj,
                      const GURL& url,
                      OfflinePageModel::SavePageResult result,
                      int64_t offline_id) {
  JNIEnv* env = base::android::AttachCurrentThread();

  Java_SavePageCallback_onSavePageDone(
      env, j_callback_obj.obj(), static_cast<int>(result),
      ConvertUTF8ToJavaString(env, url.spec()).obj(), offline_id);
}

void DeletePageCallback(const ScopedJavaGlobalRef<jobject>& j_callback_obj,
                        OfflinePageModel::DeletePageResult result) {
  JNIEnv* env = base::android::AttachCurrentThread();

  Java_DeletePageCallback_onDeletePageDone(
      env, j_callback_obj.obj(), static_cast<int>(result));
}

void SingleOfflinePageItemCallback(
    const ScopedJavaGlobalRef<jobject>& j_callback_obj,
    const OfflinePageModel::SingleOfflinePageItemResult& result) {
  JNIEnv* env = base::android::AttachCurrentThread();
  ScopedJavaLocalRef<jobject> j_result;

  if (result)
    j_result = ToJavaOfflinePageItem(env, *result);
  Java_SingleOfflinePageItemCallback_onResult(env, j_callback_obj.obj(),
                                              j_result.obj());
}

}  // namespace

static jboolean IsOfflinePagesEnabled(JNIEnv* env,
                                          const JavaParamRef<jclass>& clazz) {
  return offline_pages::IsOfflinePagesEnabled();
}

static jboolean IsOfflineBookmarksEnabled(JNIEnv* env,
                                          const JavaParamRef<jclass>& clazz) {
  return offline_pages::IsOfflineBookmarksEnabled();
}

static jboolean IsBackgroundLoadingEnabled(JNIEnv* env,
                                           const JavaParamRef<jclass>& clazz) {
  return offline_pages::IsOfflinePagesBackgroundLoadingEnabled();
}

static jboolean CanSavePage(JNIEnv* env,
                            const JavaParamRef<jclass>& clazz,
                            const JavaParamRef<jstring>& j_url) {
  GURL url(ConvertJavaStringToUTF8(env, j_url));
  return url.is_valid() && OfflinePageModel::CanSavePage(url);
}

static ScopedJavaLocalRef<jobject> GetOfflinePageBridgeForProfile(
    JNIEnv* env,
    const JavaParamRef<jclass>& jcaller,
    const JavaParamRef<jobject>& j_profile) {
  Profile* profile = ProfileAndroid::FromProfileAndroid(j_profile);

  OfflinePageModel* offline_page_model =
      OfflinePageModelFactory::GetForBrowserContext(profile);

  if (offline_page_model == nullptr)
    return ScopedJavaLocalRef<jobject>();

  OfflinePageBridge* bridge = static_cast<OfflinePageBridge*>(
      offline_page_model->GetUserData(kOfflinePageBridgeKey));
  if (!bridge) {
    bridge = new OfflinePageBridge(env, profile, offline_page_model);
    offline_page_model->SetUserData(kOfflinePageBridgeKey, bridge);
  }

  return ScopedJavaLocalRef<jobject>(bridge->java_ref());
}

OfflinePageBridge::OfflinePageBridge(JNIEnv* env,
                                     content::BrowserContext* browser_context,
                                     OfflinePageModel* offline_page_model)
    : browser_context_(browser_context),
      offline_page_model_(offline_page_model) {
  ScopedJavaLocalRef<jobject> j_offline_page_bridge =
      Java_OfflinePageBridge_create(env, reinterpret_cast<jlong>(this));
  java_ref_.Reset(j_offline_page_bridge);

  NotifyIfDoneLoading();
  offline_page_model_->AddObserver(this);
}

OfflinePageBridge::~OfflinePageBridge() {
  JNIEnv* env = base::android::AttachCurrentThread();

  // Native shutdown causes the destruction of |this|.
  Java_OfflinePageBridge_offlinePageBridgeDestroyed(env, java_ref_.obj());
}

void OfflinePageBridge::OfflinePageModelLoaded(OfflinePageModel* model) {
  DCHECK_EQ(offline_page_model_, model);
  NotifyIfDoneLoading();
}

void OfflinePageBridge::OfflinePageModelChanged(OfflinePageModel* model) {
  DCHECK_EQ(offline_page_model_, model);
  JNIEnv* env = base::android::AttachCurrentThread();
  Java_OfflinePageBridge_offlinePageModelChanged(env, java_ref_.obj());
}

void OfflinePageBridge::OfflinePageDeleted(int64_t offline_id,
                                           const ClientId& client_id) {
  JNIEnv* env = base::android::AttachCurrentThread();
  Java_OfflinePageBridge_offlinePageDeleted(
      env, java_ref_.obj(), offline_id, CreateClientId(env, client_id).obj());
}

void OfflinePageBridge::HasPages(JNIEnv* env,
                                 const JavaParamRef<jobject>& obj,
                                 const JavaParamRef<jstring>& j_namespace,
                                 const JavaParamRef<jobject>& j_callback_obj) {
  std::string name_space = ConvertJavaStringToUTF8(env, j_namespace);

  ScopedJavaGlobalRef<jobject> j_callback_ref;
  j_callback_ref.Reset(env, j_callback_obj);

  return offline_page_model_->HasPages(
      name_space, base::Bind(&HasPagesCallback, j_callback_ref));
}

void OfflinePageBridge::CheckPagesExistOffline(
    JNIEnv* env,
    const JavaParamRef<jobject>& obj,
    const JavaParamRef<jobjectArray>& j_urls_array,
    const JavaParamRef<jobject>& j_callback_obj) {
  DCHECK(j_urls_array);
  DCHECK(j_callback_obj);

  std::vector<std::string> urls;
  base::android::AppendJavaStringArrayToStringVector(env, j_urls_array.obj(),
                                                     &urls);

  std::set<GURL> page_urls;
  for (const std::string& url : urls)
    page_urls.insert(GURL(url));

  offline_page_model_->CheckPagesExistOffline(
      page_urls, base::Bind(&CheckPagesExistOfflineCallback,
                            ScopedJavaGlobalRef<jobject>(env, j_callback_obj)));
}

void OfflinePageBridge::GetAllPages(
    JNIEnv* env,
    const JavaParamRef<jobject>& obj,
    const JavaParamRef<jobject>& j_result_obj,
    const JavaParamRef<jobject>& j_callback_obj) {
  DCHECK(j_result_obj);
  DCHECK(j_callback_obj);

  ScopedJavaGlobalRef<jobject> j_result_ref;
  j_result_ref.Reset(env, j_result_obj);

  ScopedJavaGlobalRef<jobject> j_callback_ref;
  j_callback_ref.Reset(env, j_callback_obj);

  offline_page_model_->GetAllPages(
      base::Bind(&GetAllPagesCallback, j_result_ref, j_callback_ref));
}

ScopedJavaLocalRef<jlongArray> OfflinePageBridge::GetOfflineIdsForClientId(
    JNIEnv* env,
    const JavaParamRef<jobject>& obj,
    const JavaParamRef<jstring>& j_namespace,
    const JavaParamRef<jstring>& j_client_id) {
  DCHECK(offline_page_model_->is_loaded());
  offline_pages::ClientId client_id;
  client_id.name_space = ConvertJavaStringToUTF8(env, j_namespace);
  client_id.id = ConvertJavaStringToUTF8(env, j_client_id);

  std::vector<int64_t> results =
      offline_page_model_->MaybeGetOfflineIdsForClientId(client_id);

  return base::android::ToJavaLongArray(env, results);
}

ScopedJavaLocalRef<jobject> OfflinePageBridge::GetPageByOfflineId(
    JNIEnv* env,
    const JavaParamRef<jobject>& obj,
    jlong offline_id) {
  const OfflinePageItem* offline_page =
      offline_page_model_->MaybeGetPageByOfflineId(offline_id);
  if (!offline_page)
    return ScopedJavaLocalRef<jobject>();
  return ToJavaOfflinePageItem(env, *offline_page);
}

ScopedJavaLocalRef<jobject> OfflinePageBridge::GetBestPageForOnlineURL(
    JNIEnv* env,
    const JavaParamRef<jobject>& obj,
    const JavaParamRef<jstring>& online_url) {
  const OfflinePageItem* offline_page =
      offline_page_model_->MaybeGetBestPageForOnlineURL(
          GURL(ConvertJavaStringToUTF8(env, online_url)));
  if (!offline_page)
    return ScopedJavaLocalRef<jobject>();
  return ToJavaOfflinePageItem(env, *offline_page);
}

void OfflinePageBridge::GetPageByOfflineUrl(
    JNIEnv* env,
    const JavaParamRef<jobject>& obj,
    const JavaParamRef<jstring>& j_offline_url,
    const JavaParamRef<jobject>& j_callback_obj) {
  DCHECK(j_callback_obj);

  ScopedJavaGlobalRef<jobject> j_callback_ref;
  j_callback_ref.Reset(env, j_callback_obj);

  offline_page_model_->GetPageByOfflineURL(
      GURL(ConvertJavaStringToUTF8(env, j_offline_url)),
      base::Bind(&SingleOfflinePageItemCallback, j_callback_ref));
}

void OfflinePageBridge::SavePage(
    JNIEnv* env,
    const JavaParamRef<jobject>& obj,
    const JavaParamRef<jobject>& j_callback_obj,
    const JavaParamRef<jobject>& j_web_contents,
    const JavaParamRef<jstring>& j_namespace,
    const JavaParamRef<jstring>& j_client_id) {
  DCHECK(j_callback_obj);
  DCHECK(j_web_contents);

  ScopedJavaGlobalRef<jobject> j_callback_ref;
  j_callback_ref.Reset(env, j_callback_obj);

  GURL url;
  std::unique_ptr<OfflinePageArchiver> archiver;

  content::WebContents* web_contents =
      content::WebContents::FromJavaWebContents(j_web_contents);
  if (web_contents) {
    url = web_contents->GetLastCommittedURL();
    archiver.reset(new OfflinePageMHTMLArchiver(web_contents));
  }

  offline_pages::ClientId client_id;
  client_id.name_space = ConvertJavaStringToUTF8(env, j_namespace);
  client_id.id = ConvertJavaStringToUTF8(env, j_client_id);

  offline_page_model_->SavePage(
      url, client_id, std::move(archiver),
      base::Bind(&SavePageCallback, j_callback_ref, url));
}

void OfflinePageBridge::SavePageLater(
    JNIEnv* env,
    const JavaParamRef<jobject>& obj,
    const JavaParamRef<jstring>& j_url,
    const JavaParamRef<jstring>& j_namespace,
    const JavaParamRef<jstring>& j_client_id) {
  offline_pages::ClientId client_id;
  client_id.name_space = ConvertJavaStringToUTF8(env, j_namespace);
  client_id.id = ConvertJavaStringToUTF8(env, j_client_id);

  RequestCoordinator* coordinator =
      offline_pages::RequestCoordinatorFactory::GetInstance()->
          GetForBrowserContext(browser_context_);

  coordinator->SavePageLater(
      GURL(ConvertJavaStringToUTF8(env, j_url)), client_id);
}

void OfflinePageBridge::DeletePages(
    JNIEnv* env,
    const JavaParamRef<jobject>& obj,
    const JavaParamRef<jobject>& j_callback_obj,
    const JavaParamRef<jlongArray>& offline_ids_array) {
  DCHECK(j_callback_obj);

  ScopedJavaGlobalRef<jobject> j_callback_ref;
  j_callback_ref.Reset(env, j_callback_obj);

  std::vector<int64_t> offline_ids;
  base::android::JavaLongArrayToInt64Vector(env, offline_ids_array,
                                            &offline_ids);

  offline_page_model_->DeletePagesByOfflineId(
      offline_ids, base::Bind(&DeletePageCallback, j_callback_ref));
}

void OfflinePageBridge::CheckMetadataConsistency(
    JNIEnv* env,
    const JavaParamRef<jobject>& obj) {
  offline_page_model_->CheckForExternalFileDeletion();
}

void OfflinePageBridge::NotifyIfDoneLoading() const {
  if (!offline_page_model_->is_loaded())
    return;
  JNIEnv* env = base::android::AttachCurrentThread();
  Java_OfflinePageBridge_offlinePageModelLoaded(env, java_ref_.obj());
}


ScopedJavaLocalRef<jobject> OfflinePageBridge::CreateClientId(
    JNIEnv* env,
    const ClientId& client_id) const {
  return Java_OfflinePageBridge_createClientId(
      env,
      ConvertUTF8ToJavaString(env, client_id.name_space).obj(),
      ConvertUTF8ToJavaString(env, client_id.id).obj());
}

bool RegisterOfflinePageBridge(JNIEnv* env) {
  return RegisterNativesImpl(env);
}

}  // namespace android
}  // namespace offline_pages
