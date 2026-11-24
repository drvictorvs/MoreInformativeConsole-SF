#include "FormInfo.h"

#include "AddressCheck.h"
#include "GameIDs.h"
#include "SafeMemory.h"
#include "Settings.h"

namespace MIC
{
	namespace
	{
		constexpr float kRadToDeg = 57.29577951308232f;

		// Per-part timing for bDebugLog: which part of collecting the panel's data is slow.
		std::vector<std::pair<const char*, long long>>& Timings()
		{
			static std::vector<std::pair<const char*, long long>> timings;
			return timings;
		}

		struct ScopedTimer
		{
			explicit ScopedTimer(const char* a_name) :
				name(a_name), start(std::chrono::steady_clock::now()) {}
			~ScopedTimer()
			{
				const auto us = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count();
				for (auto& [n, total] : Timings()) {
					if (n == name) {
						total += us;
						return;
					}
				}
				Timings().emplace_back(name, us);
			}
			const char*                           name;
			std::chrono::steady_clock::time_point start;
		};

		// ---- small helpers ------------------------------------------------------------------------

		std::string Hex(RE::TESFormID a_id) { return std::format("{:08X}", a_id); }

		std::string FileName(const RE::TESFile* a_file)
		{
			if (!a_file || !Safe::Readable(a_file, sizeof(a_file->fileName))) {
				return {};
			}
			const auto* begin = a_file->fileName;
			const auto* end = std::find(begin, begin + sizeof(a_file->fileName), '\0');
			return std::string(begin, end);
		}

		// Editor IDs of the form types that don't keep theirs on the form, read once from the engine's
		// editor ID map (see GameID::EditorIDMap). Built the first time it's needed, after data load.
		bool KeepsEditorIDInMap(RE::FormType a_type)
		{
			using T = RE::FormType;
			return a_type == T::kFACT || a_type == T::kPACK || a_type == T::kLMSW || a_type == T::kQUST;
		}

		const std::unordered_map<RE::TESFormID, std::string>& EditorIDMap()
		{
			static std::unordered_map<RE::TESFormID, std::string> ids;
			static bool                                            built = false;
			if (built || !Address::Get().editorIDs) {
				return ids;
			}
			built = true;

			const std::byte* map = nullptr;
			const auto*      global = reinterpret_cast<const void*>(REL::Relocation<std::uintptr_t>{ GameID::EditorIDMap }.address());
			if (!Safe::Read(global, map) || !map || !Safe::Readable(map, 0x38)) {
				return ids;
			}
			std::uint64_t    capacity = 0;
			const std::byte* entries = nullptr;
			Safe::Read(map + 0x18, capacity);
			Safe::Read(map + 0x10, entries);
			constexpr std::size_t kEntry = 0x18;
			if (capacity == 0 || capacity > (1ull << 24) || !entries || !Safe::Readable(entries, capacity * kEntry)) {
				return ids;
			}

			auto* lock = reinterpret_cast<RE::BSReadWriteLock*>(const_cast<std::byte*>(map + 0x30));
			Safe::BeginBatch();  // remembers readable regions while walking the forms
			lock->LockRead();
			for (std::uint64_t i = 0; i < capacity; ++i) {
				const auto* entry = entries + i * kEntry;
				if (*reinterpret_cast<const std::uint32_t*>(entry + 0x10) == 0xFFFFFFFF) {
					continue;  // empty slot
				}
				const auto* form = *reinterpret_cast<RE::TESForm* const*>(entry + 8);
				if (!form || !Safe::Readable(form, sizeof(RE::TESForm)) || !KeepsEditorIDInMap(form->GetFormType())) {
					continue;
				}
				if (auto text = Safe::FixedString(entry); !text.empty()) {
					ids.emplace(form->GetFormID(), std::move(text));
				}
			}
			lock->UnlockRead();
			Safe::BeginBatch();
			REX::INFO("Editor IDs: {} found for factions, packages, quests and material swaps", ids.size());
			return ids;
		}

		std::string EditorID(const RE::TESForm* a_form)
		{
			ScopedTimer timer{ "editor IDs" };
			if (!a_form) {
				return {};
			}
			auto id = Safe::CString(a_form->GetFormEditorID());
			if (id.empty() && KeepsEditorIDInMap(a_form->GetFormType())) {
				const auto& map = EditorIDMap();
				if (const auto it = map.find(a_form->GetFormID()); it != map.end()) {
					id = it->second;
				}
			}
			return id;
		}

		std::string Name(RE::TESForm* a_form)
		{
			ScopedTimer timer{ "names" };
			if (!a_form) {
				return {};
			}
			if (auto* ref = a_form->AsReference()) {
				return Safe::CString(ref->GetDisplayFullName());
			}
			if (auto* full = starfield_cast<RE::TESFullName*>(a_form)) {
				return Safe::CString(full->GetFullName());
			}
			return {};
		}

		std::string TypeName(const RE::TESForm* a_form)
		{
			if (!a_form) {
				return {};
			}
			const char* str = RE::FormTypeToString(a_form->GetFormType());
			return str ? std::string(str) : std::format("type {}", std::to_underlying(a_form->GetFormType()));
		}

		// "Name [EDID] (0001ABCD)", skipping the empty parts.
		std::string Describe(RE::TESForm* a_form)
		{
			if (!a_form) {
				return "none";
			}
			std::string out = Name(a_form);
			if (const auto edid = EditorID(a_form); !edid.empty()) {
				out += out.empty() ? edid : std::format(" [{}]", edid);
			}
			out += std::format("{}({})", out.empty() ? "" : " ", Hex(a_form->GetFormID()));
			return out;
		}

		// "EDID (0001ABCD, Plugin.esm)"; falls back to the name when there is no editor ID.
		std::string DescribeWithFile(RE::TESForm* a_form)
		{
			if (!a_form) {
				return "none";
			}
			auto label = EditorID(a_form);
			if (label.empty()) {
				label = Name(a_form);
			}
			const auto file = DefiningFileName(a_form->GetFormID());
			return std::format("{}{}({}{})", label, label.empty() ? "" : " ", Hex(a_form->GetFormID()),
				file.empty() ? "" : ", " + file);
		}

