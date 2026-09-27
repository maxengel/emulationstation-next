#include "ProxyCards.h"

#include "CloudText.h"
#include "FileData.h"
#include "LocaleES.h"
#include "Log.h"
#include "OfflineAchievements.h"
#include "SystemConf.h"
#include "ThreadedCloudSync.h"
#include "Window.h"
#include "components/AsyncNotificationComponent.h"
#include "utils/FileSystemUtil.h"
#include "utils/StringUtil.h"

#include <atomic>
#include <chrono>
#include <ctime>
#include <future>
#include <memory>
#include <thread>
#include <vector>

#define TROPHY _U(" ")

namespace
{
	const char* EXIT_STAMP = "/storage/.cache/cloud_sync/last-sync-exit";
	const char* STARTUP_STAMP = "/storage/.cache/cloud_sync/last-sync-startup";
	const char* BACKUP_STAMP = "/storage/.cache/cloud_sync/last-backup";
	// What the send card said, in the last-sync shape (ThreadedCloudSync::writeStamp),
	// so "was it shown?" is a read on the device (#292).
	const char* LINK_STAMP = "/storage/.cache/cloud_sync/last-sync-link";
	const char* EXIT_SYNC = "/usr/bin/cloud_backup --yes --saves-only --recent --automatic";
	// The proxy sends within seconds of the link; a queue still not empty
	// after this long is a server not answering, and the card says so.
	const int SEND_BOUND_SECONDS = 45;

	std::atomic<bool> sSendRunning{ false };
	// #298: the saves that are owed follow every RetroAchievements card. The
	// send card is showing from its making to its close (the gate above lets
	// go earlier, as the outcome shows); the top-up is running from topUp()
	// to runTopUp's end; the saves are owed from the probe until whichever
	// of the two ends last starts them (handOff).
	static std::atomic<bool> sSendShowing{ false };
	static std::atomic<bool> sTopUpRunning{ false };
	static std::atomic<bool> sSavesOwed{ false };

	std::string readText(const char* path)
	{
		if (!Utils::FileSystem::exists(path, false))
			return "";
		return Utils::FileSystem::readAllText(path);
	}

	bool savesOwed()
	{
		if (SystemConf::getInstance()->get("cloudsaves.gameexit") != "1")
			return false;
		if (!Utils::FileSystem::exists("/usr/bin/cloud_backup"))
			return false;
		return CloudText::exitSyncOwed(readText(EXIT_STAMP), readText(STARTUP_STAMP), readText(BACKUP_STAMP));
	}

	// The exit sync that was owed, on the interface thread, when the screen
	// is free of a game and of another sync.
	void startOwedSaves(Window* window)
	{
		if (ThreadedCloudSync::isRunning() || FileData::GetRunningGame() != nullptr)
			return;
		ThreadedCloudSync::start(window, EXIT_SYNC, _("SYNC SAVES"), _("SYNCING SAVES TO THE CLOUD"), ThreadedCloudSync::Origin::Exit);
	}

	// The saves that are owed follow every RetroAchievements card (#298): the
	// send card and the top-up each ask on ending, and whichever finds the
	// other gone starts the saves. Before, the send's end started them and the
	// top-up's outcome landed after the saves card, between the achievements'
	// two cards; the maintainer asked for one batch.
	void handOff(Window* window)
	{
		if (sSendShowing || sTopUpRunning)
			return;
		if (sSavesOwed.exchange(false))
			window->postToUiThread([window] { startOwedSaves(window); });
	}

	// The send card's worker: follows the proxy's queue to its end, says the
	// outcome in the sync card's words, stamps it, and hands over to the
	// saves when they are owed.
	void runSend(Window* window, AsyncNotificationComponent* card)
	{
		int pending = OfflineAchievements::pendingAwards();
		const auto started = std::chrono::steady_clock::now();
		while (pending > 0 && std::chrono::steady_clock::now() - started < std::chrono::seconds(SEND_BOUND_SECONDS))
		{
			card->updateText(Utils::String::format(_("%d TO SEND").c_str(), pending));
			std::this_thread::sleep_for(std::chrono::seconds(1));
			pending = OfflineAchievements::pendingAwards();
		}
		const bool sent = OfflineAchievements::takeFlushed();
		// -1 is the ctl not answering: with the proxy's stamp in hand the
		// batch went and the card says so; without it, nothing can be said
		// to have been sent.
		const bool done = pending == 0 || (pending < 0 && sent);

		std::vector<std::string> action;
		std::string outcome, token, why;
		if (done)
		{
			outcome = _("COMPLETED");
			token = sent ? "sent" : "completed";
			// Longest first; the destination is implied, and named only
			// where the line has room for it (D-UI-096).
			action.push_back(_("OFFLINE ACHIEVEMENTS HAVE BEEN SENT TO RETROACHIEVEMENTS."));
			action.push_back(_("OFFLINE ACHIEVEMENTS HAVE BEEN SENT."));
		}
		else
		{
			why = pending < 0 ? _("THE OFFLINE ACHIEVEMENTS SERVICE DIDN'T ANSWER") : _("RETROACHIEVEMENTS STOPPED ANSWERING");
			outcome = std::string(_("COULDN'T FINISH")) + " - " + why;
			token = "not-sent";
			action.push_back(_("IT'LL TRY AGAIN WHEN YOU'RE CONNECTED."));
		}
		card->updateTitle(TROPHY + _("SEND OFFLINE ACHIEVEMENTS"));
		card->updateText(CloudText::outcomeCandidates(outcome), action);
		card->updatePercent(done ? 100 : -1);
		ThreadedCloudSync::writeStamp(LINK_STAMP, done ? 0 : 5, token, why);
		LOG(LogInfo) << "ProxyCards: the send card ended " << token << " (pending " << pending << ")";

		// The gate lets go as the outcome shows, as the sync card's does;
		// the card holds the outcome long enough to read, then goes.
		sSendRunning = false;
		std::this_thread::sleep_for(std::chrono::milliseconds(5000));
		card->close();
		sSendShowing = false;
		handOff(window);
	}

