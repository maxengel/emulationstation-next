#include "ThreadedCloudSync.h"
#include "CloudExit.h"
#include "Window.h"
#include "components/AsyncNotificationComponent.h"
#include "guis/GuiMsgBox.h"
#include "SystemConf.h"
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
	mGameExitSync = SystemConf::getInstance()->get("cloudsaves.gameexit") == "1";
	// With the action row. The default is a two-row card (title and text),
	// and this was created with the default, so the recovery clause run()
	// composes -- what is in place, and where to try again (D-CLOUD-077) --
	// was measured, chosen, and never drawn: the card had no row to draw it
	// on (guest d, 2026-09-10). The row is blank while the work runs and
	// carries the clause once the outcome is known.
	mWndNotification = mWindow->createAsyncNotificationComponent(true);
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

// The four outcome words and what follows them (D-UI-028, es-native-ui.md
// "Outcome vocabulary"): a why, what is in place, and how to recover.
// Nothing else -- no FAILED, no log path, no exit code, no rclone.

// The why for an exit code the scripts did not explain with a ">>> why"
// line of their own. rclone's codes; 130 is the scripts' trap, when it was
// not this process that stopped them.
std::string ThreadedCloudSync::whyForCode(int rc)
{
	switch (rc)
	{
		case 3: case 4: return _("YOUR CLOUD FOLDER WASN'T FOUND");
		case 5:         return _("YOUR CLOUD STOPPED ANSWERING");
		case 7: case 8: return _("YOUR CLOUD REFUSED THE TRANSFER");
		case CloudExit::Stopped:   return _("IT WAS STOPPED");
		// The sentinels, for a part that exited one beside a part that did
		// not (the transfer page's line 4): the same words the SKIPPED
		// outcome uses, so one code is never called two things.
		case CloudExit::NoNetwork: return _("NO NETWORK CONNECTION");
		case CloudExit::LockHeld:  return _("ANOTHER CLOUD SYNC IS RUNNING");
		default:        return _("SOMETHING WENT WRONG");
	}
}

// The stamp's one-word token for the same code, for the row's reader.
std::string ThreadedCloudSync::tokenForCode(int rc)
{
	switch (rc)
	{
		case 0: case 9: return "completed";
		case 3: case 4: return "folder-missing";
		case 5:         return "cloud-stopped";
		case 7: case 8: return "cloud-refused";
		case CloudExit::Stopped:   return "stopped";
		case CloudExit::NoNetwork: return "no-network";
		case CloudExit::LockHeld:  return "lock-held";
		default:        return "unknown";
	}
}

// The token read back: the phrase for one the table above wrote, "" for
// one it did not (the stamp's code decides then).
std::string ThreadedCloudSync::whyForToken(const std::string& token)
{
	if (token == "folder-missing") return whyForCode(3);
	if (token == "cloud-stopped")  return whyForCode(5);
	if (token == "cloud-refused")  return whyForCode(7);
	if (token == "stopped")        return whyForCode(CloudExit::Stopped);
	return "";
}

// Which way the saves moved, read from the command: the in-place clause is
// one per verb, true because rclone renames each file into place when it is
// complete (D-CLOUD-077).
enum class Verb { Sync, Backup, Restore, Other };

static Verb verbOf(const std::string& cmd)
{
	const bool restore = cmd.find("cloud_restore") != std::string::npos;
	const bool backup  = cmd.find("cloud_backup")  != std::string::npos || cmd.find("backuptool") != std::string::npos;
	if (restore && backup) return Verb::Sync;
	if (restore) return Verb::Restore;
	if (backup)  return Verb::Backup;
	return Verb::Other;
}