		std::string LastFileName(const RE::TESForm* a_form)
		{
			// The engine's "description owner" file: for plugin records, the override in effect.
			return a_form ? FileName(a_form->GetDescriptionOwnerFile()) : std::string();
		}

		void AddFileLines(InfoSection& a_section, const RE::TESForm* a_form)
		{
			ScopedTimer timer{ "plugin names" };
			const auto defined = DefiningFileName(a_form->GetFormID());
			const auto last = LastFileName(a_form);
			a_section.lines.push_back({ "Defined in", defined.empty() ? "unknown" : defined });
			if (!last.empty() && last != defined) {
				a_section.lines.push_back({ "Last changed by", last });
			}
		}

		std::string Join(const std::vector<std::string>& a_parts, std::string_view a_sep = ", ")
		{
			std::string out;
			for (const auto& p : a_parts) {
				if (p.empty()) continue;
				if (!out.empty()) out += a_sep;
				out += p;
			}
			return out;
		}

		std::string Flags(const RE::TESForm* a_form)
		{
			ScopedTimer timer{ "flags" };
			std::vector<std::string> flags;
			if (a_form->IsPersistent()) flags.emplace_back("persistent");
			if (a_form->IsDisabled()) flags.emplace_back("disabled");
			if (a_form->IsDeleted()) flags.emplace_back("deleted");
			if (a_form->IsCreated()) flags.emplace_back("created in game");
			return Join(flags);
		}

		std::string Point(const RE::NiPoint3& a_pt, float a_scale = 1.0f)
		{
			return std::format("{:.1f}, {:.1f}, {:.1f}", a_pt.x * a_scale, a_pt.y * a_scale, a_pt.z * a_scale);
		}

		std::string Model(RE::TESForm* a_form)
		{
			ScopedTimer timer{ "model" };
			if (auto* model = starfield_cast<RE::TESModel*>(a_form)) {
				return Safe::FixedString(&model->model);
			}
			return {};
		}

		std::string EngineDetails(const RE::TESForm* a_form)
		{
			ScopedTimer timer{ "engine details" };
			char buffer[512]{};
			a_form->GetFormDetailedString(buffer, sizeof(buffer));
			buffer[sizeof(buffer) - 1] = '\0';
			return buffer;
		}

		// A BSTArray's elements, only if the whole block is readable and the size is sane.
		template <class T>
		std::span<const T> SafeArray(const RE::BSTArray<T>& a_array, std::size_t a_maxSize = 4096)
		{
			const auto size = a_array.size();
			const auto* data = a_array.data();
			if (size == 0 || size > a_maxSize || !data || !Safe::Readable(data, size * sizeof(T))) {
				return {};
			}
			return { data, size };
		}

		std::string EscapeHTML(std::string_view a_text)
		{
			std::string out;
			out.reserve(a_text.size());
			for (const char c : a_text) {
				switch (c) {
				case '&': out += "&amp;"; break;
				case '<': out += "&lt;"; break;
				case '>': out += "&gt;"; break;
				case '"': out += "&quot;"; break;
				default: out += c; break;
				}
			}
			return out;
		}

		// ---- sections -----------------------------------------------------------------------------

		std::string Keywords(RE::TESForm* a_form, std::uint32_t a_max)
		{
			auto* kwForm = starfield_cast<RE::BGSKeywordForm*>(a_form);
			if (!kwForm || !Safe::Readable(kwForm, sizeof(RE::BGSKeywordForm))) {
				return {};
			}
			const auto               keywords = SafeArray(kwForm->keywords);
			std::vector<std::string> names;
			for (auto* ptr : keywords) {
				if (names.size() >= a_max) {
					break;
				}
				if (auto* kw = Safe::Form(ptr)) {
					auto edid = EditorID(kw);
					names.push_back(edid.empty() ? Hex(kw->GetFormID()) : edid);
				}
			}
			auto out = Join(names);
			if (keywords.size() > names.size() && !names.empty()) {
				out += std::format("  (+{} more)", keywords.size() - names.size());
			}
			return out;
		}

		void AddNPCSection(std::vector<InfoSection>& a_out, RE::Actor* a_actor, RE::TESNPC* a_npc)
		{
			const auto& settings = Settings::Get();
			InfoSection sec{ "NPC", {} };

			if (auto* race = Safe::Form(a_actor->race)) {
				sec.lines.push_back({ "Race", DescribeWithFile(race) });
			}

			if (a_npc && Safe::Readable(a_npc, sizeof(RE::TESNPC))) {
				const RE::TESActorBaseData& data = *a_npc;  // base class, no cast needed
				using Flag = RE::ACTOR_BASE_DATA::Flag;
				const auto& flags = data.actorData.actorBaseFlags;

				std::vector<std::string> traits;
				if (flags.all(Flag::kEssential)) traits.emplace_back("essential");
				if (flags.all(Flag::kProtected)) traits.emplace_back("protected");
				if (flags.all(Flag::kUnique)) traits.emplace_back("unique");
				if (flags.all(Flag::kInvulnerable)) traits.emplace_back("invulnerable");
				if (flags.all(Flag::kIsGhost)) traits.emplace_back("ghost");
				if (flags.all(Flag::kRespawn)) traits.emplace_back("respawns");
				if (flags.all(Flag::kSimpleActor)) traits.emplace_back("simple actor");
				traits.emplace_back(flags.all(Flag::kFemale) ? "female" : "male");
				sec.lines.push_back({ "Traits", Join(traits) });

				if (flags.all(Flag::kPCLevelMult)) {
					const auto min = data.actorData.calcLevelMin;
					const auto max = data.actorData.calcLevelMax;
					sec.lines.push_back({ "Level", max == 0 ? std::format("scales with player, at least {}", min)
					                                        : std::format("scales with player, {} to {}", min, max) });
				} else {
					sec.lines.push_back({ "Level", std::to_string(data.actorData.level) });
				}
				if (flags.all(Flag::kUsesTemplate)) {
					sec.lines.push_back({ "Template", "uses a leveled template (values above may come from it)" });
				}

				if (settings.showFactions) {
					std::vector<std::string> factions;
					for (const auto& entry : SafeArray(data.factions, 512)) {
						auto* faction = Safe::FormAs<RE::TESFaction>(entry.faction);
						if (!faction) {
							continue;
						}
						const auto  edid = EditorID(faction);
						std::string text = edid.empty() ? std::format("{} {}", Hex(faction->GetFormID()), entry.rank)
						                                : std::format("{} ({}) {}", edid, Hex(faction->GetFormID()), entry.rank);
						if (Address::Get().factionRank) {
							const auto live = a_actor->GetFactionRank(faction, a_actor->GetFormID() == 0x14);
							if (live != entry.rank) {
								text += std::format(" (now {})", live);
							}
						}
						factions.push_back(std::move(text));
					}
					if (!factions.empty()) {
						sec.lines.push_back({ "Factions", Join(factions) });
					}
				}
			}

			std::vector<std::string> state;
			if (a_actor->IsDead()) state.emplace_back("dead");
			if (a_actor->IsInCombatSearch()) state.emplace_back("searching (combat)");
			if (a_actor->IsInWater()) state.emplace_back("in water");
			if (!state.empty()) {
				sec.lines.push_back({ "State", Join(state) });
			}

			if (!sec.lines.empty()) {
				a_out.push_back(std::move(sec));
			}
		}

