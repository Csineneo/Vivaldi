// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "bindings/core/v8/ScriptModule.h"

#include "bindings/core/v8/V8Binding.h"
#include "core/dom/Modulator.h"
#include "core/dom/ScriptModuleResolver.h"

namespace blink {

ScriptModule::ScriptModule(v8::Isolate* isolate, v8::Local<v8::Module> module)
    : module_(SharedPersistent<v8::Module>::Create(module, isolate)),
      identity_hash_(static_cast<unsigned>(module->GetIdentityHash())) {
  DCHECK(!module_->IsEmpty());
}

ScriptModule::~ScriptModule() {}

ScriptModule ScriptModule::Compile(v8::Isolate* isolate,
                                   const String& source,
                                   const String& file_name,
                                   AccessControlStatus access_control_status) {
  v8::TryCatch try_catch(isolate);
  try_catch.SetVerbose(true);
  v8::Local<v8::Module> module;
  if (!V8Call(V8ScriptRunner::CompileModule(isolate, source, file_name,
                                            access_control_status),
              module, try_catch)) {
    // Compilation error is not used in Blink implementaion logic.
    // Note: Error message is delivered to user (e.g. console) by message
    // listeners set on v8::Isolate. See V8Initializer::initalizeMainThread().
    // TODO(nhiroki): Revisit this when supporting modules on worker threads.
    DCHECK(try_catch.HasCaught());
    return ScriptModule();
  }
  DCHECK(!try_catch.HasCaught());
  return ScriptModule(isolate, module);
}

ScriptValue ScriptModule::Instantiate(ScriptState* script_state) {
  v8::Isolate* isolate = script_state->GetIsolate();
  v8::TryCatch try_catch(isolate);

  DCHECK(!IsNull());
  v8::Local<v8::Context> context = script_state->GetContext();
  bool success = module_->NewLocal(script_state->GetIsolate())
                     ->Instantiate(context, &ResolveModuleCallback);
  if (!success) {
    DCHECK(try_catch.HasCaught());
    return ScriptValue(script_state, try_catch.Exception());
  }
  DCHECK(!try_catch.HasCaught());
  return ScriptValue();
}

void ScriptModule::Evaluate(ScriptState* script_state) {
  v8::Isolate* isolate = script_state->GetIsolate();
  v8::TryCatch try_catch(isolate);
  try_catch.SetVerbose(true);
  v8::Local<v8::Value> result;
  if (!V8Call(
          V8ScriptRunner::EvaluateModule(module_->NewLocal(isolate),
                                         script_state->GetContext(), isolate),
          result, try_catch)) {
    // TODO(adamk): report error
  }
}

Vector<String> ScriptModule::ModuleRequests(ScriptState* script_state) {
  if (IsNull())
    return Vector<String>();

  v8::Local<v8::Module> module = module_->NewLocal(script_state->GetIsolate());

  Vector<String> ret;

  int length = module->GetModuleRequestsLength();
  ret.ReserveInitialCapacity(length);
  for (int i = 0; i < length; ++i) {
    v8::Local<v8::String> v8_name = module->GetModuleRequest(i);
    ret.push_back(ToCoreString(v8_name));
  }
  return ret;
}

v8::MaybeLocal<v8::Module> ScriptModule::ResolveModuleCallback(
    v8::Local<v8::Context> context,
    v8::Local<v8::String> specifier,
    v8::Local<v8::Module> referrer) {
  v8::Isolate* isolate = context->GetIsolate();
  Modulator* modulator = Modulator::From(ScriptState::From(context));
  DCHECK(modulator);

  ScriptModule referrer_record(isolate, referrer);
  ExceptionState exception_state(isolate, ExceptionState::kExecutionContext,
                                 "ScriptModule", "resolveModuleCallback");
  ScriptModule resolved = modulator->GetScriptModuleResolver()->Resolve(
      ToCoreStringWithNullCheck(specifier), referrer_record, exception_state);
  if (resolved.IsNull()) {
    DCHECK(exception_state.HadException());
    return v8::MaybeLocal<v8::Module>();
  }

  DCHECK(!exception_state.HadException());
  return v8::MaybeLocal<v8::Module>(resolved.module_->NewLocal(isolate));
}

}  // namespace blink
