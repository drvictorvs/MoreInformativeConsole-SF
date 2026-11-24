#include "AddressCheck.h"
#include "ConsolePanel.h"
#include "Settings.h"

namespace
{
	void OnMessage(SFSE::MessagingInterface::Message* a_msg)
	{
		if (!a_msg) {
			return;
		}
		if (a_msg->type == SFSE::MessagingInterface::kPostLoad) {
			MIC::OSF::Init();  // optional: connects to OSF Settings' menu if installed
			return;
		}
		if (a_msg->type != SFSE::MessagingInterface::kPostDataLoad) {
			return;
		}

		if (!MIC::Address::Get().core) {
			return;  // already logged; leave the game untouched
		}
		const auto* tasks = SFSE::GetTaskInterface();
		if (!tasks) {
			REX::ERROR("SFSE task interface unavailable; the console panel is disabled");
			return;
		}
		// Runs once per frame on the game thread (SFSE's command-processing hook).
		tasks->AddPermanentTask([]() { MIC::ConsolePanel::OnFrame(); });
		REX::INFO("Console watcher registered");
	}
}

SFSE_PLUGIN_LOAD(const SFSE::LoadInterface* a_sfse)
{
	SFSE::Init(a_sfse);

	REX::INFO("{} {} loading", SFSE::GetPluginName(), SFSE::GetPluginVersion().string());
	MIC::Settings::Get().Load();

	// Check every Address Library ID up front, so a missing one disables a feature instead of
	// ending the game. If the panel's own IDs are missing, the plugin loads but does nothing.
	if (!MIC::Address::Get().core) {
		return true;
	}

	const auto* messaging = SFSE::GetMessagingInterface();
	if (!messaging || !messaging->RegisterListener(OnMessage)) {
		REX::ERROR("Could not register for SFSE messages");
		return false;
	}
	return true;
}
