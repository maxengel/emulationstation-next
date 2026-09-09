#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <sys/types.h>
#include "components/AsyncNotificationComponent.h"

// Runs a headless cloud sync command in the background with a native
// progress card, instead of taking over the screen with a console.
class ThreadedCloudSync
{
public:
	// Who asked for the saves to move. A run that has one leaves a stamp --
	// /storage/.cache/cloud_sync/last-sync-<origin>, "<epoch> <rc>" -- that
	// the cloud settings page reads back as the line under the toggle that
	// caused it (fork #94). The card is gone seconds after the work ends,
	// and somebody who set SYNC SAVES DURING STARTUP needs an answer to "did
	// it run this morning?" from a screen that still exists.
	//
	// The scripts' own stamps (last-backup, last-restore) are per direction
	// and skip the runs that did nothing (CloudExit::LockHeld, NoNetwork);
	// this one is per cause and records the whole run, skips included,
	// because the question under the toggle is "what happened at startup",
	// and "nothing, no network" is an answer to it.
	//
	// None is for jobs that are not a saves sync -- tidying folders, the
	// wizard's first backup -- which have no toggle to report to.
	enum class Origin { None, Startup, Exit, Manual };

	// `running` is what the card says while it works -- "BACKING UP..." --
	// where `title` names the operation for the line it prints when it is
	// done. One string for both read as "Back up all system data syncing
	// with the cloud", which says neither what is happening nor that it is.
	static void start(Window* window, const std::string& command,
	                  const std::string& title, const std::string& running = "",
	                  Origin origin = Origin::None);
	static bool isRunning() { return mInstance != nullptr; }

	// A game launch while the sync is still waiting for the network cancels
	// the sync rather than being refused (fork #94). The startup sync can
	// spend up to a minute waiting after boot, and for that minute nothing
	// has been read or written, so there is nothing for the launch gate to
	// protect -- while a device booted offline that will not start a game
	// for a minute is a regression on the headless run it replaced (#84
	// turned down a 15 s boot cost). True means the sync was waiting and is
	// now being stopped: go ahead and launch. False means it was not
	// waiting -- nothing running, or a transfer under way -- and the caller
	// decides as before.
	//
	// Works on the protocol the command speaks: ">>> pid N" on its first
	// line (the command runs under setsid, so N is also its process group),
	// ">>> doing network" while it probes, and any later line -- a script's
	// first words, another ">>> unit" -- to say the wait is over. The
	// process group is sent SIGTERM, so the shell, its ping and its sleep go
	// together.
	static bool cancelIfWaitingForNetwork();

private:
	void run();
	static void recordOutcome(Origin origin, int rc);

	ThreadedCloudSync(Window* window, const std::string& command,
	                  const std::string& title, const std::string& running,
	                  Origin origin);
	~ThreadedCloudSync();

	std::string					mCommand;
	std::string					mTitle;
	std::string					mRunning;
	Origin						mOrigin;

	// Set and read across the worker and the main thread; see
	// cancelIfWaitingForNetwork.
	std::atomic<pid_t>			mPid{0};
	std::atomic<bool>			mWaitingForNetwork{false};
	std::atomic<bool>			mCancelled{false};

	Window*						mWindow;
	AsyncNotificationComponent* mWndNotification;

	std::thread*				mHandle;
	static ThreadedCloudSync*	mInstance;
	// Holds mInstance steady while cancelIfWaitingForNetwork dereferences
	// it: run() clears the pointer from the worker thread, and deletes the
	// object after the card's linger, so a caller that took the pointer
	// under this lock has an object that outlives the call.
	static std::mutex			sInstanceLock;
};
