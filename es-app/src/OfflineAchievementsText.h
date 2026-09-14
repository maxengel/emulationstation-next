#pragma once
#ifndef ES_APP_OFFLINE_ACHIEVEMENTS_TEXT_H
#define ES_APP_OFFLINE_ACHIEVEMENTS_TEXT_H

// What the offline RetroAchievements proxy answers, read as text (fork
// #180, D-RA-009). With OFFLINE ACHIEVEMENTS on and the device offline, the
// achievements pages read the proxy's on-device cache through the same
// dorequest.php actions RetroArch uses, and this is the pure reading of
// those answers -- the shapes captured from a guest's proxy on 2026-09-14:
//
//   patch            {"Success":true,"PatchData":{"ID","Title","ImageIcon",
//                    "Achievements":[{"ID","Title","Description","Points",
//                    "BadgeName","Flags","BadgeURL","BadgeLockedURL",...}]}}
//                    -- a game the scan cached, keyed by game id;
//   achievementsets  {"Success":true,"GameId","Title","ImageIconUrl",
//                    "Sets":[{"Type":"core","Achievements":[same]}]}
//                    -- a game started once through RetroArch, keyed by hash;
//   unlocks          {"Success":true,"UserUnlocks":[100359]} -- the cached
//                    unlocks merged with the awards still queued; an unknown
//                    game answers 200 with an empty list, so this never
//                    decides whether a game is cached;
//   a miss           503 {"Success":false,"Error":"no cached response"}.
//
// Nothing here reads a file, asks SystemConf, opens a socket or measures a
// font, so es-unit-tests checks it (es-app/tests/unit). OfflineAchievements
// does the asking; RetroAchievements turns the model into the page's.
//
// Translation stays outside on purpose: no _() here.

#include <ctime>
#include <string>
#include <vector>

namespace OfflineAchievementsText
{
	// The proxy's address on the device: RetroArch is pointed at it per
	// launch by setsettings.sh, and the interface asks the same.
	const char* const ProxyBase = "http://127.0.0.1:8080";

	// A dorequest.php URL on the proxy for a query string ("r=patch&g=1").
	std::string requestUrl(const std::string& query);

	// A badge image on the proxy, for an achievement whose body carried no
	// URL: /Badge/<name>.png, or the _lock variant.
	std::string badgeUrl(const std::string& badgeName, bool unlocked);

	struct Achievement
	{
		int id = 0;
		std::string title;
		std::string description;
		int points = 0;
		std::string badgeName;
		std::string badgeUrl;        // as the proxy rewrote it; empty when the body had none
		std::string badgeLockedUrl;
	};

	struct Game
	{
		bool ok = false;             // a body of the right shape with a positive game id
		int id = 0;
		std::string title;
		std::string imageUrl;        // the game's icon, as the proxy rewrote it
		std::vector<Achievement> achievements;
	};

	// The patch action's body. Keeps the core achievements (Flags 3) with a
	// positive id, in the order the body gives them, which is the server's
	// display order. ok is false for anything else -- a miss, junk, a body
	// without PatchData.
	Game parsePatch(const std::string& body);

	// The achievementsets action's body: the core set (Type "core"), or the
	// first set when none says so. Same filter as parsePatch.
	Game parseAchievementSets(const std::string& body);

	// The ids in UserUnlocks. Empty for a body of any other shape; the
	// caller decides what an empty list means from the game's body.
	std::vector<int> parseUnlocks(const std::string& body);

	// The Error of a body that says Success false, else empty. The proxy
	// answers a miss with 503 and this body; HttpReq hands the body back as
	// the error message, and this is how the interface tells "not cached"
	// from "the proxy is not answering".
	std::string parseError(const std::string& body);
	bool isNotCached(const std::string& body);

	// "<achievementId> <epoch>" per line from raofflineproxy-ctl pending-ids:
	// the casual awards still waiting for a connection, and when each was
	// earned. Lines of any other shape are dropped.
	struct PendingAward
	{
		int id = 0;
		time_t when = 0;
	};
	std::vector<PendingAward> parsePendingIds(const std::string& text);

	// The proxy's own online_state.json ({"online": false}): true when the
	// text is one, with online set; false for anything else, and the caller
	// treats a state it cannot read as unknown, never as offline.
	bool parseOnlineState(const std::string& text, bool& online);

	// The client's cached_game_ids.txt, one id per line: the games the proxy
	// holds achievement data for. A line that is not a whole number is not a
	// game.
	std::vector<int> parseReadyIds(const std::string& text);

	// "score=<n> softcore=<n>" from raofflineproxy-ctl account: the account's
	// points as RetroAchievements last told the proxy. ok is false for a line
	// of any other shape -- a number that was not printed is never shown.
	struct AccountTotals
	{
		bool ok = false;
		int score = 0;
		int softcore = 0;
	};
	AccountTotals parseAccountTotals(const std::string& text);
}

#endif // ES_APP_OFFLINE_ACHIEVEMENTS_TEXT_H
