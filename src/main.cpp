#include "ConsolePanel.h"
#include "Settings.h"

namespace
{
	void OnMessage(SFSE::MessagingInterface::Message* a_msg)
	{
		if (!a_msg || a_msg->type != SFSE::MessagingInterface::kPostDataLoad) {
			return;
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

	const auto* messaging = SFSE::GetMessagingInterface();
	if (!messaging || !messaging->RegisterListener(OnMessage)) {
		REX::ERROR("Could not register for SFSE messages");
		return false;
	}
	return true;
}
