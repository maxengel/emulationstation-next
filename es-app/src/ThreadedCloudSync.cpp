#include "ThreadedCloudSync.h"
#include "CloudExit.h"
#include "CloudText.h"
#include "Window.h"
#include "components/AsyncNotificationComponent.h"
#include "guis/GuiMsgBox.h"
#include "ApiSystem.h"
#include "guis/GuiLoading.h"
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
	mWndNotification->updateText(_("STARTING..."));
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
		case 3: case 4: return _("COULDN'T FIND YOUR CLOUD FOLDER");
		case 5:         return _("YOUR CLOUD STOPPED ANSWERING");
		case 7: case 8: return _("YOUR CLOUD WOULDN'T TAKE THE FILES");
		case CloudExit::Stopped:   return _("IT WAS STOPPED");
		// The sentinels, for a part that exited one beside a part that did
		// not (the transfer page's line 4): the same words the SKIPPED
		// outcome uses, so one code is never called two things.
		case CloudExit::NoNetwork: return _("YOU'RE NOT ONLINE");
		case CloudExit::LockHeld:  return _("A SYNC IS ALREADY RUNNING");
		default:        return _("SOMETHING WENT WRONG");
	}
}

// The short forms of a why sentence, for a panel the whole one does not fit
// on (#115), are CloudText::shortenWhy and CloudText::outcomeCandidates --
// pure string work, checked by es-app/tests/unit.

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

