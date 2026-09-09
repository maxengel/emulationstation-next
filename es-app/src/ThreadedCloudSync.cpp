#include "ThreadedCloudSync.h"
#include "CloudExit.h"
#include "Window.h"
#include "components/AsyncNotificationComponent.h"
#include "guis/GuiMsgBox.h"
#include "utils/FileSystemUtil.h"
#include "utils/Platform.h"
#include "utils/StringUtil.h"
#include <chrono>
#include <cstdio>
#include <ctime>
#include <mutex>
#include <thread>
#include <signal.h>
#include <sys/wait.h>
#include "LocaleES.h"

#define ICONINDEX _U("\uF0C2 ")

ThreadedCloudSync* ThreadedCloudSync::mInstance = nullptr;
std::mutex ThreadedCloudSync::sInstanceLock;

ThreadedCloudSync::ThreadedCloudSync(Window* window, const std::string& command,
	const std::string& title, const std::string& running, Origin origin)
	: mWindow(window), mCommand(command), mTitle(title), mRunning(running), mOrigin(origin)
{
	mWndNotification = mWindow->createAsyncNotificationComponent();
	mWndNotification->updateTitle(ICONINDEX + (mRunning.empty() ? mTitle : mRunning));
	mWndNotification->updateText(_("Working..."));
	mWndNotification->updatePercent(-1);

	mHandle = new std::thread(&ThreadedCloudSync::run, this);
}

ThreadedCloudSync::~ThreadedCloudSync()
{
	mWndNotification->close();
	mWndNotification = nullptr;

	// Only if it is still us. run() clears this as soon as the work ends so
	// another sync can start during the few seconds the card holds its
	// result -- and if one has, the static belongs to that one now.
	if (ThreadedCloudSync::mInstance == this)
		ThreadedCloudSync::mInstance = nullptr;
}