		const char* CommandName(RE::COMMAND_TYPE a_type)
		{
			using C = RE::COMMAND_TYPE;
			switch (a_type) {
			case C::kNone: return "none";
			case C::kCall: return "call";
			case C::kFollow: return "follow";
			case C::kMove: return "move";
			case C::kAttack: return "attack";
			case C::kInspect: return "inspect";
			case C::kRetrieve: return "retrieve";
			case C::kStay: return "stay";
			case C::kRelease: return "release";
			case C::kHeal: return "heal";
			case C::kAssign: return "assign";
			case C::kRide: return "ride";
			case C::kEnter: return "enter";
			default: return nullptr;
			}
		}

		// "name" -> float, "bName" -> bool, "iName" -> int; or explicit "name:f|i|b".
		struct GraphVar
		{
			std::string name;
			char        type;
		};

		std::vector<GraphVar> ParseGraphVariables(std::string_view a_list)
		{
			std::vector<GraphVar> out;
			std::string           token;
			auto                  flush = [&]() {
                if (token.empty()) return;
                char type = 'f';
                if (const auto colon = token.find(':'); colon != std::string::npos) {
                    type = static_cast<char>(std::tolower(static_cast<unsigned char>(token[colon + 1 < token.size() ? colon + 1 : colon])));
                    token.erase(colon);
                } else if (token.size() > 1 && std::isupper(static_cast<unsigned char>(token[1]))) {
                    if (token[0] == 'b') type = 'b';
                    else if (token[0] == 'i') type = 'i';
                }
                if (type != 'b' && type != 'i') type = 'f';
                if (!token.empty() && out.size() < 24) out.push_back({ token, type });
                token.clear();
			};
			for (const char c : a_list) {
				if (c == ',' || c == ';' || std::isspace(static_cast<unsigned char>(c))) {
					flush();
				} else {
					token += c;
				}
			}
			flush();
			return out;
		}

		void AddAISection(std::vector<InfoSection>& a_out, RE::Actor* a_actor)
		{
			const auto& settings = Settings::Get();
			InfoSection sec{ "AI & animation", {} };

			auto* process = a_actor->currentProcess;
			if (process && Safe::Readable(process, sizeof(RE::AIProcess))) {
				auto* middleHigh = Safe::Readable(process->middleHigh, sizeof(RE::MiddleHighProcessData)) ? process->middleHigh : nullptr;
				auto* high = Safe::Readable(process->high, sizeof(RE::HighProcessData)) ? process->high : nullptr;

				// Package: a run-once package (scripts, scenes) overrides the normal one while it runs.
				const RE::ActorPackage* running = &process->currentPackage;
				bool                    runOnce = false;
				if (middleHigh && Safe::FormAs<RE::TESPackage>(middleHigh->runOncePackage.package)) {
					running = &middleHigh->runOncePackage;
					runOnce = true;
				}
				if (auto* package = Safe::FormAs<RE::TESPackage>(running->package)) {
					sec.lines.push_back({ "Package", DescribeWithFile(package) });
					// The procedure index is at +0x1C (CommonLibSF's ActorPackage puts it at 0x20, which holds
					// the package's start time). Shown only when the value is a plausible index.
					std::int32_t procedure = -1;
					Safe::Read(reinterpret_cast<const std::byte*>(running) + 0x1C, procedure);
					sec.lines.push_back({ "Package info", std::format("type {}{}{}",
						static_cast<int>(package->packageData.packageType),
						procedure >= 0 && procedure < 256 ? std::format(", step {}", procedure) : "",
						runOnce ? ", run-once override" : "") });
					if (runOnce) {
						if (auto* normal = Safe::FormAs<RE::TESPackage>(process->currentPackage.package)) {
							sec.lines.push_back({ "Normal package", DescribeWithFile(normal) });
						}
					}
				}

				if (high) {
					if (auto* idle = Safe::FormAs<RE::TESIdleForm>(high->idleToPlay)) {
						sec.lines.push_back({ "Queued idle", DescribeWithFile(idle) });
						const auto event = Safe::FixedString(&idle->animEventName);
						const auto graph = Safe::FixedString(&idle->behaviorGraphName);
						const auto file = Safe::FixedString(&idle->animFileName);
						if (!event.empty() || !graph.empty()) {
							sec.lines.push_back({ "Idle event", Join({ event, graph.empty() ? "" : "graph " + graph }) });
						}
						if (!file.empty()) {
							sec.lines.push_back({ "Idle file", file });
						}
					}

					const auto subtitle = Safe::FixedString(&high->strVoiceSubtitle, 300);
					if (!subtitle.empty()) {
						sec.lines.push_back({ high->capLinePlaying ? "Saying" : "Last line", subtitle });
					}
					if (high->inCommandState && high->commandType != RE::COMMAND_TYPE::kNone) {
						const char* name = CommandName(high->commandType);
						sec.lines.push_back({ "Command", name ? name : std::format("type {}", std::to_underlying(high->commandType)) });
					}
					sec.lines.push_back({ "Light level", std::format("{:.1f}", high->lightLevel) });
				}

				if (middleHigh && Address::Get().furniture && middleHigh->currentFurniture) {
					const auto furniture = middleHigh->currentFurniture.get();
					if (auto* ref = Safe::Form(furniture.get())) {
						sec.lines.push_back({ "Furniture", Describe(ref) });
					}
				}
			}

			if (settings.showGraphVariables) {
				auto&                    holder = static_cast<RE::IAnimationGraphManagerHolder&>(*a_actor);
				std::vector<std::string> values;
				for (const auto& var : ParseGraphVariables(settings.graphVariables)) {
					const RE::BSFixedString name{ var.name.c_str() };
					bool                    ok = false;
					std::string             text;
					if (var.type == 'b') {
						bool v{};
						ok = holder.GetGraphVariableImplBool(name, v);
						text = v ? "true" : "false";
					} else if (var.type == 'i') {
						std::int32_t v{};
						ok = holder.GetGraphVariableImplInt(name, v);
						text = std::to_string(v);
					} else {
						float v{};
						ok = holder.GetGraphVariableImplFloat(name, v);
						text = std::format("{:.2f}", v);
					}
					values.push_back(std::format("{}={}", var.name, ok ? text : "n/a"));
				}
				if (!values.empty()) {
					sec.lines.push_back({ "Graph", Join(values) });
				}
			}

			if (!sec.lines.empty()) {
				a_out.push_back(std::move(sec));
			}
		}

