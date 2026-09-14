#pragma once
#ifndef ES_APP_OFFLINE_ACHIEVEMENTS_H
#define ES_APP_OFFLINE_ACHIEVEMENTS_H

// The offline RetroAchievements proxy's answers, as the interface asks them
// (fork #173, D-RA-004; fork #179, D-RA-010): whether casual awards earned
// without a connection are still waiting, whether a batch of them has just
// gone, how the last scan of the console's games went and how many games
// are cached. All of it comes from raofflineproxy-ctl, the toggle's
// backend, and the two files it and the proxy's client leave; nothing here
// opens the proxy's store. The pure reading of what the ctl prints and
// stamps is CloudText's (parsePendingCount, parseFlushStamp,
// parseScanStamp), so it has a test.

#include "CloudText.h"
#include "OfflineAchievementsText.h"
#include <string>
#include <vector>

class Window;

namespace OfflineAchievements
{
	// Whether the image carries the backend at all.
	bool available();

	// How many casual awards the proxy is holding for the next connection:
	// raofflineproxy-ctl pending. -1 when it could not be told -- no
	// backend, or the store unreadable -- which every caller reads as "say
	// nothing about achievements". With the toggle off the ctl answers 0.
	// Runs a process; call it off the interface thread.
	int pendingAwards();

	// Whether the proxy has sent awards since anyone last asked:
	// raofflineproxy-ctl flushed prints the stamp the proxy leaves after a
	// flush that sent something and removes it, so this is true once per
	// such flush. Runs a process; call it off the interface thread.
	bool takeFlushed();

	// After a game with no sync card to ride -- SYNC SAVES WHEN EXITING A
	// GAME off, or no cloud scripts on the image -- the same two sentences
	// the card would carry, as a toast, from a thread of their own.
	void sayAfterGame(Window* window);

	// How the last scan of the console's games went, or the last automatic
	// top-up: the stamp raofflineproxy-ctl scan and topup write, read
	// uncached because another process writes it while this one runs. ran
	// is false when there has been none.
	CloudText::ScanStamp lastScan();

	// How many games the proxy holds achievement data for -- the ones that
	// earn offline -- from the client's own export of cached game ids, one
	// per line, rewritten on every change to the cache by the service and
	// the client alike. 0 when there is none yet. A file read, no process.
	int readyCount();

	// A scan's why token, said in the player's language. Unknown tokens
	// read as SOMETHING WENT WRONG rather than as the token.
	std::string scanWhy(const std::string& token);

	// The device has come online: raofflineproxy-ctl topup -- the client's
	// recently-played pass -- from a thread of its own, with nothing on
	// screen. Returns at once. The ctl refuses when the toggle is off, no
	// account is signed in or RetroAchievements does not answer, and
	// bounds how often it runs, so this is safe to call on every link.
	void topUpWhenOnline();

	// The achievements pages read the proxy's cache when the device is
	// offline (fork #180, D-RA-009). The questions below are theirs.

	// Whether the toggle is on: the one setting both the pages and the ctl
	// read, so the interface never starts a process for a feature that is
	// off.
	bool toggleOn();

	// Whether the pages should read from the device: the toggle on, the
	// backend present, and the proxy's own online_state.json -- its
	// reachability probe of RetroAchievements, refreshed every fifteen
	// seconds while the service runs -- saying it is not reachable. A file
	// that is missing or unreadable is unknown, and unknown is never read
	// as offline: the web is asked then, as it always was. A file read, no
	// process.
	bool proxyOffline();

	// The RetroAchievements account the proxy cached under: the username
	// EmulationStation signed in with. The proxy keys its cache by it and
	// answers nothing for another.
	std::string username();

	// One dorequest.php question to the proxy (a GET, the query as RetroArch
	// would send it). True with the body on a 200; false with what HttpReq
	// said in error -- the proxy's own body for a miss
	// ({"Success":false,"Error":"no cached response"}), curl's words when
	// nothing listens. Bounded: a proxy that hangs is given seconds, not a
	// spinner that never ends. Blocks; call it off the interface thread.
	bool askProxy(const std::string& query, std::string& body, std::string& error);

	// The casual awards still waiting for a connection, by achievement id,
	// with when each was earned: raofflineproxy-ctl pending-ids. Empty when
	// there are none, or it could not be told. Runs a process; call it off
	// the interface thread.
	std::vector<OfflineAchievementsText::PendingAward> pendingAwardIds();

	// The game ids the proxy holds achievement data for, from the client's
	// export (the same file readyCount counts). A file read, no process.
	std::vector<int> readyIds();

	// The account's points as RetroAchievements last told the proxy:
	// raofflineproxy-ctl account. ok is false when the proxy has no cached
	// sign-in to read them from. Runs a process; off the interface thread.
	OfflineAchievementsText::AccountTotals accountTotals();
}

#endif // ES_APP_OFFLINE_ACHIEVEMENTS_H