void ThreadedCloudSync::run()
{
	// Stream the backend's output into the notification card so the user
	// sees live status (rclone --stats-one-line lines, phase banners, ...).
	int ret = -1;
	FILE* pipe = popen((mCommand + " 2>&1").c_str(), "r");
	if (pipe != nullptr)
	{
		char line[512];
		while (fgets(line, sizeof(line), pipe) != nullptr)
		{
			std::string text(line);

			// keep it single-line and printable
			std::string clean;
			for (char c : text)
				if (c >= 32 && c < 127)
					clean += c;

			clean = Utils::String::trim(clean);

			// ">>> " lines are the scripts talking to the UI, not to the
			// player: ">>> unit SAVES||" names a phase for GuiCloudTransfer's
			// per-system fold, ">>> pid N" is the command saying which
			// process group it is (for cancelIfWaitingForNetwork), and
			// ">>> doing network" says it is waiting for something before
			// the transfer can begin. The one this card shows is the wait:
			// the startup sync (fork #94) gives the network up to a minute
			// to come up after boot, and a card reading "Working..." for
			// that minute says nothing about why. Any other keyword, and any
			// other protocol line, is not for this card and never reaches
			// it -- but each one says the wait is over, as does the first
			// word any script prints.
			if (clean.rfind(">>> ", 0) == 0)
			{
				if (clean.rfind(">>> pid ", 0) == 0)
					mPid = atoi(clean.substr(8).c_str());
				else if (clean.rfind(">>> doing ", 0) == 0)
				{
					const std::string what = Utils::String::trim(clean.substr(10));
					mWaitingForNetwork = (what == "network");
					if (what == "network" && mWndNotification != nullptr)
						mWndNotification->updateText(_("WAITING FOR THE NETWORK..."));
				}
				else
					mWaitingForNetwork = false;
				continue;
			}
			mWaitingForNetwork = false;

			// The card shows progress; the title above it already says what
			// is happening. Everything the backends print used to land here,
			// which meant rules of "====================================" for
			// seconds at a time, and -- during a sync, which runs a restore
			// and then a backup -- banners announcing "CLOUD RESTORE UTILITY"
			// and "CLOUD BACKUP UTILITY" underneath a title reading SYNCING
			// SAVES. Those are written for a log read afterwards, not for
			// somebody watching a handheld.
			//
			// So: transfer progress, and anything that went wrong. A line
			// carries progress if it has a percentage or a "x / y" count.
			const bool hasPercent = clean.find('%') != std::string::npos;
			const bool hasCount   = clean.find(" / ") != std::string::npos;
			const std::string upper = Utils::String::toUpper(clean);
			const bool isProblem =
				upper.find("ERROR") != std::string::npos ||
				upper.find("FAILED") != std::string::npos ||
				upper.find("WARN") != std::string::npos;
			const bool informative = hasPercent || hasCount || isProblem;

			// rclone's own line is written for a terminal:
			// "Transferred: 12.345 MiB / 45.678 MiB, 27%, 1.234 MiB/s, ETA 27s".
			// On a handheld card the useful part is the front of it, and the
			// speed and ETA push everything else off the end.
			auto eta = clean.find(", ETA ");
			if (eta != std::string::npos)
				clean = clean.substr(0, eta);
			if (clean.rfind("Transferred:", 0) == 0)
				clean = Utils::String::trim(clean.substr(12));

			// rclone's check counter -- "Checks: 12 / 70, 17%, Listed 313" --
			// is a comparison, not a transfer. Drawn as a bar it reads as
			// seventy uploads, and somebody who has just exited one game asks
			// why every game is being synced. Say what it is, and leave the
			// bar to the transfer line.
			const bool isChecks = clean.rfind("Checks:", 0) == 0;
			if (isChecks)
			{
				auto comma = clean.find(',');
				const std::string count = Utils::String::trim(
					clean.substr(7, comma == std::string::npos ? std::string::npos : comma - 7));
				clean = _("COMPARING SAVE FILES WITH THE CLOUD") + std::string(" ") + count;
			}

			if (informative && !clean.empty() && mWndNotification != nullptr)
			{
				mWndNotification->updateText(clean);

				// rclone's --stats-one-line already carries the percentage --
				// "Transferred: 12.3 MiB / 45.6 MiB, 27%, 1.2 MiB/s, ETA 27s"
				// -- and it was being thrown away: the bar sat at -1, which
				// means indeterminate, for the whole transfer. Take it from
				// the line already passing through rather than asking rclone
				// for it a second way.
				auto pct = clean.find('%');
				if (!isChecks && pct != std::string::npos && pct > 0)
				{
					size_t start = pct;
					while (start > 0 && isdigit((unsigned char)clean[start - 1]))
						start--;
					if (start < pct)
					{
						int value = atoi(clean.substr(start, pct - start).c_str());
						if (value >= 0 && value <= 100)
							mWndNotification->updatePercent(value);
					}
				}
			}
		}

		int status = pclose(pipe);
		if (WIFEXITED(status))
			ret = WEXITSTATUS(status);
	}

	// A cancelled run ends by SIGTERM, which pclose reports as a signal or,
	// when a shell sat between us and the group, as 143. Neither is what
	// happened. CloudExit::Stopped is what the scripts' own trap exits with
	// when somebody stops them, and what cloudLastRunDetail already reads as
	// STOPPED: the same word for the same thing, whoever did the stopping.
	const bool cancelled = mCancelled;
	if (cancelled)
		ret = CloudExit::Stopped;

	// Before the card says anything: the stamp is the answer that outlives
	// the card, so it is written first, and written whether or not there is
	// still a card to say it on.
	recordOutcome(mOrigin, ret);

	// One surface for the whole event.
	//
	// The card used to vanish the instant the work ended, and the outcome
	// arrived as a GuiInfoPopup: a different shape, in a different place,
	// at exactly the moment somebody is looking for the answer. Two things
	// appeared where one thing happened. Say it in the card that has been
	// reporting all along, hold it long enough to read, and let that same
	// card fade.
	//
	// "FINISHED" would say it stopped, not that it worked. Somebody who has
	// just sent their saves somewhere wants to be told it went well.
	if (mWndNotification != nullptr)
	{
		mWndNotification->updateTitle(ICONINDEX + mTitle);
		// LockHeld is the scripts' "another cloud sync holds the lock";
		// NoNetwork is their "no network" (CloudExit.h has the values and
		// why). Neither is a failure: the boot-time sync was already doing
		// this work, or there was nothing to sync to -- and FAILED would
		// send somebody to a log to find out nothing went wrong.
		// A cancel is not a failure either: the player chose a game over a
		// wait, and the saves were never touched.
		mWndNotification->updateText(cancelled
			? _("SKIPPED - A GAME WAS STARTED")
			: ret == 0 ? _("COMPLETED SUCCESSFULLY")
			: ret == CloudExit::LockHeld ? _("SKIPPED - ANOTHER CLOUD SYNC IS RUNNING")
			: ret == CloudExit::NoNetwork ? _("SKIPPED - NO NETWORK CONNECTION")
			: _("FAILED - SEE /var/log/cloud_sync.log"));

		// A full bar on success; on failure the bar goes, because a
		// progress bar left standing under the word FAILED reads as a
		// measure of how much of the failure has completed.
		mWndNotification->updatePercent(ret == 0 ? 100 : -1);

		// Nothing is running any more, so stop claiming otherwise: somebody
		// who wants to start another sync while the card is still up should
		// not be told one is already going. Under the lock, so a
		// cancelIfWaitingForNetwork that has just taken the pointer finishes
		// with it before it goes -- and the delete below is a linger later.
		{
			std::lock_guard<std::mutex> lock(sInstanceLock);
			if (ThreadedCloudSync::mInstance == this)
				ThreadedCloudSync::mInstance = nullptr;
		}

		// Hold the outcome long enough to read, then let the card fade.
		// Success is two words and a full bar, and somebody who just exited
		// a game is standing there watching it, so a second and a half (two
		// lingered -- maintainer, 2026-09-07); a skip or a failure is a
		// sentence to act on, so five. Five for everything dated from when a
		// sync took 18 seconds -- once the exit sync came down to about five,
		// the card spent as long saying it was done as it had spent working.
		std::this_thread::sleep_for(std::chrono::milliseconds(ret == 0 ? 1500 : 5000));
	}

	delete this;
}

