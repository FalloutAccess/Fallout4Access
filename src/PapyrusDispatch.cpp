#include "PapyrusDispatch.h"

#include <RE/B/BSScriptUtil.h>
#include <RE/B/BSScript_IVirtualMachine.h>
#include <RE/B/BSScript_Variable.h>

namespace
{
	// --- OG 1.10.163 args-functor ABI shim ---------------------------------
	// OG's DispatchStaticCall reads the args functor's impl pointer at
	// functor+0x18 and invokes (*(*impl + 0x10))(impl, &outArray) — and touches
	// the functor NOWHERE else (verified in Ghidra, Fallout4.exe 1527229). A
	// real std::function keeps its impl elsewhere, so OG reads garbage and CTDs.
	// We hand-build a functor with the layout OG expects. Lifetime: OG invokes
	// the functor synchronously while packing args (before the script body
	// runs), so stack-local storage at the callsite is safe.
	struct OGArgsImpl
	{
		const void*  vtable;  // +0x00 — OG calls vtable[2] (offset 0x10)
		bool         hasArg;  // +0x08
		std::int32_t arg;     // +0x0C
	};

	// vtable slot 2 (byte offset 0x10). x64: this=RCX, out=RDX.
	bool OGArgsInvoke(OGArgsImpl* a_self, RE::BSScrapArray<RE::BSScript::Variable>* a_out)
	{
		if (a_self->hasArg) {
			*a_out = RE::BSScript::detail::PackVariables(a_self->arg);
		}
		// No arg → leave OG's pre-initialized empty array as-is.
		return true;
	}

	// Slots 0/1 are unused by OG's invoke path; only slot 2 (offset 0x10) matters.
	const void* const kOGArgsVTable[3] = {
		nullptr, nullptr, reinterpret_cast<const void*>(&OGArgsInvoke)
	};

	struct OGArgsFunctor
	{
		std::uintptr_t _pad[3];  // +0x00, +0x08, +0x10
		OGArgsImpl*    impl;     // +0x18 — where OG reads the impl pointer
	};
}

bool PapyrusDispatch::DispatchStatic(RE::BSScript::IVirtualMachine* a_vm,
	const RE::BSFixedString&                                        a_obj,
	const RE::BSFixedString&                                        a_func,
	const RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor>& a_cb,
	const std::int32_t*                                             a_arg)
{
	if (REX::FModule::IsRuntimeOG()) {
		OGArgsImpl    impl{ kOGArgsVTable, a_arg != nullptr, a_arg ? *a_arg : 0 };
		OGArgsFunctor functor{ {}, &impl };
		using FunctorT =
			RE::BSTThreadScrapFunction<bool(RE::BSScrapArray<RE::BSScript::Variable>&)>;
		return a_vm->DispatchStaticCall(a_obj, a_func,
			reinterpret_cast<const FunctorT&>(functor), a_cb);
	}

	if (a_arg) {
		return a_vm->DispatchStaticCall(a_obj, a_func, a_cb, *a_arg);
	}
	return a_vm->DispatchStaticCall(a_obj, a_func, a_cb);
}
