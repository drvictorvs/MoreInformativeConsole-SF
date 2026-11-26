#include "Highlight.h"

#include "AddressCheck.h"
#include "GameIDs.h"
#include "SafeMemory.h"
#include "Settings.h"

#include <mutex>

namespace MIC::Highlight
{
	namespace
	{
		using Ctor_t = void* (*)(void* a_storage, const GameID::HighlightStyle* a_style);
		using Clear_t = void (*)(void* a_manager);
		using SetStyle_t = void (*)(const std::uint32_t* a_styleHandle, const GameID::HighlightStyle* a_style);
		using VisitLeaves_t = bool (*)(void* a_visitor, void* a_root);
		using Get3D_t = void** (*)(RE::TESObjectREFR* a_ref, void** a_out);  // returns NiPointer<NiAVObject> through a_out
		using DeleteThis_t = void (*)(void* a_object);

		constexpr std::size_t kGet3DSlot = 0xAC;  // TESObjectREFR vtable; confirmed in game by DropOverhaul-SF

		// What the game's visitor (GameID::HighlightAddVisitor_VTable) expects.
		struct Context
		{
			void*         manager;
			std::uint32_t styleHandle;
		};
		struct Visitor
		{
			const void* vtable;
			Context*    context;
		};

		std::mutex            g_lock;  // the console task can run on different threads
		alignas(16) std::byte g_storage[GameID::kHighlightManagerSize]{};  // never destroyed
		void*                 g_manager = nullptr;
		bool                  g_unavailable = false;
		std::uint32_t         g_styleVersion = 0;
		std::atomic<bool>     g_active{ false };  // something is highlighted
		RE::TESFormID         g_shownID = 0;
		const void*           g_shownRoot = nullptr;  // compared only, never dereferenced
		std::chrono::steady_clock::time_point g_lastWalk{};
		// Each walk records a change for every geometry in the renderer's tables, so the same 3D is walked
		// again (for geometry it gained, e.g. equipment) at most this often.
		constexpr auto kRewalkInterval = std::chrono::milliseconds(1000);

		template <class T>
		T GameFunction(REL::ID a_id)
		{
			return reinterpret_cast<T>(REL::Relocation<std::uintptr_t>{ a_id }.address());
		}

		GameID::HighlightStyle MakeStyle()
		{
			const auto& settings = Settings::Get();
			const auto  rgb = settings.highlightColor & 0xFFFFFF;
			const auto  fillAlpha = static_cast<std::uint32_t>(std::lround(std::clamp(settings.highlightAlpha, 0.0, 1.0) * 255.0));

			// The outline's edge hardness follows the game's own [Highlight] setting.
			float hardness = 0.1f;
			if (Address::IsPresent(GameID::fDefaultHighlightOutlineHardness.id())) {
				float      game = 0.0f;
				const auto address = REL::Relocation<std::uintptr_t>{ GameID::fDefaultHighlightOutlineHardness }.address();
				if (Safe::Read(reinterpret_cast<const void*>(address), game) && std::isfinite(game) && game > 0.0f && game <= 10.0f) {
					hardness = game;
				}
			}

			GameID::HighlightStyle style{};
			style.fill = (fillAlpha << 24) | rgb;
			style.outline = 0xFF000000u | rgb;
			style.thickness = static_cast<float>(std::clamp(settings.highlightOutline, 0.0, 20.0));
			style.hardness = hardness;
			return style;
		}

		std::uint32_t StyleHandle()
		{
			std::uint32_t handle = 0;
			std::memcpy(&handle, g_storage + GameID::kHighlightManager_Style, sizeof(handle));
			return handle;
		}

		void* Manager()
		{
			if (g_manager || g_unavailable) {
				return g_manager;
			}
			if (!Address::Get().selectionHighlight) {
				g_unavailable = true;
				return nullptr;
			}
			const auto style = MakeStyle();
			GameFunction<Ctor_t>(GameID::HighlightManager_Ctor)(g_storage, &style);
			g_manager = g_storage;
			g_styleVersion = Settings::Get().version;
			REX::INFO("Selection highlight: created a highlight manager (style handle {:06X}, fill {:08X}, outline {:08X}, thickness {})",
				StyleHandle() & 0xFFFFFF, style.fill, style.outline, style.thickness);
			return g_manager;
		}

		void ClearLocked()
		{
			if (g_manager && g_active.load()) {
				GameFunction<Clear_t>(GameID::HighlightManager_Clear)(g_manager);
			}
			g_active.store(false);
			g_shownID = 0;
			g_shownRoot = nullptr;
		}

		// The reference's 3D with a reference held, or nullptr when it isn't loaded.
		void* Get3D(RE::TESObjectREFR* a_ref)
		{
			auto** vtable = *reinterpret_cast<void***>(a_ref);
			void*  root = nullptr;
			reinterpret_cast<Get3D_t>(vtable[kGet3DSlot])(a_ref, &root);
			return root;
		}

		// Drops the reference Get3D took, as NiPointer would (NiRefObject: refcount +8, DeleteThis slot 1).
		void Release(void* a_object)
		{
			auto& count = *reinterpret_cast<std::uint32_t*>(static_cast<std::byte*>(a_object) + 8);
			if (std::atomic_ref<std::uint32_t>(count).fetch_sub(1) == 1) {
				reinterpret_cast<DeleteThis_t>((*reinterpret_cast<void***>(a_object))[1])(a_object);
			}
		}
	}

	void Show(RE::TESObjectREFR* a_ref)
	{
		std::lock_guard guard(g_lock);
		const auto&     settings = Settings::Get();
		if (!settings.highlightSelection || !a_ref) {
			ClearLocked();
			return;
		}
		auto* manager = Manager();
		if (!manager) {
			return;
		}
		if (g_styleVersion != settings.version) {
			const auto style = MakeStyle();
			const auto handle = StyleHandle();
			GameFunction<SetStyle_t>(GameID::HighlightStyle_Set)(&handle, &style);
			g_styleVersion = settings.version;
		}

		void* root = Get3D(a_ref);
		if (!root) {
			ClearLocked();
			return;
		}
		// Another reference, or new 3D for the same one: start over. Otherwise, now and then, pick up
		// geometry the reference gained; walking the rest again is harmless.
		const auto now = std::chrono::steady_clock::now();
		if (a_ref->GetFormID() != g_shownID || root != g_shownRoot) {
			ClearLocked();
		} else if (g_active.load() && now - g_lastWalk < kRewalkInterval) {
			Release(root);
			return;
		}
		g_lastWalk = now;
		Context context{ manager, StyleHandle() };
		Visitor visitor{ reinterpret_cast<const void*>(REL::Relocation<std::uintptr_t>{ GameID::HighlightAddVisitor_VTable }.address()), &context };
		GameFunction<VisitLeaves_t>(GameID::NiAVObject_VisitLeaves)(&visitor, root);
		g_active.store(true);
		g_shownID = a_ref->GetFormID();
		g_shownRoot = root;
		Release(root);
	}

	void Hide()
	{
		if (!g_active.load()) {
			return;
		}
		std::lock_guard guard(g_lock);
		ClearLocked();
	}
}
