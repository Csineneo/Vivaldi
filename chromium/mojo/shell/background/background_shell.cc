// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mojo/shell/background/background_shell.h"

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/command_line.h"
#include "base/logging.h"
#include "base/path_service.h"
#include "base/synchronization/waitable_event.h"
#include "base/threading/simple_thread.h"
#include "mojo/message_pump/message_pump_mojo.h"
#include "mojo/services/package_manager/package_manager.h"
#include "mojo/shell/application_loader.h"
#include "mojo/shell/application_manager.h"
#include "mojo/shell/capability_filter.h"
#include "mojo/shell/connect_params.h"
#include "mojo/shell/public/cpp/shell_client.h"
#include "mojo/shell/public/cpp/shell_connection.h"
#include "mojo/shell/standalone/context.h"
#include "url/gurl.h"

namespace mojo {
namespace shell {
namespace {

scoped_ptr<base::MessagePump> CreateMessagePumpMojo() {
  return make_scoped_ptr(new common::MessagePumpMojo);
}

// Used to obtain the ShellClientRequest for an application. When
// ApplicationLoader::Load() is called a callback is run with the
// ShellClientRequest.
class BackgroundApplicationLoader : public ApplicationLoader {
 public:
  using Callback = base::Callback<void(mojom::ShellClientRequest)>;

  explicit BackgroundApplicationLoader(const Callback& callback)
      : callback_(callback) {}
  ~BackgroundApplicationLoader() override {}

  // ApplicationLoader:
  void Load(const GURL& url, mojom::ShellClientRequest request) override {
    DCHECK(!callback_.is_null());  // Callback should only be run once.
    Callback callback = callback_;
    callback_.Reset();
    callback.Run(std::move(request));
  }

 private:
  Callback callback_;

  DISALLOW_COPY_AND_ASSIGN(BackgroundApplicationLoader);
};

class MojoMessageLoop : public base::MessageLoop {
 public:
  MojoMessageLoop()
      : base::MessageLoop(base::MessageLoop::TYPE_CUSTOM,
                          base::Bind(&CreateMessagePumpMojo)) {}
  ~MojoMessageLoop() override {}

  void BindToCurrentThread() { base::MessageLoop::BindToCurrentThread(); }

 private:
  DISALLOW_COPY_AND_ASSIGN(MojoMessageLoop);
};

}  // namespace

// Manages the thread to startup mojo.
class BackgroundShell::MojoThread : public base::SimpleThread {
 public:
  explicit MojoThread(scoped_ptr<BackgroundShell::InitParams> init_params)
      : SimpleThread("mojo-background-shell"),
        init_params_(std::move(init_params)) {}
  ~MojoThread() override {}

  void CreateShellClientRequest(base::WaitableEvent* signal,
                                scoped_ptr<ConnectParams> params,
                                mojom::ShellClientRequest* request) {
    // Only valid to call this on the background thread.
    DCHECK_EQ(message_loop_, base::MessageLoop::current());

    // Ownership of |loader| passes to ApplicationManager.
    const GURL url = params->target().url();
    BackgroundApplicationLoader* loader = new BackgroundApplicationLoader(
        base::Bind(&MojoThread::OnGotApplicationRequest, base::Unretained(this),
                   url, signal, request));
    context_->application_manager()->SetLoaderForURL(make_scoped_ptr(loader),
                                                     url);
    context_->application_manager()->Connect(std::move(params));
    // The request is asynchronously processed. When processed
    // OnGotApplicationRequest() is called and we'll signal |signal|.
  }

  base::MessageLoop* message_loop() { return message_loop_; }

  // Stops the background thread.
  void Stop() {
    DCHECK_NE(message_loop_, base::MessageLoop::current());
    message_loop_->task_runner()->PostTask(
        FROM_HERE, base::MessageLoop::QuitWhenIdleClosure());
    Join();
  }

  // base::SimpleThread:
  void Start() override {
    DCHECK(!message_loop_);
    message_loop_ = new MojoMessageLoop;
    base::SimpleThread::Start();
  }
  void Run() override {
    // The construction/destruction order is very finicky and has to be done
    // in the order here.
    scoped_ptr<base::MessageLoop> message_loop(message_loop_);

    Context::EnsureEmbedderIsInitialized();

    message_loop_->BindToCurrentThread();

    scoped_ptr<Context> context(new Context);
    context_ = context.get();
    scoped_ptr<mojo::shell::Context::InitParams> context_init_params(
        new mojo::shell::Context::InitParams);
    if (init_params_) {
      context_init_params->app_catalog = std::move(init_params_->app_catalog);
      context_init_params->native_runner_delegate =
          init_params_->native_runner_delegate;
    }
    context_->Init(std::move(context_init_params));

    message_loop_->Run();

    // Has to happen after run, but while messageloop still valid.
    context_->Shutdown();

    // Context has to be destroyed after the MessageLoop has been destroyed.
    message_loop.reset();
    context_ = nullptr;
  }

 private:
  void OnGotApplicationRequest(const GURL& url,
                               base::WaitableEvent* signal,
                               mojom::ShellClientRequest* request_result,
                               mojom::ShellClientRequest actual_request) {
    *request_result = std::move(actual_request);
    // Trigger destruction of the loader.
    context_->application_manager()->SetLoaderForURL(nullptr, url);
    signal->Signal();
  }

  // We own this. It's created on the main thread, but destroyed on the
  // background thread.
  MojoMessageLoop* message_loop_ = nullptr;
  // Created in Run() on the background thread.
  Context* context_ = nullptr;

  scoped_ptr<BackgroundShell::InitParams> init_params_;

  DISALLOW_COPY_AND_ASSIGN(MojoThread);
};

BackgroundShell::InitParams::InitParams() {}
BackgroundShell::InitParams::~InitParams() {}

BackgroundShell::BackgroundShell() {}

BackgroundShell::~BackgroundShell() {
  thread_->Stop();
}

void BackgroundShell::Init(scoped_ptr<InitParams> init_params) {
  DCHECK(!thread_);
  thread_.reset(new MojoThread(std::move(init_params)));
  thread_->Start();
}

mojom::ShellClientRequest BackgroundShell::CreateShellClientRequest(
    const GURL& url) {
  scoped_ptr<ConnectParams> params(new ConnectParams);
  params->set_target(Identity(url, std::string(), mojom::Connector::kUserRoot));
  mojom::ShellClientRequest request;
  base::WaitableEvent signal(true, false);
  thread_->message_loop()->task_runner()->PostTask(
      FROM_HERE, base::Bind(&MojoThread::CreateShellClientRequest,
                            base::Unretained(thread_.get()), &signal,
                            base::Passed(&params), &request));
  signal.Wait();
  return request;
}

}  // namespace shell
}  // namespace mojo