// Which way the saves moved, read from the command (CloudText::verbOf): the
// in-place clause is one per verb, true because rclone renames each file
// into place when it is complete (D-CLOUD-077).
static std::string inPlaceClause(CloudText::Verb verb, bool moved)
{
	switch (verb)
	{
		case CloudText::Verb::Sync:    return moved ? _("THE SAVES THAT MADE IT ARE ON BOTH SIDES. NOTHING ELSE CHANGED.") : _("DON'T WORRY, NOTHING CHANGED.");
		case CloudText::Verb::Backup:  return moved ? _("WHAT MADE IT IS IN YOUR CLOUD. THE REST IS STILL HERE.") : _("DON'T WORRY, NOTHING CHANGED.");
		case CloudText::Verb::Restore: return moved ? _("WHAT MADE IT IS ON THIS DEVICE. NOTHING ELSE CHANGED.") : _("DON'T WORRY, NOTHING CHANGED.");
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
			// reported as a failure with that part's why, rather than as the last
			// part's code. ">>> unit" and anything newer is for the transfer
			// page and never reaches the card -- but each one says the wait
			// is over, as does the first word any script prints.
			//
			// Which line is which is CloudText::classifyProtocolLine, so the
			// shapes can be checked without a pipe (es-app/tests/unit); what
			// each one does to this thread stays here.
			const CloudText::ProtocolLine protocol = CloudText::classifyProtocolLine(clean);
			if (protocol.kind != CloudText::ProtocolKind::NotProtocol)
			{
				switch (protocol.kind)
				{
				case CloudText::ProtocolKind::Pid:
					mPid = protocol.number;
					break;
				case CloudText::ProtocolKind::Doing:
				{
					// The local answer, not the member: cancelForLaunch clears
					// the flag from another thread, and this line is about the
					// line just read.
					const bool network = (protocol.text == "network");
					mWaitingForNetwork = network;
					if (network && mWndNotification != nullptr)
						mWndNotification->updateText(_("WAITING FOR THE NETWORK..."));
					break;
				}
				case CloudText::ProtocolKind::Why:
					mWaitingForNetwork = false;
					if (!protocol.text.empty())
						mWhy = protocol.text;
					break;
				case CloudText::ProtocolKind::Offer:
					// A script asking for a question to be put to the player
					// once the run is over. The only one today: a cloud that
					// answers with no saves folder in it, which is not a
					// failure but does leave the player with nothing to
					// restore and no obvious way forward (#100, D-CLOUD-085).
					mWaitingForNetwork = false;
					mOffer = protocol.text;
					break;
				case CloudText::ProtocolKind::Tier:
					mWaitingForNetwork = false;
					if (!protocol.text.empty())
						mTiers.push_back(std::make_pair(protocol.text, protocol.number));
					break;
				default:
					mWaitingForNetwork = false;
					break;
				}
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
				clean = _("COMPARING YOUR SAVES WITH THE CLOUD") + std::string(" ") + count;
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
	// find nothing wrong. COULDN'T FINISH for everything else, with the why:
	// the scripts' own sentence when they printed one, the code's phrase
	// otherwise.
	//
	// A composed run whose parts disagree -- the startup sync's restore
	// finished and its backup did not -- is a failure like any other. It
	// read COMPLETED WITH GAPS for a day, and the maintainer's verdict on
	// seeing one (2026-09-10) is that a half-outcome the player cannot act
	// on is worse than either plain answer: "if you don't know what the gaps
	// are, it's not very helpful ... it just makes you more anxious and
	// trust the system less because it's working kind of. You'd rather just
	// know it couldn't connect or it could connect." So the word is
	// COULDN'T FINISH and the why is the failing part's; the action line
	// below still says truthfully what did move. The stamp keeps the token
	// so a log can still tell a partial run from a total one.
	std::vector<std::string> okTiers, badTiers;
	for (auto& t : mTiers)
		(t.second == 0 || t.second == 9 ? okTiers : badTiers).push_back(t.first);
	const bool completed = !cancelled && (ret == 0 || ret == 9);
	const bool gaps = !cancelled && !completed && !okTiers.empty() && !badTiers.empty();
	const std::string why = mWhy.empty() ? whyForCode(ret) : mWhy;

	std::string outcome, token;
	if (cancelled)
	{
		outcome = _("SKIPPED - YOU STARTED A GAME");
		token = "cancelled";
	}
	else if (completed)
	{
		outcome = _("COMPLETED");
		token = "completed";
	}
	else if (gaps)
	{
		outcome = _("COULDN'T FINISH") + std::string(" - ") + why;
		token = "gaps";
	}
	else if (ret == CloudExit::LockHeld)
	{
		outcome = _("SKIPPED - A SYNC IS ALREADY RUNNING");
		token = "lock-held";
	}
	else if (ret == CloudExit::NoNetwork)
	{
		outcome = _("SKIPPED - YOU'RE NOT ONLINE");
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
	//
	// The manual stamp (last-sync-manual) is the SYNC SAVES WITH THE CLOUD
	// row's. A manual backup or restore is stamped by its script (last-backup,
	// last-restore), which the BACK UP and RESTORE rows read; writing
	// last-sync-manual for those too put a backup's outcome under the sync
	// row -- LAST 00:48 - COULDN'T FINISH on a row nobody had pressed (guest
	// d, 2026-09-10). Automatic origins stamp whatever they ran.
	if (mOrigin != Origin::Manual || CloudText::verbOf(mCommand) == CloudText::Verb::Sync)
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
			const CloudText::Verb verb = CloudText::verbOf(mCommand);
			const std::string inPlace = inPlaceClause(verb, mMoved);

			std::string recover;
			if (cancelled && mGameExitSync)
				recover = _("YOUR SAVES GO UP WHEN YOU EXIT THE GAME.");
			else if (mOrigin == Origin::Startup)
				recover = _("IT'LL TRY AGAIN AT STARTUP, OR SYNC NOW FROM GAME SETTINGS.");
			else if (mOrigin == Origin::Exit)
				recover = _("IT'LL TRY AGAIN WHEN YOU EXIT A GAME.");
			else if (ret == CloudExit::NoNetwork)
				recover = _("TRY AGAIN WHEN YOU'RE ONLINE.");
			else if (ret == CloudExit::LockHeld)
				recover = _("WAIT FOR IT TO FINISH, THEN TRY AGAIN.");
			else if (mOrigin == Origin::Manual)
				recover = _("TRY AGAIN FROM GAME SETTINGS > ") + std::string(
					verb == CloudText::Verb::Sync ? _("SYNC SAVES WITH THE CLOUD")
					: verb == CloudText::Verb::Restore ? _("RESTORE SAVES FROM THE CLOUD")
					: _("BACK UP SAVES TO THE CLOUD"));
			else if (mCommand.find("cloud_migrate_layout") != std::string::npos)
				recover = _("TRY AGAIN FROM MANAGE CLOUD STORAGE > TIDY UP YOUR CLOUD FOLDERS");
			else
				recover = _("TRY AGAIN FROM MANAGE CLOUD STORAGE > BACK UP TO THE CLOUD");

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
				action.push_back(_("IT'LL TRY AGAIN NEXT STARTUP."));
		}
		// The outcome line, from candidates too, and for the same reason
		// as the action line (#115): it is composed -- the outcome word,
		// then the why -- and the why is a whole sentence. Longest first:
		// the whole thing, then the why with its trailing clause dropped,
		// then the outcome word alone, which fits any panel this runs on.
		// A translation whose outcome line carries no " - " has no split
		// to make and gets the single candidate it has today.
		mWndNotification->updateText(CloudText::outcomeCandidates(outcome), action);

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

	// A question the run asked us to put to the player, once its card has
	// had its say. Pushed on the interface thread, and only when the run
	// completed -- an offer to create a folder on top of a failure is one
	// thing too many to read at once.
	if (completed && mOffer == "create-saves-folder")
	{
		Window* window = mWindow;
		window->postToUiThread([window]()
		{
			window->pushGui(new GuiMsgBox(window,
				_("YOUR CLOUD HAS NO SAVES FOLDER YET, SO THERE WAS NOTHING TO BRING BACK.\n\nCREATE IT NOW, READY FOR YOUR FIRST BACKUP?"),
				_("CREATE IT"), [window]
				{
					window->pushGui(new GuiLoading<int>(window, _("SETTING UP YOUR CLOUD FOLDERS"),
						[](auto gui)
						{
							return ApiSystem::executeScriptLegacy("timeout 90 /usr/bin/cloud_setup --seed-folders",
								[](const std::string) {}).second;
						},
						[window](int rc)
						{
							window->pushGui(new GuiMsgBox(window, rc == 0
								? _("DONE. YOUR SAVES WILL GO THERE THE NEXT TIME YOU BACK THEM UP.")
								: _("COULDN'T CREATE IT. CHECK YOUR CONNECTION AND TRY AGAIN FROM MANAGE CLOUD STORAGE."),
								_("OK")));
						}));
				},
				_("NOT NOW"), nullptr));
		});
	}

	delete this;
}

void ThreadedCloudSync::start(Window* window, const std::string& command,
	const std::string& title, const std::string& running, Origin origin)
{
	if (ThreadedCloudSync::mInstance != nullptr)
	{
		window->pushGui(new GuiMsgBox(window, _("A SYNC IS ALREADY RUNNING.")));
		return;
	}

	std::lock_guard<std::mutex> lock(sInstanceLock);
	ThreadedCloudSync::mInstance = new ThreadedCloudSync(window, command, title, running, origin);
}

bool ThreadedCloudSync::cancelForLaunch(CancelRefusal* refusal)
{
	// Stopping unless we find otherwise: it covers the sync that was
	// signalled and has not gone yet, and the one that had already gone
	// before we took the lock. Both are answered by trying again in a
	// moment; only the player's own sync is answered by waiting.
	if (refusal != nullptr)
		*refusal = CancelRefusal::Stopping;

	ThreadedCloudSync* sync = nullptr;
	pid_t pid = 0;
	{
		std::lock_guard<std::mutex> lock(sInstanceLock);
		sync = ThreadedCloudSync::mInstance;
		if (sync == nullptr)
			return false;
		// The player pressed this one; the launch does not override it.
		if (sync->mOrigin != Origin::Startup && sync->mOrigin != Origin::Exit)
		{
			if (refusal != nullptr)
				*refusal = CancelRefusal::PlayerStarted;
			return false;
		}

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