static std::string inPlaceClause(Verb verb, bool moved)
{
	switch (verb)
	{
		case Verb::Sync:    return moved ? _("THE SAVES THAT MOVED ARE ON BOTH SIDES. THE REST ARE AS THEY WERE.") : _("YOUR SAVES ARE AS THEY WERE.");
		case Verb::Backup:  return moved ? _("WHAT WAS SENT IS IN YOUR CLOUD. THE REST IS STILL ON THIS DEVICE.") : _("NOTHING WAS SENT. YOUR CLOUD IS AS IT WAS.");
		case Verb::Restore: return moved ? _("WHAT ARRIVED IS ON THIS DEVICE. THE REST IS AS IT WAS.") : _("NOTHING ARRIVED. THIS DEVICE IS AS IT WAS.");
		default:            return "";
	}
}

void ThreadedCloudSync::run()
{
	// Stream the backend's output into the notification card so the user
	// sees live status (rclone --stats-one-line lines, phase banners, ...).
	int ret = -1;

	// Every command runs in a session of its own and says so on its first
	// line: setsid makes the shell a process group leader, and ">>> pid N"
	// tells cancelForLaunch which group to signal, so the shell, the
	// scripts and their rclone children go together. Done here rather than
	// by each caller because the one caller that wrapped its own command
	// (the startup sync) was the only one that could be cancelled -- the
	// after-a-game backup ran bare, with no group to send a signal to.
	// shellQuote, so a command with a quote in it survives the trip.
	const std::string wrapped = "setsid sh -c "
		+ Utils::String::shellQuote("echo \">>> pid $$\"; " + mCommand) + " 2>&1";
	FILE* pipe = popen(wrapped.c_str(), "r");
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
			// player. ">>> pid N" is the wrapper above saying which process
			// group the command is (for cancelForLaunch); ">>> doing network"
			// says it is waiting for the network, the one wait this card
			// shows -- the startup sync (fork #94) gives the network up to a
			// minute to come up after boot, and a card reading "Working..."
			// for that minute says nothing about why. ">>> why <sentence>" is
			// the scripts saying, at the point of failure and in the
			// player's words, what went wrong -- the last one is the outcome
			// line's why (D-UI-028; it replaced a filter that showed any line
			// containing ERROR, FAILED or WARN, which passed rclone's prose
			// and dropped the scripts' own diagnoses). ">>> tier <label>|<rc>"
			// is a composed command reporting each of its parts as it ends,
			// so a run where one part finished and another did not is
			// reported as that (COMPLETED WITH GAPS) rather than as the last
			// part's code. ">>> unit" and anything newer is for the transfer
			// page and never reaches the card -- but each one says the wait
			// is over, as does the first word any script prints.
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
				else if (clean.rfind(">>> why ", 0) == 0)
				{
					mWaitingForNetwork = false;
					std::string why = Utils::String::toUpper(Utils::String::trim(clean.substr(8)));
					// The outcome line supplies its own end; a sentence's
					// full stop after a dash reads as a typo.
					while (!why.empty() && why.back() == '.')
						why.pop_back();
					if (!why.empty())
						mWhy = why;
				}
				else if (clean.rfind(">>> tier ", 0) == 0)
				{
					mWaitingForNetwork = false;
					auto parts = Utils::String::split(clean.substr(9), '|', false);
					const std::string label = parts.size() > 0 ? Utils::String::trim(parts[0]) : "";
					const int rc = parts.size() > 1 ? atoi(Utils::String::trim(parts[1]).c_str()) : -1;
					if (!label.empty())
						mTiers.push_back(std::make_pair(Utils::String::toUpper(label), rc));
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
			// So: transfer progress, and nothing else. A line carries
			// progress if it has a percentage or a "x / y" count. What went
			// wrong arrives on the protocol line above, in the scripts' own
			// words, and is said once at the end.
			const bool hasPercent = clean.find('%') != std::string::npos;
			const bool hasCount   = clean.find(" / ") != std::string::npos;
			const bool informative = hasPercent || hasCount;

			// rclone's own line is written for a terminal:
			// "Transferred: 12.345 MiB / 45.678 MiB, 27%, 1.234 MiB/s, ETA 27s".
			// On a handheld card the useful part is the front of it, and the
			// speed and ETA push everything else off the end.
			auto eta = clean.find(", ETA ");
			if (eta != std::string::npos)
				clean = clean.substr(0, eta);
			if (clean.rfind("Transferred:", 0) == 0)
			{
				clean = Utils::String::trim(clean.substr(12));
				// The byte line of a stats block ("80 KiB / 300 KiB, 27%"; the
				// count line has no unit) leaving "0 B" is the one fact the
				// in-place clause turns on: something reached the other side.
				const bool byteLine = clean.find("iB") != std::string::npos || clean.find(" B") != std::string::npos;
				if (byteLine && clean.rfind("0 B /", 0) != 0)
					mMoved = true;
			}

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

	// The outcome, in the four words every cloud surface uses (D-UI-028).
	//
	// COMPLETED when the whole run did (rclone's 9 -- nothing needed
	// moving -- counts). SKIPPED for the two sentinels the scripts exit
	// before touching anything (CloudExit.h) and for the launch cancel; none
	// of the three is a failure, and FAILED would send somebody to a log to
	// find nothing wrong. COMPLETED WITH GAPS when a composed run's parts
	// disagree -- the startup sync's restore finished and its backup did
	// not; the old card read the whole run as failed. COULDN'T FINISH for
	// everything else, with the why: the scripts' own sentence when they
	// printed one, the code's phrase otherwise.
	std::vector<std::string> okTiers, badTiers;
	for (auto& t : mTiers)
		(t.second == 0 || t.second == 9 ? okTiers : badTiers).push_back(t.first);
	const bool completed = !cancelled && (ret == 0 || ret == 9);
	const bool gaps = !cancelled && !completed && !okTiers.empty() && !badTiers.empty();
	const std::string why = mWhy.empty() ? whyForCode(ret) : mWhy;

	std::string outcome, token;
	if (cancelled)
	{
		outcome = _("SKIPPED - A GAME WAS STARTED");
		token = "cancelled";
	}
	else if (completed)
	{
		outcome = _("COMPLETED");
		token = "completed";
	}
	else if (gaps)
	{
		std::string names;
		for (size_t i = 0; i < badTiers.size(); i++)
			names += (i ? ", " : "") + badTiers[i];
		outcome = _("COMPLETED WITH GAPS") + std::string(" - ") + names + " " + _("DID NOT FINISH");
		token = "gaps";
	}
	else if (ret == CloudExit::LockHeld)
	{
		outcome = _("SKIPPED - ANOTHER CLOUD SYNC IS RUNNING");
		token = "lock-held";
	}
	else if (ret == CloudExit::NoNetwork)
	{
		outcome = _("SKIPPED - NO NETWORK CONNECTION");
		token = "no-network";
	}
	else
	{
		outcome = _("COULDN'T FINISH") + std::string(" - ") + why;
		token = tokenForCode(ret);
	}

	// Before the card says anything: the stamp is the answer that outlives
	// the card, so it is written first, and written whether or not there is
	// still a card to say it on.
	recordOutcome(mOrigin, ret, token, mWhy);

	// One surface for the whole event.
	//
	// The card used to vanish the instant the work ended, and the outcome
	// arrived as a GuiInfoPopup: a different shape, in a different place,
	// at exactly the moment somebody is looking for the answer. Two things
	// appeared where one thing happened. Say it in the card that has been
	// reporting all along, hold it long enough to read, and let that same
	// card fade.
	if (mWndNotification != nullptr)
	{
		mWndNotification->updateTitle(ICONINDEX + mTitle);

		// The action line, blank until now: what is in place, and how to
		// recover (D-CLOUD-077). The in-place clause is the verb's; "moved"
		// is whether rclone's byte totals ever left zero. The recovery
		// clause names the surface that runs it again: for an automatic
		// sync, when that is; for one the player pressed, the row.
		std::vector<std::string> action;
		if (!completed)
		{
			const Verb verb = verbOf(mCommand);
			const std::string inPlace = inPlaceClause(verb, mMoved);

			std::string recover;
			if (cancelled && mGameExitSync)
				recover = _("YOUR SAVES ARE SENT WHEN YOU EXIT THE GAME.");
			else if (mOrigin == Origin::Startup)
				recover = _("IT RUNS AGAIN AT THE NEXT STARTUP, OR UNDER GAME SETTINGS > SYNC SAVES WITH THE CLOUD");
			else if (mOrigin == Origin::Exit)
				recover = _("IT RUNS AGAIN WHEN YOU EXIT A GAME");
			else if (ret == CloudExit::NoNetwork)
				recover = _("TRY AGAIN WHEN YOU'RE ONLINE.");
			else if (ret == CloudExit::LockHeld)
				recover = _("WAIT FOR IT TO FINISH, THEN TRY AGAIN.");
			else if (mOrigin == Origin::Manual)
				recover = _("TRY AGAIN: GAME SETTINGS > ") + std::string(
					verb == Verb::Sync ? _("SYNC SAVES WITH THE CLOUD")
					: verb == Verb::Restore ? _("RESTORE SAVES FROM THE CLOUD")
					: _("BACK UP SAVES TO THE CLOUD"));
			else if (mCommand.find("cloud_migrate_layout") != std::string::npos)
				recover = _("TRY AGAIN: GAME SETTINGS > MANAGE CLOUD STORAGE > TIDY UP YOUR CLOUD FOLDERS");
			else
				recover = _("TRY AGAIN: GAME SETTINGS > MANAGE CLOUD STORAGE > BACK UP TO THE CLOUD");

			// Candidates, longest first; the card measures them in the row's
			// own font on the interface thread and shows the first that
			// fits (AsyncNotificationComponent). If both clauses do not fit
			// on the one line, the in-place clause goes first
			// (es-native-ui.md): the outcome word above already implies
			// it, and the recovery is the part nobody can guess. The
			// startup sentence has a short form for a panel where even it
			// alone does not fit.
			if (!inPlace.empty())
				action.push_back(inPlace + " " + recover);
			action.push_back(recover);
			if (mOrigin == Origin::Startup)
				action.push_back(_("IT RUNS AGAIN AT THE NEXT STARTUP."));
		}
		mWndNotification->updateText(outcome, action);

		// A full bar on success; otherwise the bar goes, because a progress
		// bar left standing under COULDN'T FINISH reads as a measure of how
		// much of the failure has completed.
		mWndNotification->updatePercent(completed ? 100 : -1);

		// Nothing is running any more, so stop claiming otherwise: somebody
		// who wants to start another sync while the card is still up should
		// not be told one is already going. Under the lock, so a
		// cancelForLaunch that has just taken the pointer finishes
		// with it before it goes -- and the delete below is a linger later.
		{
			std::lock_guard<std::mutex> lock(sInstanceLock);
			if (ThreadedCloudSync::mInstance == this)
				ThreadedCloudSync::mInstance = nullptr;
		}

		// Hold the outcome long enough to read, then let the card fade.
		// Success is one word and a full bar, and somebody who just exited
		// a game is standing there watching it, so a second and a half (two
		// lingered -- maintainer, 2026-09-07); anything else is two lines to
		// act on, so five. Five for everything dated from when a sync took
		// 18 seconds -- once the exit sync came down to about five, the card
		// spent as long saying it was done as it had spent working.
		std::this_thread::sleep_for(std::chrono::milliseconds(completed ? 1500 : 5000));
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

bool ThreadedCloudSync::cancelForLaunch()
{
	ThreadedCloudSync* sync = nullptr;
	pid_t pid = 0;
	{
		std::lock_guard<std::mutex> lock(sInstanceLock);
		sync = ThreadedCloudSync::mInstance;
		if (sync == nullptr)
			return false;
		// The player pressed this one; the launch does not override it.
		if (sync->mOrigin != Origin::Startup && sync->mOrigin != Origin::Exit)
			return false;

		// Cancelled before the signal, so run() finds it set however quickly
		// pclose returns. The whole group: the command runs under setsid, so
		// its pid is its process group, and the scripts, their rclone and any
		// ping or sleep are in it.
		sync->mCancelled = true;
		sync->mWaitingForNetwork = false;
		pid = sync->mPid;
		if (pid > 0)
			::kill(-pid, SIGTERM);
	}

	// Now wait for it to be gone, and only then let the launch go ahead.
	//
	// The signal is not the end of the sync; the process ending is. rclone
	// copy writes each file under a temporary name and renames it into
	// place when complete, and a rename that lands after the emulator has
	// opened that save is the one thing this gate exists to prevent -- a
	// game about to write a save must not have a restore rename over it
	// underneath. So the launch waits for run() to report the process gone:
	// it clears mInstance under the lock once pclose has returned, which
	// is once every writer to the pipe has exited. Two seconds is the
	// budget; at one and a half the group is sent SIGKILL, for an rclone
	// that is slow to act on SIGTERM. Past the budget the answer is no, and
	// the caller refuses the launch as it always did -- a sync that will
	// not die is not one to start a game over.
	const auto started = std::chrono::steady_clock::now();
	bool killed = false;
	for (;;)
	{
		{
			std::lock_guard<std::mutex> lock(sInstanceLock);
			if (ThreadedCloudSync::mInstance != sync)
				return true;
			// Its first line had not arrived when we looked -- the command
			// was only just started. Signal it as soon as it says who it is.
			// Safe to read: mInstance still names it, so it is not deleted.
			if (pid <= 0)
			{
				pid = sync->mPid;
				if (pid > 0)
					::kill(-pid, SIGTERM);
			}
		}

		const long elapsed = (long) std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now() - started).count();
		if (elapsed >= 2000)
			return false;
		if (!killed && elapsed >= 1500 && pid > 0)
		{
			::kill(-pid, SIGKILL);
			killed = true;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(20));
	}
}

// /storage/.cache/cloud_sync/last-sync-<origin>: one line, "<epoch> <rc>
// <token>[ <why>]" -- the first two fields the shape the scripts give
// last-backup and last-restore, so one reader (GuiMenu's cloudLastRunDetail)
// serves all of them; the third and any after it are additive (D-UI-028):
// the one-word token names the outcome where the code alone cannot (a 130
// that was a launch cancel against one that was not; a composed run that
// completed with gaps), and the why is the scripts' own sentence when they
// printed one, for the row to show in place of the token's phrase. Readers
// split on space and read what is there. Device-local, so under .cache
// rather than .config: a stamp carried in a settings backup onto a second
// device would describe a run that device never made.
//
// Written whole or not at all. A temp file and a rename: the reader is the
// menu, on another thread and possibly at this moment, and a half-written
// line parses as "never" -- the one thing the stamp exists to stop the row
// saying after a run.
void ThreadedCloudSync::recordOutcome(Origin origin, int rc, const std::string& token, const std::string& why)
{
	const char* name = origin == Origin::Startup ? "startup"
		: origin == Origin::Exit ? "exit"
		: origin == Origin::Manual ? "manual" : nullptr;
	if (name == nullptr)
		return;

	const std::string dir = "/storage/.cache/cloud_sync";
	if (!Utils::FileSystem::createDirectory(dir))
		return;

	// One line: the why is kept to printable characters and a single line
	// so the reader's split cannot be confused by it.
	std::string sentence;
	for (char c : why)
		if (c >= 32 && c < 127)
			sentence += c;
	sentence = Utils::String::trim(sentence);

	const std::string path = dir + "/last-sync-" + name;
	const std::string tmp = path + ".tmp";
	Utils::FileSystem::writeAllText(tmp,
		std::to_string(static_cast<long long>(time(nullptr))) + " " + std::to_string(rc) + " " + token
		+ (sentence.empty() ? "" : " " + sentence) + "\n");
	if (std::rename(tmp.c_str(), path.c_str()) != 0)
		std::remove(tmp.c_str());
}
