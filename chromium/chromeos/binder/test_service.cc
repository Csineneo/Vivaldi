// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromeos/binder/test_service.h"

#include "base/bind.h"
#include "base/guid.h"
#include "base/run_loop.h"
#include "base/strings/utf_string_conversions.h"
#include "chromeos/binder/local_object.h"
#include "chromeos/binder/service_manager_proxy.h"
#include "chromeos/binder/transaction_data.h"
#include "chromeos/binder/transaction_data_reader.h"
#include "chromeos/binder/writable_transaction_data.h"

namespace binder {

class TestService::TestObject : public LocalObject::TransactionHandler {
 public:
  TestObject() { VLOG(1) << "Object created: " << this; }

  ~TestObject() override { VLOG(1) << "Object destroyed: " << this; }

  scoped_ptr<binder::TransactionData> OnTransact(
      binder::CommandBroker* command_broker,
      const binder::TransactionData& data) {
    VLOG(1) << "Transact code = " << data.GetCode();
    binder::TransactionDataReader reader(data);
    switch (data.GetCode()) {
      case INCREMENT_INT_TRANSACTION: {
        int32_t arg = 0;
        reader.ReadInt32(&arg);
        scoped_ptr<binder::WritableTransactionData> reply(
            new binder::WritableTransactionData());
        reply->WriteInt32(arg + 1);
        return std::move(reply);
      }
    }
    return scoped_ptr<TransactionData>();
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(TestObject);
};

TestService::TestService()
    : service_name_(base::ASCIIToUTF16("org.chromium.TestService-" +
                                       base::GenerateGUID())) {}

TestService::~TestService() {}

bool TestService::StartAndWait() {
  if (!thread_.Start() || !thread_.WaitUntilThreadStarted() ||
      !thread_.initialized()) {
    LOG(ERROR) << "Failed to start the thread.";
    return false;
  }
  bool result = false;
  base::RunLoop run_loop;
  thread_.task_runner()->PostTaskAndReply(
      FROM_HERE,
      base::Bind(&TestService::Initialize, base::Unretained(this), &result),
      run_loop.QuitClosure());
  run_loop.Run();
  return result;
}

void TestService::Stop() {
  thread_.Stop();
}

void TestService::Initialize(bool* result) {
  // Add service.
  scoped_refptr<LocalObject> object(
      new LocalObject(make_scoped_ptr(new TestObject)));
  *result = ServiceManagerProxy::AddService(thread_.command_broker(),
                                            service_name_, object, 0);
}

}  // namespace binder
