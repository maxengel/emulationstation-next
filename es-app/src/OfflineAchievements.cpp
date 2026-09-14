#include "OfflineAchievements.h"
#include "ApiSystem.h"
#include "CloudText.h"
#include "LocaleES.h"
#include "Log.h"
#include "SystemConf.h"
#include "Window.h"
#include "utils/FileSystemUtil.h"
#include "utils/StringUtil.h"
#include <string>
#include <thread>

namespace
{
	const char* CTL = "/usr/bin/raofflineproxy-ctl";
	// Where the proxy keeps everything it writes (the unit's
	// RAOFFLINEPROXY_CONFIG_DIR), and the two files the scan reads back.
	const char* SCAN_STAMP = "/storage/.config/raofflineproxy/last-scan";
	const char* READY_FILE = "/storage/.config/raofflineproxy/cached_game_ids.txt";

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

CloudText::ScanStamp OfflineAchievements::lastScan()
{
	// Uncached: the ctl writes this while EmulationStation runs, and
	// Utils::FileSystem::exists remembers a miss for the session otherwise
	// (es-native-ui.md; the cloud rows read NOT DONE ON THIS DEVICE YET
	// after a run had stamped, 2026-09-10).
	if (!Utils::FileSystem::exists(SCAN_STAMP, false))
		return CloudText::ScanStamp();
	return CloudText::parseScanStamp(Utils::FileSystem::readAllText(SCAN_STAMP));
}

int OfflineAchievements::readyCount()
{
	if (!Utils::FileSystem::exists(READY_FILE, false))
		return 0;
	// One game id per line (es_export.py). Lines that are not a number are
	// not games: a count is never made of what was not read as one.
	int count = 0;
	for (const std::string& raw : Utils::String::split(Utils::FileSystem::readAllText(READY_FILE), '\n', true))
	{
		const std::string line = Utils::String::trim(raw);
		if (line.empty())
			continue;
		bool digits = true;
		for (char c : line)
			if (c < '0' || c > '9')
				digits = false;
		if (digits)
			count++;
	}
	return count;
}

std::string OfflineAchievements::scanWhy(const std::string& token)
{
	// The ctl's tokens (raofflineproxy-ctl, the scan's header), in the
	// player's words (es-player-text.md: everyday, not formal).
	if (token == "TOGGLE_OFF")
		return _("TURN ON OFFLINE ACHIEVEMENTS FIRST.");
	if (token == "NO_ACCOUNT")
		return _("SIGN IN TO RETROACHIEVEMENTS FIRST.");
	if (token == "SIGN_IN_REFUSED")
		return _("RETROACHIEVEMENTS DIDN'T ACCEPT YOUR SIGN-IN");
	if (token == "RETROACHIEVEMENTS_STOPPED_ANSWERING")
		return _("RETROACHIEVEMENTS STOPPED ANSWERING");
	if (token == "NO_GAMES_FOUND")
		return _("NO GAMES WERE FOUND ON THIS CONSOLE");
	if (token == "LIBRARY_UNREADABLE")
		return _("YOUR GAMES COULDN'T BE READ");
	if (token == "TOOK_TOO_LONG")
		return _("IT TOOK TOO LONG");
	return _("SOMETHING WENT WRONG");
}

void OfflineAchievements::topUpWhenOnline()
{
	if (!available())
		return;
	// The toggle is read here as well as in the ctl so a device with the
	// feature off never starts a process for it on every link.
	if (!SystemConf::getInstance()->getBool("global.retroachievements.offlineproxy"))
		return;

	// Never on the interface thread, and never waited for: the ctl probes
	// RetroAchievements, then runs the client's recently-played pass, and
	// either can take minutes. Its outcome lands in the stamp the OFFLINE
	// ACHIEVEMENTS page reads, and nowhere on screen (D-RA-010: silent).
	std::thread([]
	{
		const auto answer = ask("topup");
		LOG(LogInfo) << "OfflineAchievements: topup exited " << answer.second;
	}).detach();
}
