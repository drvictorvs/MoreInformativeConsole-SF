#include "AddressCheck.h"

#include "GameIDs.h"

namespace MIC::Address
{
	namespace
	{
		// Reads IDDB's lookup tables without calling offset(). get_id2offset() is protected; a pointer to
		// it may be formed through a derived class, then called on the real IDDB object.
		struct IDDBPeek : REL::IDDB
		{
			static std::span<std::uint32_t> V5(const REL::IDDB& a_db)
			{
				constexpr auto fn = &IDDBPeek::get_id2offset<std::uint32_t>;
				return (a_db.*fn)();
			}

			static std::span<REL::IDDB::MAPPING> V0(const REL::IDDB& a_db)
			{
				constexpr auto fn = &IDDBPeek::get_id2offset<REL::IDDB::MAPPING>;
				return (a_db.*fn)();
			}
		};

		// The same checks IDDB's constructor makes before it would call REX::FAIL: the plugin sits in
		// Data/SFSE/Plugins, and versionlib-<game version>.bin is there with a supported format.
		bool DatabaseLoadable()
		{
			wchar_t buffer[REX::W32::MAX_PATH]{};
			REX::W32::GetModuleFileNameW(REX::W32::GetCurrentModule(), buffer, REX::W32::MAX_PATH);
			const std::filesystem::path plugin(buffer);

			auto loader = plugin.parent_path().parent_path().filename().wstring();
			std::ranges::transform(loader, loader.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towupper(c)); });
			if (loader != L"SFSE") {
				REX::ERROR("Plugin is not inside an SFSE folder ({}); Address Library cannot be located",
					plugin.parent_path().string());
				return false;
			}

			const auto version = REX::FModule::GetExecutingModule().GetFileVersion();
			const auto path = plugin.parent_path() / std::format(L"versionlib-{}.bin", version.wstring(L"-"));

			std::error_code ec;
			if (!std::filesystem::exists(path, ec)) {
				REX::ERROR("Address Library for game version {} not found ({})", version.string(), path.string());
				return false;
			}

