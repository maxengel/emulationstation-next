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
#include <string>

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
}

#endif // ES_APP_OFFLINE_ACHIEVEMENTS_H
