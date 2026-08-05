#pragma once

#include "pch.h"
#include <initializer_list>
#include <string>

// Small shared helpers for defensively reading Scaleform GFx::Value trees.
// Used by menu-accessibility modules that walk loader-hosted content
// (Mod Manager, Bethesda.net login) where paths cannot be resolved with a
// single GetVariable and must be navigated member-by-member.
//
// Every reader checks both presence and type, returning a safe default on any
// failure, per the project's "Unchecked GetVariable Returns" rule.
namespace sfh
{
	// Read an integer-like Scaleform value (Int / UInt / Number) into an
	// std::int32_t. Returns true on success.
	inline bool ReadNumberAsInt(const Scaleform::GFx::Value& a_val, std::int32_t& a_out)
	{
		if (a_val.IsInt()) {
			a_out = a_val.GetInt();
			return true;
		}
		if (a_val.IsUInt()) {
			a_out = static_cast<std::int32_t>(a_val.GetUInt());
			return true;
		}
		if (a_val.IsNumber()) {
			a_out = static_cast<std::int32_t>(a_val.GetNumber());
			return true;
		}
		return false;
	}

	// Read a boolean-like Scaleform value. Accepts IsBoolean and numeric types
	// (non-zero == true) since some game UIs expose flags as numbers.
	inline bool ReadAsBool(const Scaleform::GFx::Value& a_val, bool& a_out)
	{
		if (a_val.IsBoolean()) {
			a_out = a_val.GetBoolean();
			return true;
		}
		if (a_val.IsNumber()) {
			a_out = a_val.GetNumber() != 0.0;
			return true;
		}
		if (a_val.IsInt()) {
			a_out = a_val.GetInt() != 0;
			return true;
		}
		if (a_val.IsUInt()) {
			a_out = a_val.GetUInt() != 0;
			return true;
		}
		return false;
	}

	// Read a string-valued member from a Scaleform object. Returns empty string
	// on any failure.
	inline std::string ReadStringMember(Scaleform::GFx::Value& a_obj, const char* a_name)
	{
		Scaleform::GFx::Value member;
		if (a_obj.GetMember(a_name, &member) && member.IsString()) {
			return member.GetString();
		}
		return "";
	}

	// Read an integer-valued member. Returns a_default on any failure.
	inline std::int32_t ReadIntMember(Scaleform::GFx::Value& a_obj, const char* a_name, std::int32_t a_default = -1)
	{
		Scaleform::GFx::Value member;
		if (!a_obj.GetMember(a_name, &member)) {
			return a_default;
		}
		std::int32_t out = a_default;
		if (!ReadNumberAsInt(member, out)) {
			return a_default;
		}
		return out;
	}

	// Read a boolean-valued member. Returns a_default on any failure.
	inline bool ReadBoolMember(Scaleform::GFx::Value& a_obj, const char* a_name, bool a_default = false)
	{
		Scaleform::GFx::Value member;
		if (!a_obj.GetMember(a_name, &member)) {
			return a_default;
		}
		bool out = a_default;
		if (!ReadAsBool(member, out)) {
			return a_default;
		}
		return out;
	}

	// Read the `.text` string of a child object reached by walking a_path of
	// member names from a_start (e.g. {"DetailsPage_mc", "Title_tf"} reads
	// a_start.DetailsPage_mc.Title_tf.text). Returns empty string on failure.
	inline std::string ReadTextAtPath(
		const Scaleform::GFx::Value&       a_start,
		std::initializer_list<const char*> a_path);

	// Walk a chain of object members: foo -> foo.bar -> foo.bar.baz. a_path is
	// the list of member names; each hop must resolve to an object. Returns
	// true with a_out set to the final object on success. An empty path returns
	// a_start unchanged (success if a_start is an object).
	inline bool WalkMembers(
		const Scaleform::GFx::Value&       a_start,
		std::initializer_list<const char*> a_path,
		Scaleform::GFx::Value&             a_out)
	{
		Scaleform::GFx::Value current = a_start;
		if (!current.IsObject()) {
			return false;
		}
		for (auto* name : a_path) {
			Scaleform::GFx::Value next;
			if (!current.GetMember(name, &next) || !next.IsObject()) {
				return false;
			}
			current = next;
		}
		a_out = current;
		return true;
	}

	inline std::string ReadTextAtPath(
		const Scaleform::GFx::Value&       a_start,
		std::initializer_list<const char*> a_path)
	{
		Scaleform::GFx::Value obj;
		if (!WalkMembers(a_start, a_path, obj)) {
			return "";
		}
		return ReadStringMember(obj, "text");
	}

	// Read an object-valued member. Returns true with a_out set on success.
	inline bool GetObjectMember(Scaleform::GFx::Value& a_obj, const char* a_name, Scaleform::GFx::Value& a_out)
	{
		return a_obj.GetMember(a_name, &a_out) && a_out.IsObject();
	}

	// Invoke a_obj.getChildAt(a_idx) and return true with the child in a_out on
	// success. The public display-list API is the only way to reach loader
	// content and unnamed children from C++.
	inline bool GetChildAt(Scaleform::GFx::Value& a_obj, std::int32_t a_idx, Scaleform::GFx::Value& a_out)
	{
		if (a_idx < 0 || !a_obj.IsObject()) {
			return false;
		}
		Scaleform::GFx::Value args[1];
		args[0] = static_cast<std::uint32_t>(a_idx);
		if (!a_obj.Invoke("getChildAt", &a_out, args, 1)) {
			return false;
		}
		return a_out.IsObject();
	}

	// Read numChildren as an int. Returns 0 on failure.
	inline std::int32_t GetNumChildren(Scaleform::GFx::Value& a_obj)
	{
		return ReadIntMember(a_obj, "numChildren", 0);
	}

	// True if a_obj.<a_name>.visible == true.
	inline bool ChildVisible(Scaleform::GFx::Value& a_obj, const char* a_name)
	{
		Scaleform::GFx::Value child;
		if (!GetObjectMember(a_obj, a_name, child)) {
			return false;
		}
		return ReadBoolMember(child, "visible");
	}
}