		void AddExperimentalActor(std::vector<InfoSection>& a_out, RE::Actor* a_actor)
		{
			const auto& settings = Settings::Get();
			InfoSection sec{ "Actor (experimental)", {} };

			if (settings.showActorState) {
				std::vector<std::string> state;
				if (a_actor->IsInCombat()) state.emplace_back("in combat");
				if (static_cast<RE::TESObjectREFR*>(a_actor)->IsInvulnerable()) state.emplace_back("invulnerable");
				if (a_actor->IsTalking()) state.emplace_back("talking");
				sec.lines.push_back({ "State", state.empty() ? "-" : Join(state) });
				if (auto* scene = Safe::Form(a_actor->GetCurrentScene())) {
					sec.lines.push_back({ "Scene", DescribeWithFile(scene) });
				}
			}

			if (settings.showActorValues && Address::Get().actorValues) {
				auto* avs = RE::ActorValue::GetSingleton();
				if (avs && Safe::Readable(avs, sizeof(RE::ActorValue))) {
					auto&                    owner = static_cast<RE::ActorValueOwner&>(*a_actor);
					std::vector<std::string> values;
					const std::pair<const char*, RE::ActorValueInfo*> list[] = {
						{ "Health", avs->health }, { "Oxygen", avs->oxygen }, { "Carry", avs->carryWeight },
						{ "Speed", avs->speedMult }, { "Aggression", avs->aggression }, { "Confidence", avs->confidence },
						{ "Assistance", avs->assistance }, { "Morality", avs->morality },
					};
					for (const auto& [label, info] : list) {
						if (auto* form = Safe::FormAs<RE::ActorValueInfo>(info)) {
							const auto current = owner.GetActorValue(*form);
							const auto base = owner.GetBaseActorValue(*form);
							values.push_back(current == base ? std::format("{} {:.0f}", label, current) : std::format("{} {:.0f}/{:.0f}", label, current, base));
						}
					}
					if (!values.empty()) {
						sec.lines.push_back({ "Values", Join(values) });
					}
				}
			}

			if (!sec.lines.empty()) {
				a_out.push_back(std::move(sec));
			}
		}

		// The reference's own lock entry, read directly (fallback when the game's lock getter is unavailable).
		RE::REFR_LOCK* OwnLock(RE::TESObjectREFR* a_ref)
		{
			if (!Address::Get().inventory) {  // GetByType takes the extra-data read lock
				return nullptr;
			}
			auto* list = a_ref->extraDataList.get();
			if (!list || !Safe::Readable(list, sizeof(RE::ExtraDataList))) {
				return nullptr;
			}
			auto* extra = list->GetByType<RE::ExtraLock>();
			if (!extra || !Safe::Readable(extra, sizeof(RE::ExtraLock)) || !Safe::Readable(extra->lock, sizeof(RE::REFR_LOCK))) {
				return nullptr;
			}
			return extra->lock;
		}

		template <class T>
		bool ReadGlobal(REL::ID a_id, T& a_out)
		{
			if (!Address::IsPresent(a_id.id())) {
				return false;
			}
			return Safe::Read(reinterpret_cast<const void*>(REL::Relocation<std::uintptr_t>{ a_id }.address()), a_out);
		}

		std::string GameSettingString(REL::ID a_id)
		{
			const char* text = nullptr;
			return ReadGlobal(a_id, text) ? Safe::CString(text, 64) : std::string();
		}

		// The game's lock-level name for a level, exactly as REFR_LOCK::GetLockLevel (ID 45439) and its
		// level-to-category step (ID 45441) do it: the first rung whose iLockLevelMax* is >= the level,
		// in ladder order; above every rung, "Requires Key" (iLockLevelMaxImpossible's name).
		std::string LockCategory(std::int32_t a_level)
		{
			for (std::size_t i = 0; i < std::size(GameID::kLockLadder); ++i) {
				std::int32_t max = 0;
				if (!ReadGlobal(GameID::kLockLadder[i].maxValue, max)) {
					return {};
				}
				if (a_level <= max || i == GameID::kLockImpossible) {
					return GameSettingString(GameID::kLockLadder[i].name);
				}
			}
			return {};
		}

