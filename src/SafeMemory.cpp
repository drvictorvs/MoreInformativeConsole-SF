#include "SafeMemory.h"

namespace MIC::Safe
{
	namespace
	{
		constexpr std::uint32_t kReadableMask =
			REX::W32::PAGE_READONLY | REX::W32::PAGE_READWRITE | REX::W32::PAGE_WRITECOPY |
			REX::W32::PAGE_EXECUTE_READ | REX::W32::PAGE_EXECUTE_READWRITE | 0x80u /* PAGE_EXECUTE_WRITECOPY */;
		constexpr std::uint32_t kGuardOrNoCache = 0x100u /* PAGE_GUARD */;
	}

	bool Readable(const void* a_ptr, std::size_t a_size)
	{
		const auto start = reinterpret_cast<std::uintptr_t>(a_ptr);
		if (start < 0x10000 || a_size == 0 || start + a_size < start) {
			return false;
		}
		const auto end = start + a_size;

		auto address = start;
		while (address < end) {
			REX::W32::MEMORY_BASIC_INFORMATION info{};
			if (REX::W32::VirtualQuery(reinterpret_cast<const void*>(address), &info, sizeof(info)) != sizeof(info)) {
				return false;
			}
			if (info.state != REX::W32::MEM_COMMIT || (info.protect & kGuardOrNoCache) || !(info.protect & kReadableMask)) {
				return false;
			}
			const auto regionEnd = reinterpret_cast<std::uintptr_t>(info.baseAddress) + info.regionSize;
			if (regionEnd <= address) {
				return false;
			}
			address = regionEnd;
		}
		return true;
	}

	RE::TESForm* Form(const void* a_ptr)
	{
		if (!Readable(a_ptr, sizeof(RE::TESForm))) {
			return nullptr;
		}
		const auto* candidate = static_cast<const RE::TESForm*>(a_ptr);
		const auto  id = candidate->GetFormID();  // plain field read; memory checked above
		if (id == 0) {
			return nullptr;
		}
		auto* real = RE::TESForm::LookupByID(id);
		return real == a_ptr ? real : nullptr;
	}

	std::string CString(const char* a_str, std::size_t a_max)
	{
		std::string out;
		if (!a_str) {
			return out;
		}
		constexpr std::uintptr_t kPage = 0x1000;
		auto                     p = reinterpret_cast<std::uintptr_t>(a_str);
		std::uintptr_t           checkedUntil = 0;
		for (std::size_t i = 0; i < a_max; ++i, ++p) {
			if (p >= checkedUntil) {
				const auto pageEnd = (p & ~(kPage - 1)) + kPage;
				if (!Readable(reinterpret_cast<const void*>(p), pageEnd - p)) {
					break;
				}
				checkedUntil = pageEnd;
			}
			const char c = *reinterpret_cast<const char*>(p);
			if (c == '\0') {
				break;
			}
			out += c;
		}
		return out;
	}

	std::string FixedString(const void* a_address, std::size_t a_max)
	{
		// BSFixedString holds a single BSStringPool::Entry*. Text follows the leaf entry.
		const RE::BSStringPool::Entry* entry = nullptr;
		if (!Read(a_address, entry) || !entry) {
			return {};
		}
		for (int hop = 0; hop < 4; ++hop) {
			if (!Readable(entry, sizeof(RE::BSStringPool::Entry))) {
				return {};
			}
			if (!entry->external()) {
				return CString(reinterpret_cast<const char*>(entry + 1), a_max);
			}
			entry = entry->_right;
		}
		return {};
	}
}