	// The top-up's watcher: the ctl on a thread of its own, this one attaching
	// a card once the ctl's progress file says it has work and no other card
	// holds the screen, and ending it with the ctl's stamp.
	// Every way out of runTopUp clears the running flag and asks whether the
	// saves that are owed may start (#298).
	struct TopUpEnd
	{
		Window* window;
		~TopUpEnd() { sTopUpRunning = false; handOff(window); }
	};

	void runTopUp(Window* window, bool afterIndex)
	{
		TopUpEnd ending{ window };
		std::atomic<bool> finished{ false };
		int rc = -1;
		const time_t startedAt = time(nullptr);
		std::thread ctl([&rc, &finished, afterIndex]
		{
			rc = OfflineAchievements::runTopUp(afterIndex);
			finished = true;
		});

		AsyncNotificationComponent* card = nullptr;
		bool sawWork = false;
		// A card is made on the interface thread and handed back here. When
		// the loop that runs posted tasks is not turning -- a game has the
		// screen -- the run goes on without a card, and the lambda, run
		// later, makes none.
		auto attach = [window]() -> AsyncNotificationComponent*
		{
			// `wanted` is flipped once, by whichever side gets there
			// first: the lambda, which then makes the card and hands it
			// over; or this thread on giving up, after which the lambda
			// makes nothing. A card is never made for nobody.
			auto made = std::make_shared<std::promise<AsyncNotificationComponent*>>();
			auto wanted = std::make_shared<std::atomic<bool>>(true);
			auto got = made->get_future();
			window->postToUiThread([window, made, wanted]
			{
				if (!wanted->exchange(false))
					return;
				AsyncNotificationComponent* c = window->createAsyncNotificationComponent(true);
				c->updateTitle(TROPHY + _("UPDATING OFFLINE ACHIEVEMENTS..."));
				c->updateText(_("STARTING..."));
				c->updatePercent(-1);
				made->set_value(c);
			});
			if (got.wait_for(std::chrono::seconds(3)) == std::future_status::ready || !wanted->exchange(false))
				return got.get();
			return nullptr;
		};
		auto screenFree = []
		{
			return !sSendRunning && !ThreadedCloudSync::isRunning() && FileData::GetRunningGame() == nullptr;
		};
		while (!finished)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(500));
			const CloudText::RunningProgress p = OfflineAchievements::runningProgress();
			if (!p.running)
				continue;
			sawWork = true;
			if (card == nullptr && screenFree())
				card = attach();
			if (card != nullptr && p.total > 0)
				card->updateText(Utils::String::format(_("%d OF %d").c_str(), p.index, p.total));
		}
		ctl.join();
		// A run too quick for the poll to have seen its progress file still
		// left its stamp: work was done when the stamp is this run's and
		// counts a game added or a fetch failed.
		const CloudText::ScanStamp s = OfflineAchievements::lastScan();
		if (!sawWork && s.ran && s.when >= startedAt && (s.cached > 0 || s.errors > 0))
			sawWork = true;
		if (!sawWork)
			return;   // a run with no work: nothing to show
		// A run that had work and ended before a card could be attached --
		// quick, or behind the send card -- still gets its outcome said,
		// once the screen is free (bounded: a game may have it).
		const auto waited = std::chrono::steady_clock::now();
		while (card == nullptr && std::chrono::steady_clock::now() - waited < std::chrono::seconds(90))
		{
			if (screenFree())
				card = attach();
			else
				std::this_thread::sleep_for(std::chrono::seconds(1));
		}
		if (card == nullptr)
			return;   // the screen stayed taken: the ctl's stamp keeps the outcome for the achievements page
		std::vector<std::string> action;
		std::string outcome;
		const bool ok = rc == 0;
		if (ok)
		{
			outcome = _("COMPLETED");
			// Added is said only of games new to the store; a run that re-read
			// what was there says how many are ready (#298, the maintainer:
			// "if it's just doing an update, it can say X number of games
			// ready for offline play"). A stamp an older ctl wrote has no
			// added=, and cached stood for it.
			const int added = s.added >= 0 ? s.added : s.cached;
			if (added == 1)
				action.push_back(_("1 GAME ADDED FOR OFFLINE PLAY."));
			else if (added > 1)
				action.push_back(Utils::String::format(_("%d GAMES ADDED FOR OFFLINE PLAY.").c_str(), added));
			else if (s.ready == 1)
				action.push_back(_("1 GAME READY FOR OFFLINE PLAY."));
			else if (s.ready > 1)
				action.push_back(Utils::String::format(_("%d GAMES READY FOR OFFLINE PLAY.").c_str(), s.ready));
			else
				action.push_back(_("YOUR OFFLINE ACHIEVEMENTS ARE UP TO DATE."));
		}
		else
		{
			outcome = std::string(_("COULDN'T FINISH")) + " - " + OfflineAchievements::scanWhy(s.why);
			action.push_back(_("IT'LL TRY AGAIN NEXT TIME YOU'RE CONNECTED."));
		}
		card->updateTitle(TROPHY + _("UPDATE OFFLINE ACHIEVEMENTS"));
		card->updateText(CloudText::outcomeCandidates(outcome), action);
		card->updatePercent(ok ? 100 : -1);
		LOG(LogInfo) << "ProxyCards: the top-up card ended rc " << rc << " cached " << s.cached << " added " << s.added << " ready " << s.ready;
		std::this_thread::sleep_for(std::chrono::milliseconds(5000));
		card->close();
	}
}