void ThreadedCloudSync::start(Window* window, const std::string& command,
	const std::string& title, const std::string& running, Origin origin)
{
	if (ThreadedCloudSync::mInstance != nullptr)
	{
		window->pushGui(new GuiMsgBox(window, _("A CLOUD SYNC IS ALREADY RUNNING.")));
		return;
	}

	std::lock_guard<std::mutex> lock(sInstanceLock);
	ThreadedCloudSync::mInstance = new ThreadedCloudSync(window, command, title, running, origin);
}

bool ThreadedCloudSync::cancelIfWaitingForNetwork()
{
	std::lock_guard<std::mutex> lock(sInstanceLock);
	ThreadedCloudSync* sync = ThreadedCloudSync::mInstance;
	if (sync == nullptr || !sync->mWaitingForNetwork)
		return false;
	const pid_t pid = sync->mPid;
	if (pid <= 0)
		return false;

	// Cancelled before the signal, so run() finds it set however quickly
	// pclose returns. The whole group: the command runs under setsid, so its
	// pid is its process group, and ping and sleep are in it.
	//
	// There is a race, and it is accepted: the network can come up at the
	// instant of the launch, the shell moves on to cloud_restore, and the
	// SIGTERM lands on that instead of on a sleep. The restore is an rclone
	// copy -- each file written under a temporary name and renamed when
	// complete, nothing deleted -- so a copy cut short leaves no partial
	// file, and the next run finishes what this one started. The card and
	// the stamp still say a game was started, which is what happened.
	sync->mCancelled = true;
	sync->mWaitingForNetwork = false;
	::kill(-pid, SIGTERM);
	return true;
}

// /storage/.cache/cloud_sync/last-sync-<origin>: one line, "<epoch> <rc>",
// the same shape the scripts give last-backup and last-restore so one reader
// (GuiMenu's cloudLastRunDetail) serves all of them. Device-local, so under
// .cache rather than .config: a stamp carried in a settings backup onto a
// second device would describe a run that device never made.
//
// Written whole or not at all. A temp file and a rename: the reader is the
// menu, on another thread and possibly at this moment, and a half-written
// line parses as "never" -- the one thing the stamp exists to stop the row
// saying after a run.
void ThreadedCloudSync::recordOutcome(Origin origin, int rc)
{
	const char* name = origin == Origin::Startup ? "startup"
		: origin == Origin::Exit ? "exit"
		: origin == Origin::Manual ? "manual" : nullptr;
	if (name == nullptr)
		return;

	const std::string dir = "/storage/.cache/cloud_sync";
	if (!Utils::FileSystem::createDirectory(dir))
		return;

	const std::string path = dir + "/last-sync-" + name;
	const std::string tmp = path + ".tmp";
	Utils::FileSystem::writeAllText(tmp,
		std::to_string(static_cast<long long>(time(nullptr))) + " " + std::to_string(rc) + "\n");
	if (std::rename(tmp.c_str(), path.c_str()) != 0)
		std::remove(tmp.c_str());
}
