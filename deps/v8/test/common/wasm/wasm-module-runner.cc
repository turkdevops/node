// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "test/common/wasm/wasm-module-runner.h"

#include "src/execution/isolate.h"
#include "src/handles/handles.h"
#include "src/objects/heap-number-inl.h"
#include "src/objects/objects-inl.h"
#include "src/objects/property-descriptor.h"
#include "src/wasm/module-decoder.h"
#include "src/wasm/wasm-engine.h"
#include "src/wasm/wasm-js.h"
#include "src/wasm/wasm-module.h"
#include "src/wasm/wasm-objects.h"
#include "src/wasm/wasm-result.h"
#include "test/common/wasm/wasm-interpreter.h"

namespace v8 {
namespace internal {
namespace wasm {
namespace testing {

MaybeHandle<WasmModuleObject> CompileForTesting(Isolate* isolate,
                                                ErrorThrower* thrower,
                                                const ModuleWireBytes& bytes) {
  auto enabled_features = WasmFeatures::FromIsolate(isolate);
  MaybeHandle<WasmModuleObject> module = isolate->wasm_engine()->SyncCompile(
      isolate, enabled_features, thrower, bytes);
  DCHECK_EQ(thrower->error(), module.is_null());
  return module;
}

MaybeHandle<WasmInstanceObject> CompileAndInstantiateForTesting(
    Isolate* isolate, ErrorThrower* thrower, const ModuleWireBytes& bytes) {
  MaybeHandle<WasmModuleObject> module =
      CompileForTesting(isolate, thrower, bytes);
  if (module.is_null()) return {};
  return isolate->wasm_engine()->SyncInstantiate(
      isolate, thrower, module.ToHandleChecked(), {}, {});
}

bool InterpretWasmModuleForTesting(Isolate* isolate,
                                   Handle<WasmInstanceObject> instance,
                                   const char* name, size_t argc,
                                   WasmValue* args) {
  HandleScope handle_scope(isolate);  // Avoid leaking handles.
  WasmCodeRefScope code_ref_scope;
  MaybeHandle<WasmExportedFunction> maybe_function =
      GetExportedFunction(isolate, instance, "main");
  Handle<WasmExportedFunction> function;
  if (!maybe_function.ToHandle(&function)) {
    return false;
  }
  int function_index = function->function_index();
  const FunctionSig* signature =
      instance->module()->functions[function_index].sig;
  size_t param_count = signature->parameter_count();
  std::unique_ptr<WasmValue[]> arguments(new WasmValue[param_count]);

  size_t arg_count = std::min(param_count, argc);
  if (arg_count > 0) {
    memcpy(arguments.get(), args, arg_count);
  }

  // Fill the parameters up with default values.
  for (size_t i = argc; i < param_count; ++i) {
    switch (signature->GetParam(i).kind()) {
      case ValueType::kI32:
        arguments[i] = WasmValue(int32_t{0});
        break;
      case ValueType::kI64:
        arguments[i] = WasmValue(int64_t{0});
        break;
      case ValueType::kF32:
        arguments[i] = WasmValue(0.0f);
        break;
      case ValueType::kF64:
        arguments[i] = WasmValue(0.0);
        break;
      case ValueType::kOptRef:
        arguments[i] =
            WasmValue(Handle<Object>::cast(isolate->factory()->null_value()));
        break;
      case ValueType::kRef:
      case ValueType::kRtt:
      case ValueType::kI8:
      case ValueType::kI16:
      case ValueType::kStmt:
      case ValueType::kBottom:
      case ValueType::kS128:
        UNREACHABLE();
    }
  }

  // Don't execute more than 16k steps.
  constexpr int kMaxNumSteps = 16 * 1024;

  Zone zone(isolate->allocator(), ZONE_NAME);

  WasmInterpreter interpreter{
      isolate, instance->module(),
      ModuleWireBytes{instance->module_object().native_module()->wire_bytes()},
      instance};
  interpreter.InitFrame(&instance->module()->functions[function_index],
                        arguments.get());
  WasmInterpreter::State interpreter_result = interpreter.Run(kMaxNumSteps);

  if (isolate->has_pending_exception()) {
    // Stack overflow during interpretation.
    isolate->clear_pending_exception();
    return false;
  }

  return interpreter_result != WasmInterpreter::PAUSED;
}

int32_t RunWasmModuleForTesting(Isolate* isolate,
                                Handle<WasmInstanceObject> instance, int argc,
                                Handle<Object> argv[]) {
  ErrorThrower thrower(isolate, "RunWasmModule");
  return CallWasmFunctionForTesting(isolate, instance, &thrower, "main", argc,
                                    argv);
}

int32_t CompileAndRunWasmModule(Isolate* isolate, const byte* module_start,
                                const byte* module_end) {
  HandleScope scope(isolate);
  ErrorThrower thrower(isolate, "CompileAndRunWasmModule");
  MaybeHandle<WasmInstanceObject> instance = CompileAndInstantiateForTesting(
      isolate, &thrower, ModuleWireBytes(module_start, module_end));
  if (instance.is_null()) {
    return -1;
  }
  return RunWasmModuleForTesting(isolate, instance.ToHandleChecked(), 0,
                                 nullptr);
}

WasmInterpretationResult InterpretWasmModule(
    Isolate* isolate, Handle<WasmInstanceObject> instance,
    int32_t function_index, WasmValue* args) {
  // Don't execute more than 16k steps.
  constexpr int kMaxNumSteps = 16 * 1024;

  Zone zone(isolate->allocator(), ZONE_NAME);
  v8::internal::HandleScope scope(isolate);

  WasmInterpreter interpreter{
      isolate, instance->module(),
      ModuleWireBytes{instance->module_object().native_module()->wire_bytes()},
      instance};
  interpreter.InitFrame(&instance->module()->functions[function_index], args);
  WasmInterpreter::State interpreter_result = interpreter.Run(kMaxNumSteps);

  bool stack_overflow = isolate->has_pending_exception();
  isolate->clear_pending_exception();

  if (stack_overflow) return WasmInterpretationResult::Stopped();

  if (interpreter.state() == WasmInterpreter::TRAPPED) {
    return WasmInterpretationResult::Trapped(
        interpreter.PossibleNondeterminism());
  }

  if (interpreter_result == WasmInterpreter::FINISHED) {
    return WasmInterpretationResult::Finished(
        interpreter.GetReturnValue().to<int32_t>(),
        interpreter.PossibleNondeterminism());
  }

  return WasmInterpretationResult::Stopped();
}

MaybeHandle<WasmExportedFunction> GetExportedFunction(
    Isolate* isolate, Handle<WasmInstanceObject> instance, const char* name) {
  Handle<JSObject> exports_object;
  Handle<Name> exports = isolate->factory()->InternalizeUtf8String("exports");
  exports_object = Handle<JSObject>::cast(
      JSObject::GetProperty(isolate, instance, exports).ToHandleChecked());

  Handle<Name> main_name = isolate->factory()->NewStringFromAsciiChecked(name);
  PropertyDescriptor desc;
  Maybe<bool> property_found = JSReceiver::GetOwnPropertyDescriptor(
      isolate, exports_object, main_name, &desc);
  if (!property_found.FromMaybe(false)) return {};
  if (!desc.value()->IsJSFunction()) return {};

  return Handle<WasmExportedFunction>::cast(desc.value());
}

int32_t CallWasmFunctionForTesting(Isolate* isolate,
                                   Handle<WasmInstanceObject> instance,
                                   ErrorThrower* thrower, const char* name,
                                   int argc, Handle<Object> argv[]) {
  MaybeHandle<WasmExportedFunction> maybe_export =
      GetExportedFunction(isolate, instance, name);
  Handle<WasmExportedFunction> main_export;
  if (!maybe_export.ToHandle(&main_export)) {
    return -1;
  }

  // Call the JS function.
  Handle<Object> undefined = isolate->factory()->undefined_value();
  MaybeHandle<Object> retval =
      Execution::Call(isolate, main_export, undefined, argc, argv);

  // The result should be a number.
  if (retval.is_null()) {
    DCHECK(isolate->has_pending_exception());
    isolate->clear_pending_exception();
    thrower->RuntimeError("Calling exported wasm function failed.");
    return -1;
  }
  Handle<Object> result = retval.ToHandleChecked();
  if (result->IsSmi()) {
    return Smi::ToInt(*result);
  }
  if (result->IsHeapNumber()) {
    return static_cast<int32_t>(HeapNumber::cast(*result).value());
  }
  thrower->RuntimeError(
      "Calling exported wasm function failed: Return value should be number");
  return -1;
}

void SetupIsolateForWasmModule(Isolate* isolate) {
  WasmJs::Install(isolate, true);
}

}  // namespace testing
}  // namespace wasm
}  // namespace internal
}  // namespace v8