			std::ifstream file(path, std::ios::binary);
			std::uint32_t format = 0;
			if (!file.read(reinterpret_cast<char*>(&format), sizeof(format))) {
				REX::ERROR("Could not read the Address Library file {}", path.string());
				return false;
			}
			if (format != 1 && format != 2 && format != 5) {
				REX::ERROR("Unsupported Address Library format {} in {}", format, path.string());
				return false;
			}
			return true;
		}

		bool& DatabaseOK()
		{
			static bool ok = DatabaseLoadable();
			return ok;
		}

		struct Need
		{
			REL::ID     id;
			const char* name;
		};

		// Engine functions and globals reached by this plugin, grouped by feature.
		// Traced from the CommonLibSF headers at OSF fork commit 4242565.
		const std::vector<Need>& CoreIDs()
		{
			namespace GFxObj = RE::ID::Scaleform::GFx::Value::ObjectInterface;
			static const std::vector<Need> ids{
				{ RE::ID::UI::Singleton, "UI::Singleton" },
				{ RE::ID::UI::IsMenuOpen, "UI::IsMenuOpen" },
				{ RE::ID::TESForm::LookupByID, "TESForm::LookupByID" },
				{ RE::ID::RTDynamicCast, "RTDynamicCast" },  // starfield_cast
				{ RE::ID::BSStringPool::GetEntry, "BSStringPool::GetEntry" },  // BSFixedString("Console")
				{ RE::ID::BSStringPool::Entry::Release, "BSStringPool::Entry::Release" },
				{ RE::ID::BSStringPool::BucketTable::GetSingleton, "BSStringPool::BucketTable::GetSingleton" },
				{ GFxObj::ObjectAddRef, "GFx ObjectAddRef" },
				{ GFxObj::ObjectRelease, "GFx ObjectRelease" },
				{ GFxObj::GetMember, "GFx GetMember" },
				{ GFxObj::SetMember, "GFx SetMember" },
				{ GFxObj::Invoke, "GFx Invoke" },
			};
			return ids;
		}

		bool CheckGroup(std::string_view a_feature, std::initializer_list<Need> a_ids)
		{
			bool ok = true;
			for (const auto& need : a_ids) {
				if (!IsPresent(need.id.id())) {
					REX::WARN("{}: Address Library ID {} ({}) is not available for this game version",
						a_feature, need.id.id(), need.name);
					ok = false;
				}
			}
			return ok;
		}

		Features Run()
		{
			Features f;
			if (!DatabaseOK()) {
				REX::ERROR("Address Library unavailable; the plugin stays inactive");
				return f;
			}

			bool core = true;
			for (const auto& need : CoreIDs()) {
				if (!IsPresent(need.id.id())) {
					REX::WARN("Panel: Address Library ID {} ({}) is not available for this game version",
						need.id.id(), need.name);
					core = false;
				}
			}
			f.core = core;

			f.consolePrint = CheckGroup("bPrintToConsole", {
				{ RE::ID::ConsoleLog::Singleton, "ConsoleLog::Singleton" },
				{ RE::ID::ConsoleLog::AddString, "ConsoleLog::AddString" },
			});
			f.location = CheckGroup("Location line", {
				{ RE::ID::TESObjectREFR::GetCurrentLocation, "TESObjectREFR::GetCurrentLocation" },
			});
			f.dataHandler = CheckGroup("Plugin names", {
				{ RE::ID::TESDataHandler::Singleton, "TESDataHandler::Singleton" },
			});

			f.inventory = CheckGroup("Inventory", {
				{ RE::ID::BSReadWriteLock::LockRead, "BSReadWriteLock::LockRead" },
				{ RE::ID::BSReadWriteLock::UnlockRead, "BSReadWriteLock::UnlockRead" },
			});
			f.factionRank = CheckGroup("Live faction ranks", {
				{ RE::ID::Actor::GetFactionRank, "Actor::GetFactionRank" },
			});
			f.furniture = CheckGroup("Furniture line", {
				{ RE::ID::BSPointerHandleManagerInterface::GetSmartPointer, "BSPointerHandleManagerInterface::GetSmartPointer" },
				{ RE::ID::TESForm::DecRefCount, "TESForm::DecRefCount" },
			});
			f.actorValues = CheckGroup("Actor values", {
				{ RE::ID::ActorValue::GetSingleton, "ActorValue::GetSingleton" },
			});

			{
				bool ok = CheckGroup("Lock details", {
					{ GameID::GetLock, "GetLock (found for this mod)" },
					{ GameID::GetAreaLevel, "GetAreaLevel (found for this mod)" },
					{ GameID::fLeveledLockMult, "fLeveledLockMult" },
				});
				for (const auto& rung : GameID::kLockLadder) {
					ok = CheckGroup("Lock details", { { rung.maxValue, "iLockLevelMax*" }, { rung.name, "sLockLevelName*" } }) && ok;
				}
				f.lockDetails = ok;
			}

			f.materialSwaps = CheckGroup("Material swaps", {
				{ GameID::GetModelMaterialSwaps, "GetModelMaterialSwaps (found for this mod)" },
			});

			if (!f.core) {
				REX::ERROR("Core functions are missing for this game version; the plugin stays inactive");
			}
			REX::INFO("Address check: panel={} consolePrint={} location={} pluginNames={} inventory={} factionRank={} furniture={} actorValues={} lockDetails={} materialSwaps={}",
				f.core, f.consolePrint, f.location, f.dataHandler, f.inventory, f.factionRank, f.furniture, f.actorValues, f.lockDetails, f.materialSwaps);
			return f;
		}
	}

	bool IsPresent(std::uint64_t a_id)
	{
		if (a_id == 0 || !DatabaseOK()) {
			return false;
		}
		const auto* db = REL::IDDB::GetSingleton();
		if (!db) {
			return false;
		}

		if (const auto v5 = IDDBPeek::V5(*db); !v5.empty()) {
			return a_id < v5.size() && v5[a_id] != 0;
		}

		const auto v0 = IDDBPeek::V0(*db);
		const auto it = std::ranges::lower_bound(v0, a_id, {}, &REL::IDDB::MAPPING::id);
		return it != v0.end() && it->id == a_id && it->offset != 0;
	}

	const Features& Get()
	{
		static const Features features = Run();
		return features;
	}
}
