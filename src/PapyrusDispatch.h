#pragma once

#include "pch.h"

// OG-safe Papyrus global-function dispatch.
//
// CommonLibF4 models the VM's args functor (BSTThreadScrapFunction) as MSVC
// std::function, whose layout matches the AE/NG engine. The OG 1.10.163 engine
// predates that toolchain: its DispatchStaticCall reads the args functor's impl
// pointer at functor+0x18 and invokes (*(*impl + 0x10))(impl, &outArray) — and
// touches the functor NOWHERE else. A real std::function keeps its impl
// elsewhere, so OG reads garbage there and CTDs at Fallout4.exe+0x273659E.
//
// EVERY C++->Papyrus DispatchStaticCall on OG hits this (auto-walk, hit-marker
// sounds, LOS sound). Route all of them through this helper, which hand-builds
// a functor in OG's layout on the OG runtime and uses the stock CommonLibF4
// template on AE/NG.
namespace PapyrusDispatch
{
	// Dispatch a global Papyrus function taking 0 or 1 int32 arg (pass
	// a_arg == nullptr for no args). a_cb may be an empty BSTSmartPointer
	// (== nullptr callback) for non-latent fire-and-forget calls, or a real
	// callback to receive the return value / required for latent targets.
	bool DispatchStatic(RE::BSScript::IVirtualMachine*                  a_vm,
		const RE::BSFixedString&                                        a_obj,
		const RE::BSFixedString&                                        a_func,
		const RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor>& a_cb,
		const std::int32_t*                                             a_arg);
}
