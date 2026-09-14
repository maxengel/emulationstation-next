#pragma once
#ifndef ES_APP_OFFLINE_ACHIEVEMENTS_H
#define ES_APP_OFFLINE_ACHIEVEMENTS_H

// The offline RetroAchievements proxy's two answers, as the sync cards ask
// them (fork #173, D-RA-004): whether casual awards earned without a
// connection are still waiting, and whether a batch of them has just gone.
// Both come from raofflineproxy-ctl, the toggle's backend, which reads the
// proxy's store and the stamp it leaves; nothing here opens either. The
// pure reading of what the ctl prints is CloudText's (parsePendingCount,
// parseFlushStamp), so it has a test.

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
}

#endif // ES_APP_OFFLINE_ACHIEVEMENTS_H
