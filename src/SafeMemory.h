#pragma once

// Defensive reads for data whose layout in CommonLibSF has not been confirmed on every game version.
// A wrong number only shows an odd value; following a wrong pointer would crash. So every pointer
// taken from such data goes through these checks before it is dereferenced.

namespace MIC::Safe
{
	// True when [a_ptr, a_ptr + a_size) is committed, readable, non-guard memory.
	bool Readable(const void* a_ptr, std::size_t a_size);

	// Returns a_ptr as a form only if it is readable and the engine's form table maps its form ID
	// back to the same address. Anything else (garbage, freed, not a form) returns nullptr.
	RE::TESForm* Form(const void* a_ptr);

	template <class T>
	T* FormAs(const void* a_ptr)
	{
		auto* form = Form(a_ptr);
		return form ? form->As<T>() : nullptr;
	}

	// Reads a NUL-terminated string of at most a_max bytes, checking each page it touches.
	std::string CString(const char* a_str, std::size_t a_max = 256);

	// Reads a BSFixedString / BSFixedStringCS stored at a_address (the object itself, not its text).
	std::string FixedString(const void* a_address, std::size_t a_max = 256);

	// Copies a trivially copyable value at a_address if readable.
	template <class T>
	bool Read(const void* a_address, T& a_out)
	{
		static_assert(std::is_trivially_copyable_v<T>);
		if (!Readable(a_address, sizeof(T))) {
			return false;
		}
		std::memcpy(std::addressof(a_out), a_address, sizeof(T));
		return true;
	}
}
