#include "OfflineAchievements.h"
#include "ApiSystem.h"
#include "CloudText.h"
#include "HttpReq.h"
#include "LocaleES.h"
#include "Log.h"
#include "SystemConf.h"
#include "Window.h"
#include "utils/FileSystemUtil.h"
#include "utils/Platform.h"
#include "utils/StringUtil.h"
#include <ctime>
#include <string>
#include <thread>

namespace
{
	const char* CTL = "/usr/bin/raofflineproxy-ctl";
	// Where the proxy keeps everything it writes (the unit's
	// RAOFFLINEPROXY_CONFIG_DIR), and the two files the scan reads back.
	const char* SCAN_STAMP = "/storage/.config/raofflineproxy/last-scan";
	// The one line the ctl keeps while a scan or top-up runs its jobs,
	// rewritten as each game finishes and removed when the run ends (fork
	// #189); CloudText::parseRunningProgress reads it.
	const char* RUNNING_FILE = "/storage/.config/raofflineproxy/running";
	const char* READY_FILE = "/storage/.config/raofflineproxy/cached_game_ids.txt";
	// The proxy's own answer to "can RetroAchievements be reached", written
	// by its connectivity monitor (state.py save_online_state).
	const char* ONLINE_STATE = "/storage/.config/raofflineproxy/online_state.json";

	// How long one question to the proxy may take, in all. It answers from
	// its store on the same device in milliseconds; anything longer is a
	// proxy that is not answering, and the page must not wait on it.
	const long PROXY_CONNECT_MS = 2000L;
	const long PROXY_TOTAL_MS = 15000L;

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
	// The two sentences are the sync card's, verbatim (D-RA-017).
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

CloudText::RunningProgress OfflineAchievements::runningProgress()
{
	// Uncached, as the stamp is. The ctl writes the file whole (a temporary
	// name, then a rename) and removes it as the run ends, so a read finds
	// either a whole line or nothing -- and nothing, should the file go
	// between the two calls here, parses as no run.
	if (!Utils::FileSystem::exists(RUNNING_FILE, false))
		return CloudText::RunningProgress();
	return CloudText::parseRunningProgress(Utils::FileSystem::readAllText(RUNNING_FILE), (long long) time(nullptr));
}

std::vector<int> OfflineAchievements::readyIds()
{
	// Uncached, like the scan stamp: the client rewrites this file while
	// EmulationStation runs.
	if (!Utils::FileSystem::exists(READY_FILE, false))
		return std::vector<int>();
	// One game id per line (es_export.py). Lines that are not a number are
	// not games: a count is never made of what was not read as one.
	return OfflineAchievementsText::parseReadyIds(Utils::FileSystem::readAllText(READY_FILE));
}

int OfflineAchievements::readyCount()
{
	return (int)readyIds().size();
}

bool OfflineAchievements::toggleOn()
{
	return SystemConf::getInstance()->getBool("global.retroachievements.offlineproxy");
}

bool OfflineAchievements::proxyOffline()
{
	if (!available() || !toggleOn())
		return false;
	// The device's own link first (fork #190). The proxy's monitor rewrites
	// its state only when its next probe fails, some time after the link
	// drops, and a player who has just switched Wi-Fi off opens a page
	// before that; asking the web then is a PLEASE WAIT with no route
	// under it. No address on any wired or wireless interface (the same
	// question the network watcher and the scan row ask; a VPN's tunnel
	// does not count) is offline, whatever the file says.
	if (Utils::Platform::queryIPAddress().empty())
		return true;
	if (!Utils::FileSystem::exists(ONLINE_STATE, false))
		return false;
	bool online = true;
	if (!OfflineAchievementsText::parseOnlineState(Utils::FileSystem::readAllText(ONLINE_STATE), online))
		return false;
	return !online;
}

std::string OfflineAchievements::username()
{
	return SystemConf::getInstance()->get("global.retroachievements.username");
}

bool OfflineAchievements::askProxy(const std::string& query, std::string& body, std::string& error)
{
	body.clear();
	error.clear();

	HttpReqOptions options;
	options.connectTimeout = PROXY_CONNECT_MS;
	options.timeout = PROXY_TOTAL_MS;
	// No cookie jar for a local process, and the interface's own name: the
	// proxy remembers the last client's User-Agent but never uses it for
	// its own requests (utils.py self_user_agent), so this changes nothing
	// about how it speaks to RetroAchievements.
	options.useCookieManager = false;

	HttpReq req(OfflineAchievementsText::requestUrl(query), &options);
	if (req.wait())
	{
		body = req.getContent();
		return true;
	}
	// For a 4xx or 503 HttpReq keeps the proxy's body as the message, so a
	// miss reads as the proxy's own words and not as a status code.
	error = req.getErrorMsg();
	return false;
}

std::vector<OfflineAchievementsText::PendingAward> OfflineAchievements::pendingAwardIds()
{
	if (!available() || !toggleOn())
		return std::vector<OfflineAchievementsText::PendingAward>();

	// Every line, not the last: one award per line. Exit 0 with rows, 1 with
	// none; anything else is the ctl saying it could not tell, and the page
	// then marks nothing as waiting rather than guessing.
	std::string all;
	auto result = ApiSystem::executeScriptLegacy(std::string(CTL) + " pending-ids 2>/dev/null",
		[&all](const std::string line) { all += line + "\n"; });
	if (result.second != 0 && result.second != 1)
		return std::vector<OfflineAchievementsText::PendingAward>();
	return OfflineAchievementsText::parsePendingIds(all);
}

OfflineAchievementsText::AccountTotals OfflineAchievements::accountTotals()
{
	if (!available() || !toggleOn())
		return OfflineAchievementsText::AccountTotals();
	const auto answer = ask("account");
	if (answer.second != 0)
		return OfflineAchievementsText::AccountTotals();
	return OfflineAchievementsText::parseAccountTotals(answer.first);
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
	// A fetch failed for some game, even among many that went through
	// (audit #186 PL-24): the run could not finish, and the next scan tries
	// those games again, since nothing marks them cached.
	if (token == "SOME_GAMES_NOT_SAVED")
		return _("SOME GAMES COULDN'T BE SAVED. TRY THE SCAN AGAIN.");
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

void OfflineAchievements::topUpAfterIndex()
{
	if (!available() || !toggleOn())
		return;

	// The index has just grown, so this run is not held to the half hour
	// since the last attempt; the ctl still bounds it (its lock, its
	// timeout) and stamps its outcome for the OFFLINE ACHIEVEMENTS page.
	std::thread([]
	{
		const auto answer = ask("topup --after-index");
		LOG(LogInfo) << "OfflineAchievements: topup --after-index exited " << answer.second;
	}).detach();
}