		void AddLockSection(std::vector<InfoSection>& a_out, RE::TESObjectREFR* a_ref)
		{
			RE::REFR_LOCK* own = OwnLock(a_ref);
			RE::REFR_LOCK* lock = own;
			const bool     details = Address::Get().lockDetails;
			if (details) {
				using GetLock_t = RE::REFR_LOCK*(RE::TESObjectREFR*);
				static REL::Relocation<GetLock_t*> getLock{ GameID::GetLock };
				lock = getLock(a_ref);
			}
			if (!lock || !Safe::Readable(lock, sizeof(RE::REFR_LOCK))) {
				return;
			}

			InfoSection sec{ "Lock", {} };
			const std::int32_t base = static_cast<std::uint8_t>(lock->baseLevel);
			std::int32_t       level = base;
			std::string        how;

			// Same rule as the GetLockLevel console command: leveled locks (flag 0x04) add the area
			// level times fLeveledLockMult (converted to an integer by truncation), capped at 99,
			// unless the lock is set to iLockLevelMaxImpossible ("Requires Key").
			std::int32_t impossible = 255;
			if (details) {
				ReadGlobal(GameID::kLockLadder[GameID::kLockImpossible].maxValue, impossible);
			}
			if (lock->flags.all(RE::REFR_LOCK::Flag::kLeveled) && base != (impossible & 0xFF)) {
				if (details) {
					using GetAreaLevel_t = std::uint16_t(RE::TESObjectREFR*, bool);
					static REL::Relocation<GetAreaLevel_t*> getAreaLevel{ GameID::GetAreaLevel };
					float mult = 0.0f;
					ReadGlobal(GameID::fLeveledLockMult, mult);
					const auto area = getAreaLevel(a_ref, false);
					level = std::min(99, base + static_cast<std::int32_t>(area) * static_cast<std::int32_t>(mult));
					how = std::format(", leveled: base {} + area level {} x {}", base, area, static_cast<std::int32_t>(mult));
				} else {
					how = ", leveled";
				}
			}

			const auto category = details ? LockCategory(level) : std::string();
			sec.lines.push_back({ "Lock", std::format("{}, {}{}{}", lock->IsLocked() ? "locked" : "unlocked",
											  category.empty() ? std::format("level {}", level) : std::format("{} ({})", category, level),
											  how, (details && !own) ? " (from the linked door)" : "") });
			if (auto* key = Safe::Form(lock->key)) {
				sec.lines.push_back({ "Key", Describe(key) });
			}
			a_out.push_back(std::move(sec));
		}

		void AddInventorySection(std::vector<InfoSection>& a_out, RE::TESObjectREFR* a_ref)
		{
			const auto& settings = Settings::Get();
			if (!Address::Get().inventory) {
				return;
			}
			struct Entry
			{
				std::string   name;
				std::uint64_t count;
				bool          equipped;
			};
			std::vector<Entry> entries;
			std::size_t        distinct = 0;
			std::uint64_t      total = 0;

			a_ref->ForEachInventoryItem([&](const RE::BGSInventoryItem& a_item) {
				if (distinct >= 4096) {
					return RE::BSContainer::ForEachResult::kStop;
				}
				auto* object = Safe::Form(a_item.object);
				if (!object) {
					return RE::BSContainer::ForEachResult::kContinue;
				}
				std::uint64_t count = 0;
				for (const auto& stack : SafeArray(a_item.stacks, 1024)) {
					if (stack.count > 0 && stack.count < 1'000'000) {
						count += stack.count;
					}
				}
				++distinct;
				total += count;
				if (entries.size() < settings.maxInventoryItems) {
					auto name = Name(object);
					if (name.empty()) name = EditorID(object);
					if (name.empty()) name = Hex(object->GetFormID());
					entries.push_back({ std::move(name), count, a_item.IsEquipped() });
				}
				return RE::BSContainer::ForEachResult::kContinue;
			});

			if (distinct == 0) {
				return;
			}
			InfoSection sec{ "Inventory", {} };
			sec.lines.push_back({ "Items", std::format("{} kinds, {} total", distinct, total) });
			std::vector<std::string> parts;
			for (const auto& e : entries) {
				parts.push_back(std::format("{}{}{}", e.name, e.count > 1 ? std::format(" x{}", e.count) : "", e.equipped ? " [E]" : ""));
			}
			if (!parts.empty()) {
				sec.lines.push_back({ "First", Join(parts) + (distinct > entries.size() ? ", ..." : "") });
			}
			a_out.push_back(std::move(sec));
		}

		void AddPositionSection(std::vector<InfoSection>& a_out, RE::TESObjectREFR* a_ref)
		{
			const auto& settings = Settings::Get();
			InfoSection sec{ "Position", {} };
			sec.lines.push_back({ "Position", Point(a_ref->GetPosition()) });
			sec.lines.push_back({ "Rotation", Point(a_ref->GetAngle(), kRadToDeg) + " deg" });
			// TESObjectREFR::GetScale has no Address Library ID for 1.16.244 in this CommonLibSF
			// fork (calling it aborts the game), so read the stored value: hundredths, 100 = 1.0.
			if (const auto rawScale = a_ref->scale; rawScale > 0 && rawScale <= 10000) {
				sec.lines.push_back({ "Scale", std::format("{:.2f}", rawScale / 100.0) });
			}
			if (auto* cell = Safe::FormAs<RE::TESObjectCELL>(a_ref->parentCell)) {
				sec.lines.push_back({ "Cell", std::format("{}{}", Describe(cell), cell->IsInterior() ? "  interior" : "  exterior") });
				sec.lines.push_back({ "Cell from", DefiningFileName(cell->GetFormID()) });
			}
			if (Address::Get().location) {
				if (auto* location = Safe::FormAs<RE::BGSLocation>(a_ref->GetCurrentLocation())) {
					sec.lines.push_back({ "Location", Describe(location) });
					if (settings.showLocationChain) {
						std::vector<std::string> chain;
						auto*                    parent = Safe::FormAs<RE::BGSLocation>(location->parentLocation.get());
						for (int depth = 0; parent && depth < 8; ++depth) {
							auto name = Name(parent);
							chain.push_back(name.empty() ? EditorID(parent) : name);
							parent = Safe::FormAs<RE::BGSLocation>(parent->parentLocation.get());
						}
						if (!chain.empty()) {
							sec.lines.push_back({ "Inside", Join(chain, " > ") });
						}
					}
				}
			}
			if (settings.showBounds) {
				const auto min = a_ref->GetBoundMin();
				const auto max = a_ref->GetBoundMax();
				sec.lines.push_back({ "Size", std::format("{:.1f} x {:.1f} x {:.1f}", max.x - min.x, max.y - min.y, max.z - min.z) });
			}
			if (settings.showGravity) {
				sec.lines.push_back({ "Gravity", std::format("scale {:.2f}{}", a_ref->GetGravityScale(), a_ref->IsInZeroGravity() ? ", zero-g" : "") });
			}
			a_out.push_back(std::move(sec));
		}
		// ---- biped slots, armor addons, material swaps --------------------------------------------
		// Offsets come from GameID::Layout (confirmed on 1.16.244; several differ from the fork), so
		// every read goes through SafeMemory and every pointer is checked against the form table.

