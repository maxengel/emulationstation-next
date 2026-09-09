#pragma once

#include <string>
#include <thread>
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
	// and skip the runs that did nothing (exit 3, 4); this one is per cause
	// and records the whole run, skips included, because the question under
	// the toggle is "what happened at startup", and "nothing, no network" is
	// an answer to it.
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

	Window*						mWindow;
	AsyncNotificationComponent* mWndNotification;

	std::thread*				mHandle;
	static ThreadedCloudSync*	mInstance;
};
