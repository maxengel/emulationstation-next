#include "OfflineAchievements.h"
#include "ApiSystem.h"
#include "CloudText.h"
#include "LocaleES.h"
#include "Window.h"
#include "utils/FileSystemUtil.h"
#include <string>
#include <thread>

namespace
{
	const char* CTL = "/usr/bin/raofflineproxy-ctl";

	// The ctl's last stdout line and its exit status. executeScriptLegacy is
	// the public route that hands back both, as the toggle's page uses it.
	std::pair<std::string, int> ask(const char* verb)
	{
		std::string last;
		auto result = ApiSystem::executeScriptLegacy(std::string(CTL) + " " + verb + " 2>/dev/null",
			[&last](const std::string line) { last = line; });
		return std::make_pair(last, result.second);
	}
}

bool OfflineAchievements::available()
{
	// The image's own file: static for the session, so the cache is right.
	return Utils::FileSystem::exists(CTL);
}

int OfflineAchievements::pendingAwards()
{
	if (!available())
		return -1;

	// 0 with the count, 1 with 0; anything else is the ctl saying it could
	// not tell, and prints no number to be mistaken for one.
	const auto answer = ask("pending");
	if (answer.second != 0 && answer.second != 1)
		return -1;
	return CloudText::parsePendingCount(answer.first);
}

bool OfflineAchievements::takeFlushed()
{
	if (!available())
		return false;

	const auto answer = ask("flushed");
	if (answer.second != 0)
		return false;
	return CloudText::parseFlushStamp(answer.first).ok;
}

void OfflineAchievements::sayAfterGame(Window* window)
{
	if (!available())
		return;

	// Off the interface thread: each answer is a process, and a game has
	// just exited on a handheld. The stamp is consumed first so a flush that
	// already happened is not told after the awards that followed it; the
	// waiting awards are the newer fact, so they win when both are true.
	std::thread([window]
	{
		const bool sent = takeFlushed();
		const int pending = pendingAwards();

		std::string text;
		if (pending > 0)
			text = _("OFFLINE ACHIEVEMENTS WILL BE SENT NEXT TIME YOU'RE CONNECTED.");
		else if (sent)
			text = _("OFFLINE ACHIEVEMENTS HAVE BEEN SENT TO RETROACHIEVEMENTS.");
		if (text.empty())
			return;

		window->postToUiThread([window, text] { window->displayNotificationMessage(text); });
	}).detach();
}