		namespace L = GameID::Layout;

		const std::byte* At(const void* a_base, std::size_t a_offset)
		{
			return static_cast<const std::byte*>(a_base) + a_offset;
		}

		// The engine's array header: { u32 size; u32 capacity; T* data }.
		struct RawArray
		{
			std::uint32_t    size;
			std::uint32_t    capacity;
			const std::byte* data;
		};
		static_assert(sizeof(RawArray) == 0x10);

		bool ReadRawArray(const void* a_address, std::size_t a_stride, std::size_t a_max, RawArray& a_out)
		{
			return Safe::Read(a_address, a_out) && a_out.size > 0 && a_out.size <= a_max && a_out.data &&
			       Safe::Readable(a_out.data, a_out.size * a_stride);
		}

		std::uint64_t ReadMask(const void* a_form, std::size_t a_offset)
		{
			std::uint64_t mask = 0;
			Safe::Read(At(a_form, a_offset), mask);
			return mask;
		}

		// Biped slot bit indices, e.g. "3, 5, 33".
		std::string SlotList(std::uint64_t a_mask)
		{
			std::vector<std::string> bits;
			for (std::uint32_t i = 0; i < 64; ++i) {
				if (a_mask & (1ull << i)) {
					bits.push_back(std::to_string(i));
				}
			}
			return bits.empty() ? "none" : Join(bits);
		}

		std::string ShortName(RE::TESForm* a_form)
		{
			auto name = Name(a_form);
			if (name.empty()) name = EditorID(a_form);
			if (name.empty()) name = Hex(a_form->GetFormID());
			return name;
		}

		// Receives the swap list from GameID::GetModelMaterialSwaps. The engine holds its write lock
		// while calling Visit, so this only copies pointers into fixed storage: no allocation, no
		// calls back into the game.
		class SwapCollector
		{
		public:
			virtual ~SwapCollector() = default;                // vtable slot 0
			virtual void Visit(const RawArray* a_swaps)        // vtable slot 1
			{
				if (!a_swaps || !Safe::Readable(a_swaps, sizeof(RawArray))) {
					return;
				}
				const auto n = std::min<std::uint32_t>(a_swaps->size, kMax);
				if (n == 0 || !a_swaps->data || !Safe::Readable(a_swaps->data, n * sizeof(std::uintptr_t))) {
					return;
				}
				std::memcpy(items.data(), a_swaps->data, n * sizeof(std::uintptr_t));
				count = n;
				total = a_swaps->size;
			}

			static constexpr std::uint32_t        kMax = 16;
			std::array<std::uintptr_t, kMax> items{};
			std::uint32_t                         count{ 0 };
			std::uint32_t                         total{ 0 };
		};

		// Layered material swap forms attached to one model component (TESModel or a subclass).
		std::vector<RE::TESForm*> MaterialSwapsOf(const void* a_model, std::uint32_t& a_total)
		{
			a_total = 0;
			std::vector<RE::TESForm*> out;
			if (!a_model || !Address::Get().materialSwaps || !Safe::Readable(a_model, 0x10)) {
				return out;
			}
			using func_t = void(const void*, SwapCollector*, bool);
			static REL::Relocation<func_t*> func{ GameID::GetModelMaterialSwaps };
			SwapCollector collector;
			func(a_model, &collector, false);  // false: never create an entry for a model that has none

			a_total = collector.total;
			for (std::uint32_t i = 0; i < collector.count; ++i) {
				const auto item = collector.items[i];
				// Pointers once data has loaded; accept a bare form ID too, just in case.
				auto* form = Safe::Form(reinterpret_cast<const void*>(item));
				if (!form && item != 0 && item <= 0xFFFFFFFF) {
					form = Safe::Form(RE::TESForm::LookupByID(static_cast<RE::TESFormID>(item)));
				}
				if (form) {
					out.push_back(form);
				}
			}
			return out;
		}

		// "material -> override; ..." for one BGSLayeredMaterialSwap.
		std::string SwapEntries(RE::TESForm* a_swap, std::uint32_t a_max)
		{
			if (a_swap->GetFormType() != RE::FormType::kLMSW || a_max == 0) {
				return {};
			}
			RawArray entries{};
			if (!ReadRawArray(At(a_swap, L::LMSW_Entries), L::LMSW_EntrySize, 4096, entries)) {
				return "no entries";
			}
			std::vector<std::string> parts;
			for (std::uint32_t i = 0; i < entries.size && parts.size() < a_max; ++i) {
				const auto* entry = entries.data + i * L::LMSW_EntrySize;
				auto from = Safe::FixedString(entry + L::LMSW_Entry_Key);
				auto to = Safe::FixedString(entry + L::LMSW_Entry_Override);
				if (from.empty() && to.empty()) {
					continue;
				}
				parts.push_back(std::format("{} -> {}", from.empty() ? "?" : from, to.empty() ? "(none)" : to));
			}
			auto out = Join(parts, "; ");
			if (entries.size > parts.size()) {
				out += std::format("{}({} entries)", out.empty() ? "" : "; ... ", entries.size);
			}
			return out;
		}

		void AddSwapLines(InfoSection& a_sec, const void* a_model, std::string_view a_label)
		{
			const auto& settings = Settings::Get();
			if (!settings.showMaterialSwaps) {
				return;
			}
			std::uint32_t total = 0;
			const auto    swaps = MaterialSwapsOf(a_model, total);
			for (auto* swap : swaps) {
				a_sec.lines.push_back({ std::string(a_label), DescribeWithFile(swap) });
				if (const auto entries = SwapEntries(swap, settings.maxSwapEntries); !entries.empty()) {
					a_sec.lines.push_back({ std::string(a_label.size(), ' '), entries });
				}
			}
			if (total > swaps.size()) {
				a_sec.lines.push_back({ std::string(a_label), std::format("... {} in all", total) });
			}
		}

