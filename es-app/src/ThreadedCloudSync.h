#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>
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

	// A game launch during a sync EmulationStation started on its own -- the
	// startup sync, the after-a-game backup -- cancels the sync in whatever
	// phase it is in and waits for it to be gone before the launch goes
	// ahead (#101, maintainer's decision, 2026-09-09). It used to cancel
	// only while the sync was still waiting for the network (fork #94); a
	// transfer under way was refused, and with rclone's own timeouts as the
	// only bound on a link that had dropped, that refusal could stand for
	// many minutes (#103). A sync the player asked for keeps the refusal:
	// Origin::Manual, and Origin::None -- the wizard's first backup and the
	// folder tidy -- since they pressed it and can wait for it or stop it
	// themselves.
	//
	// True means the launch may proceed: the sync was one of ours and has
	// ended. False means it may not: nothing running, the player's own
	// sync, or one that had not ended within the budget (two seconds, with
	// SIGKILL to the group at one and a half). The caller refuses on false
	// exactly as it always did.
	//
	// Works on the protocol run() gives every command: it runs under setsid,
	// so its pid is its process group, and its first line is ">>> pid N".
	// The group is sent SIGTERM, so the shell, the scripts and their rclone
	// go together; the scripts' trap exits CloudExit::Stopped, the card says
	// SKIPPED - A GAME WAS STARTED and the stamp records the same, as the
	// network-wait cancel always did.
	static bool cancelForLaunch();

	// The outcome vocabulary's why for an exit code the scripts did not
	// explain with a ">>> why" line (D-UI-028): rclone 3/4 YOUR CLOUD FOLDER
	// WASN'T FOUND, 5 YOUR CLOUD STOPPED ANSWERING, 7/8 YOUR CLOUD REFUSED
	// THE TRANSFER, 130 IT WAS STOPPED, else SOMETHING WENT WRONG. The token
	// is the same table as one word for the stamps; whyForToken reads it
	// back. Shared with the transfer page and the rows under the toggles so
	// every surface says the same thing about the same code.
	static std::string whyForCode(int rc);
	static std::string tokenForCode(int rc);
	static std::string whyForToken(const std::string& token);

private:
	void run();
	// The stamp's fields: "<epoch> <rc> <token>[ <why>]" -- the token is one
	// word for the outcome (completed, gaps, no-network, lock-held,
	// cancelled, stopped, folder-missing, cloud-stopped, cloud-refused,
	// unknown), the why the scripts' own sentence when they printed one
	// (">>> why ..."), for the row to show instead of the token's phrase.
	static void recordOutcome(Origin origin, int rc, const std::string& token, const std::string& why);

	ThreadedCloudSync(Window* window, const std::string& command,
	                  const std::string& title, const std::string& running,
	                  Origin origin);
	~ThreadedCloudSync();

	std::string					mCommand;
	std::string					mTitle;
	std::string					mRunning;
	Origin						mOrigin;

	// Set and read across the worker and the main thread; see
	// cancelForLaunch. mWaitingForNetwork is what the card reads to say
	// WAITING FOR THE NETWORK...; it no longer gates the cancel.
	std::atomic<pid_t>			mPid{0};
	std::atomic<bool>			mWaitingForNetwork{false};
	std::atomic<bool>			mCancelled{false};

	// Read by run() alone, on the worker thread. mWhy is the last ">>> why
	// <sentence>" the scripts printed, upper-cased, for the outcome line;
	// mMoved records that rclone's byte totals ever left "0 B", which is
	// what decides the in-place clause; mTiers is every ">>> tier
	// <label>|<rc>" a composed command echoed after each of its parts, so a
	// run where one part finished and another did not is COMPLETED WITH
	// GAPS rather than the last part's code (D-UI-028).
	std::string					mWhy;
	bool						mMoved{false};
	std::vector<std::pair<std::string, int>> mTiers;
	// Whether SYNC SAVES WHEN EXITING A GAME is on, read in the constructor
	// on the interface thread: the recovery clause for a cancelled sync
	// says the saves go when the game exits only if something will send
	// them then.
	bool						mGameExitSync{false};

	Window*						mWindow;
	AsyncNotificationComponent* mWndNotification;

	std::thread*				mHandle;
	static ThreadedCloudSync*	mInstance;
	// Holds mInstance steady while cancelForLaunch dereferences it: run()
	// clears the pointer from the worker thread, and deletes the object
	// after the card's linger, so a caller that took the pointer under this
	// lock has an object that outlives the call.
	static std::mutex			sInstanceLock;
};