namespace ProxyCards
{
	// The probe and the wait, off the interface thread. `saves` is whether
	// the exit sync that is owed follows: yes from the link's return, no
	// from the end of a sync card -- a sync that could not run for want of
	// a network would otherwise call for itself again as it ended.
	static void probe(Window* window, bool saves)
	{
		std::thread([window, saves]
		{
			const bool toggle = OfflineAchievements::toggleOn();
			const int pending = toggle ? OfflineAchievements::pendingAwards() : 0;
			const bool flushed = toggle && OfflineAchievements::flushStampPresent();
			const bool owed = saves && savesOwed();
			if (pending <= 0 && !flushed)
			{
				// Nothing to send: the saves wait only for a top-up in flight (#298).
				if (owed)
				{
					sSavesOwed = true;
					handOff(window);
				}
				return;
			}
			// One floating surface at a time (D-UI-093): a sync card that is
			// up -- the startup sync, waiting for this very link -- finishes
			// and fades first. The proxy sends meanwhile, so what this card
			// then says is that the awards went. A sync that outlasts the
			// wait is followed by no card; the stamp keeps, and the sync
			// card's own end asks again.
			const auto started = std::chrono::steady_clock::now();
			while ((ThreadedCloudSync::isRunning() || FileData::GetRunningGame() != nullptr)
				&& std::chrono::steady_clock::now() - started < std::chrono::seconds(120))
				std::this_thread::sleep_for(std::chrono::seconds(1));
			if (ThreadedCloudSync::isRunning() || FileData::GetRunningGame() != nullptr)
				return;
			if (sSendRunning.exchange(true))
				return;   // another probe got there first
			window->postToUiThread([window, owed]
			{
				if (FileData::GetRunningGame() != nullptr || ThreadedCloudSync::isRunning())
				{
					sSendRunning = false;   // the proxy sends anyway; the screen is taken
					if (owed)
					{
						sSavesOwed = true;
						handOff(window);
					}
					return;
				}
				if (owed)
					sSavesOwed = true;
				sSendShowing = true;
				AsyncNotificationComponent* card = window->createAsyncNotificationComponent(true);
				card->updateTitle(TROPHY + _("SENDING OFFLINE ACHIEVEMENTS..."));
				card->updateText(_("STARTING..."));
				card->updatePercent(-1);
				std::thread(runSend, window, card).detach();
			});
		}).detach();
	}

	void startIfOwed(Window* window)
	{
		if (!OfflineAchievements::available() || sSendRunning)
			return;
		probe(window, true);
	}

	void afterSync(Window* window)
	{
		if (!OfflineAchievements::available() || sSendRunning)
			return;
		probe(window, false);
	}

	bool sendRunning()
	{
		return sSendRunning;
	}

	void topUp(Window* window, bool afterIndex)
	{
		if (!OfflineAchievements::available() || !OfflineAchievements::toggleOn())
			return;
		sTopUpRunning = true;   // before the thread, so a probe started beside it sees it (#298)
		std::thread(runTopUp, window, afterIndex).detach();
	}

	bool topUpRunning()
	{
		return OfflineAchievements::available() && OfflineAchievements::runningProgress().running;
	}

	bool stopTopUp()
	{
		return OfflineAchievements::stopRun();
	}
}