		bool AddonFitsRace(const void* a_arma, const RE::TESForm* a_race)
		{
			if (!a_race) {
				return false;
			}
			const void* race = nullptr;
			if (Safe::Read(At(a_arma, L::ARMA_Race), race) && race == a_race) {
				return true;
			}
			RawArray extra{};
			if (ReadRawArray(At(a_arma, L::ARMA_AdditionalRaces), sizeof(void*), 256, extra)) {
				for (std::uint32_t i = 0; i < extra.size; ++i) {
					const void* other = nullptr;
					if (Safe::Read(extra.data + i * sizeof(void*), other) && other == a_race) {
						return true;
					}
				}
			}
			return false;
		}

		// The armor addon the engine would pick for this race (the first match), else the first one.
		RE::TESForm* PickAddon(const void* a_armo, const RE::TESForm* a_race, std::uint32_t& a_count, bool& a_matched)
		{
			a_count = 0;
			a_matched = false;
			RawArray addons{};
			if (!ReadRawArray(At(a_armo, L::ARMO_Addons), 0x10, 256, addons)) {
				return nullptr;
			}
			RE::TESForm* first = nullptr;
			for (std::uint32_t i = 0; i < addons.size; ++i) {
				const void* ptr = nullptr;
				Safe::Read(addons.data + i * 0x10 + 8, ptr);
				auto* arma = Safe::Form(ptr);
				if (!arma || arma->GetFormType() != RE::FormType::kARMA) {
					continue;
				}
				++a_count;
				if (!first) {
					first = arma;
				}
				if (!a_matched && AddonFitsRace(arma, a_race)) {
					first = arma;
					a_matched = true;
				}
			}
			return first;
		}

		void AddWornSection(std::vector<InfoSection>& a_out, RE::Actor* a_actor, RE::TESNPC* a_npc)
		{
			const auto& settings = Settings::Get();
			if (!Address::Get().inventory || settings.maxWornItems == 0) {
				return;
			}
			std::vector<RE::TESForm*> worn;
			a_actor->ForEachInventoryItem([&](const RE::BGSInventoryItem& a_item) {
				if (worn.size() >= 64) {
					return RE::BSContainer::ForEachResult::kStop;
				}
				if (a_item.IsEquipped()) {
					if (auto* object = Safe::Form(a_item.object)) {
						worn.push_back(object);
					}
				}
				return RE::BSContainer::ForEachResult::kContinue;
			});
			if (worn.empty()) {
				return;
			}

			bool female = false;
			if (a_npc && Safe::Readable(a_npc, sizeof(RE::TESNPC))) {
				const RE::TESActorBaseData& data = *a_npc;
				female = data.actorData.actorBaseFlags.all(RE::ACTOR_BASE_DATA::Flag::kFemale);
			}
			const auto* race = Safe::Form(a_actor->race);

			InfoSection   sec{ "Worn", {} };
			std::uint64_t covered = 0;
			std::uint64_t overlap = 0;
			std::size_t   shown = 0;
			for (auto* object : worn) {
				const bool isArmor = object->GetFormType() == RE::FormType::kARMO;
				const auto mask = isArmor ? ReadMask(object, L::ARMO_BipedSlots) : 0;
				overlap |= covered & mask;
				covered |= mask;
				if (shown >= settings.maxWornItems) {
					continue;
				}
				++shown;
				if (!isArmor) {
					sec.lines.push_back({ ShortName(object), std::format("{} ({})", TypeName(object), Hex(object->GetFormID())) });
					continue;
				}
				sec.lines.push_back({ ShortName(object), std::format("slots {}  ({}, {})", SlotList(mask), Hex(object->GetFormID()),
					LastFileName(object).empty() ? DefiningFileName(object->GetFormID()) : LastFileName(object)) });
				if (!settings.showWornModels) {
					continue;
				}
				std::uint32_t count = 0;
				bool          matched = false;
				auto*         arma = PickAddon(object, race, count, matched);
				if (!arma) {
					continue;
				}
				const auto armaMask = ReadMask(arma, L::ARMA_BipedSlots);
				sec.lines.push_back({ "  addon", std::format("{}{}{}{}", DescribeWithFile(arma),
					armaMask != mask ? "  slots " + SlotList(armaMask) : "",
					count > 1 ? std::format("  (1 of {})", count) : "",
					matched ? "" : "  (no race match)") });
				// The model for the actor's sex, falling back to the other one (many addons set only male).
				const void* model = At(arma, L::ARMA_BipedModel[female ? 1 : 0]);
				auto        path = Safe::FixedString(At(model, L::TESModel_Path));
				if (path.empty()) {
					model = At(arma, L::ARMA_BipedModel[female ? 0 : 1]);
					path = Safe::FixedString(At(model, L::TESModel_Path));
				}
				if (!path.empty()) {
					sec.lines.push_back({ "  model", path });
				}
				AddSwapLines(sec, model, "  swap");
			}
			sec.lines.insert(sec.lines.begin(), InfoLine{ "Slots used", covered ? SlotList(covered) : "none" });
			if (overlap) {
				sec.lines.insert(sec.lines.begin() + 1, InfoLine{ "Overlap", SlotList(overlap) });
			}
			if (worn.size() > shown) {
				sec.lines.push_back({ "...", std::format("{} more", worn.size() - shown) });
			}
			a_out.push_back(std::move(sec));
		}
	}

	std::string DefiningFileName(RE::TESFormID a_formID)
	{
		const auto top = a_formID >> 24;
		if (top == 0xFF) {
			return "(created in save)";
		}
		if (!Address::Get().dataHandler) {
			return {};
		}
		auto* handler = RE::TESDataHandler::GetSingleton();
		if (!handler) {
			return {};
		}
		const auto& files = handler->compiledFileCollection;

		if (top == 0xFE) {  // small master: FE xxx yyy
			const auto index = (a_formID >> 12) & 0xFFF;
			return index < files.smallFiles.size() ? FileName(files.smallFiles[index]) : std::string();
		}
		if (top == 0xFD) {  // medium master: FD xx yyyy
			const auto index = (a_formID >> 16) & 0xFF;
			return index < files.mediumFiles.size() ? FileName(files.mediumFiles[index]) : std::string();
		}
		return top < files.files.size() ? FileName(files.files[top]) : std::string();
	}

	std::vector<InfoSection> CollectReferenceInfo(RE::TESObjectREFR* a_ref)
	{
		const auto&              settings = Settings::Get();
		std::vector<InfoSection> sections;
		Timings().clear();
		Safe::QueryCount() = 0;
		Safe::BeginBatch();
		struct EndBatch
		{
			~EndBatch() { Safe::BeginBatch(); }  // nothing remembered outlives this collection
		} endBatch;
		if (!a_ref) {
			return sections;
		}

		// Read the base object without copying the NiPointer (a copy's destructor calls
		// TESForm::DecRefCount), and only trust it if the form table agrees it is a form.
		auto* base = static_cast<RE::TESBoundObject*>(Safe::Form(a_ref->data.objectReference.get()));
		auto* actor = a_ref->As<RE::Actor>();

		if (settings.showReference) {
			ScopedTimer timer{ "[Reference]" };
			InfoSection ref{ "Reference", {} };
			ref.lines.push_back({ "Name", Name(a_ref).empty() ? "(no name)" : Name(a_ref) });
			ref.lines.push_back({ "Ref ID", std::format("{}  ({})", Hex(a_ref->GetFormID()), TypeName(a_ref)) });
			if (const auto edid = EditorID(a_ref); !edid.empty()) {
				ref.lines.push_back({ "Editor ID", edid });
			}
			AddFileLines(ref, a_ref);
			if (const auto flags = Flags(a_ref); !flags.empty()) {
				ref.lines.push_back({ "Flags", flags });
			}
			if (settings.showEngineDetails) {
				ref.lines.push_back({ "Engine", EngineDetails(a_ref) });
			}
			sections.push_back(std::move(ref));
		}

		if (settings.showBase && base) {
			ScopedTimer timer{ "[Base object]" };
			InfoSection sec{ "Base object", {} };
			sec.lines.push_back({ "Name", Name(base).empty() ? "(no name)" : Name(base) });
			sec.lines.push_back({ "Base ID", std::format("{}  ({})", Hex(base->GetFormID()), TypeName(base)) });
			if (const auto edid = EditorID(base); !edid.empty()) {
				sec.lines.push_back({ "Editor ID", edid });
			}
			AddFileLines(sec, base);
			if (settings.showModel) {
				if (const auto model = Model(base); !model.empty()) {
					sec.lines.push_back({ "Model", model });
				}
			}
			if (settings.showMaterialSwaps) {
				AddSwapLines(sec, starfield_cast<RE::TESModel*>(base), "Material swap");
			}
			if (settings.showKeywords && settings.maxKeywords > 0) {
				ScopedTimer kwTimer{ "keywords" };
				if (const auto kws = Keywords(base, settings.maxKeywords); !kws.empty()) {
					sec.lines.push_back({ "Keywords", kws });
				}
			}
			if (settings.showEngineDetails) {
				sec.lines.push_back({ "Engine", EngineDetails(base) });
			}
			sections.push_back(std::move(sec));
		}

		if (actor) {
			if (settings.showNPC) {
				{
					ScopedTimer timer{ "[NPC]" };
					AddNPCSection(sections, actor, base ? base->As<RE::TESNPC>() : nullptr);
				}
			}
			if (settings.showAI) {
				{
					ScopedTimer timer{ "[AI]" };
					AddAISection(sections, actor);
				}
			}
			if (settings.showWorn) {
				{
					ScopedTimer timer{ "[Worn]" };
					AddWornSection(sections, actor, base ? base->As<RE::TESNPC>() : nullptr);
				}
			}
			if (settings.showActorState || settings.showActorValues) {
				{
					ScopedTimer timer{ "[Actor experimental]" };
					AddExperimentalActor(sections, actor);
				}
			}
		} else if (settings.showLock) {
			{
				ScopedTimer timer{ "[Lock]" };
				AddLockSection(sections, a_ref);
			}
		}

		if (settings.showInventory && settings.maxInventoryItems > 0) {
			{
				ScopedTimer timer{ "[Inventory]" };
				AddInventorySection(sections, a_ref);
			}
		}

		if (settings.showPosition) {
			{
				ScopedTimer timer{ "[Position]" };
				AddPositionSection(sections, a_ref);
			}
		}

		return sections;
	}

	std::string LastCollectTimings()
	{
		std::string out;
		for (const auto& [name, us] : Timings()) {
			if (us >= 1000) {
				out += std::format("{}{} {} ms", out.empty() ? "" : ", ", name, us / 1000);
			}
		}
		return std::format("{}; {} memory checks", out.empty() ? "no part over 1 ms" : out, Safe::QueryCount());
	}

	std::string RenderHTML(const std::vector<InfoSection>& a_sections)
	{
		const auto& settings = Settings::Get();
		std::string out;
		for (const auto& section : a_sections) {
			if (!out.empty()) {
				out += "<br>";
			}
			out += std::format("<font color='#{:06X}'>{}</font><br>", settings.headerColor & 0xFFFFFF, EscapeHTML(section.title));
			for (const auto& line : section.lines) {
				if (line.label.find_first_not_of(' ') == std::string::npos) {
					out += std::format("&nbsp;&nbsp;&nbsp;&nbsp;{}<br>", EscapeHTML(line.value));  // continues the line above
					continue;
				}
				out += std::format("<font color='#{:06X}'>{}:</font> {}<br>", settings.labelColor & 0xFFFFFF,
					EscapeHTML(line.label), EscapeHTML(line.value));
			}
		}
		return out;
	}

	std::string RenderPlain(const std::vector<InfoSection>& a_sections)
	{
		std::string out;
		for (const auto& section : a_sections) {
			out += std::format("-- {} --\n", section.title);
			for (const auto& line : section.lines) {
				if (line.label.find_first_not_of(' ') == std::string::npos) {
					out += std::format("      {}\n", line.value);
					continue;
				}
				out += std::format("  {}: {}\n", line.label, line.value);
			}
		}
		return out;
	}
}
