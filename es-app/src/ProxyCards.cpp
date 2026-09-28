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
	// #305 (the maintainer: saves first, then the RetroAchievements work as
	// one batch): at the link's return the owed saves sync goes first when
	// one is owed, and the batch -- the send card, the top-up -- starts when
	// its card ends (afterSync); with no saves owed the batch runs at once.
	// It was the other way round (#298), and the send card's stamp, written
	// by the proxy seconds after its queue empties, was found by the probe
	// at the saves card's end and shown as a second card for the same batch.
	static std::atomic<bool> sSendShowing{ false };
	// One top-up watcher at a time (audit #307 PL-056): a second one read
	// the same progress file and raised a second card beside the first,
	// whose ctl had refused it (75) and whose card then said COULDN'T
	// FINISH. A request that arrives while the watcher runs is not dropped
	// either -- the index's run (--after-index) is the one that lists the
	// games the index just found, and nothing else would run it -- so it
	// is kept here, one bit a kind, for the watcher to run when its run
	// ends: 1 the link's return, 2 the index's.
	static std::atomic<bool> sTopUpRunning{ false };
	static std::atomic<int> sTopUpWanted{ 0 };
	static std::atomic<bool> sBatchOwed{ false };
	// A stamp the proxy writes after the queue has emptied is the send
	// card's to take: it waits this long for it before saying the outcome.
	const int STAMP_WAIT_SECONDS = 10;

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
		LOG(LogInfo) << "ProxyCards: the owed saves sync starts";
		ThreadedCloudSync::start(window, EXIT_SYNC, _("SYNC SAVES"), _("SYNCING SAVES TO THE CLOUD"), ThreadedCloudSync::Origin::Exit);
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
			card->updateText(Utils::String::format(_("SENDING %d EARNED OFFLINE...").c_str(), pending));
			std::this_thread::sleep_for(std::chrono::seconds(1));
			pending = OfflineAchievements::pendingAwards();
		}
		// The proxy writes its flush stamp at "Flush complete", seconds after
		// its queue has emptied; the card that reports the batch takes it,
		// or the probe at the next sync card's end would (#305: a second
		// card for the same three awards on the RG35XX SP, 2026-09-27).
		bool sent = OfflineAchievements::takeFlushed();
		if (!sent && pending == 0)
		{
			const auto waited = std::chrono::steady_clock::now();
			while (!OfflineAchievements::flushStampPresent()
				&& std::chrono::steady_clock::now() - waited < std::chrono::seconds(STAMP_WAIT_SECONDS))
				std::this_thread::sleep_for(std::chrono::milliseconds(500));
			sent = OfflineAchievements::takeFlushed();
		}
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
			// The sentence says the awards reached the account, and only the
			// proxy's flush stamp is evidence of that (audit #307 PL-054). A
			// queue that emptied with no stamp after the wait above was not
			// shown to have gone anywhere -- the toggle turned off, the store
			// replaced -- so the card says COMPLETED and nothing more.
			// Longest first (D-UI-096); the title names RetroAchievements, so
			// the line says what happened and nothing the title said (D-UI-107).
			if (sent)
			{
				action.push_back(_("WHAT YOU EARNED OFFLINE IS NOW ON YOUR ACCOUNT."));
				action.push_back(_("NOW ON YOUR ACCOUNT."));
			}
		}
		else
		{
			why = pending < 0 ? _("THIS DEVICE'S OFFLINE SERVICE DIDN'T ANSWER") : _("IT STOPPED ANSWERING");
			outcome = std::string(_("COULDN'T FINISH")) + " - " + why;
			token = "not-sent";
			action.push_back(_("IT'LL TRY AGAIN WHEN YOU'RE CONNECTED."));
		}
		card->updateTitle(TROPHY + _("RETROACHIEVEMENTS"));
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
	}

	// One run of the top-up: the ctl on a thread of its own, this one
	// attaching a card once the ctl's progress file says it has work and no
	// other card holds the screen, and ending it with the ctl's stamp.
	// topUpWatcher runs these one after another.
	// One card for both ways an index can end offline (D-UI-104): the ctl's
	// probe refused, or the hash library never came. The maintainer's shape
	// and words (D-UI-106, 2026-09-27): the trophy and a title, the sentence
	// under it, as the other RetroAchievements cards are drawn -- a one-line
	// toast clipped the first sentence at 640x480 and had no room for a
	// second (D-UI-105). Five seconds, as those cards' outcomes stand.
	static void offlineIndexCard(Window* window)
	{
		window->postToUiThread([window]
		{
			AsyncNotificationComponent* card = window->createAsyncNotificationComponent();
			card->updateTitle(TROPHY + _("RETROACHIEVEMENTS (OFFLINE)"));
			card->updateText(_("NEWLY ADDED GAMES WILL BE ENABLED ONCE YOU RECONNECT."));
			std::thread([card]
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(5000));
				card->close();
			}).detach();
		});
	}

	void runTopUp(Window* window, bool afterIndex)
	{
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
				c->updateTitle(TROPHY + _("RETROACHIEVEMENTS (OFFLINE)"));
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
			// Every run the ctl makes is worth its card (fork #299, D-UI-103): the
			// link's return runs only when a game was played since the last
			// attempt, and the index's run only when it found new games, so a run
			// that shows is a run with something to look for -- and the card's
			// words say what it is doing.
			sawWork = true;
			if (card == nullptr && screenFree())
				card = attach();
			if (card != nullptr && p.total > 0)
				card->updateText(Utils::String::format(_("GETTING GAME %d OF %d READY...").c_str(), p.index, p.total));
		}
		ctl.join();
		// A game list updated offline (fork #299, D-UI-104): the index found
		// games and the ctl could not reach RetroAchievements (69). It left
		// its marker, and the link's return lists the library once for it,
		// so the toast's promise is the ctl's to keep.
		if (afterIndex && rc == 69)
		{
			LOG(LogInfo) << "ProxyCards: the index's top-up was refused offline; the card says the new games are enabled once reconnected";
			offlineIndexCard(window);
		}
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
			// A run with nothing new writes no stamp (the row's line keeps the
			// last scan that said something), so its count is the ready file's;
			// a stamp from before this run says nothing about it.
			const bool stamped = s.ran && s.when >= startedAt;
			const int added = stamped ? (s.added >= 0 ? s.added : s.cached) : 0;
			const int ready = stamped ? s.ready : OfflineAchievements::readyCount();
			// The title says RETROACHIEVEMENTS (OFFLINE); the line says only
			// what changed (D-UI-107, the maintainer's rule for every card).
			if (added == 1)
				action.push_back(_("1 MORE GAME IS READY."));
			else if (added > 1)
				action.push_back(Utils::String::format(_("%d MORE GAMES ARE READY.").c_str(), added));
			else if (ready == 1)
				action.push_back(_("1 GAME IS READY."));
			else if (ready > 1)
				action.push_back(Utils::String::format(_("%d GAMES ARE READY.").c_str(), ready));
			else
				action.push_back(_("EVERYTHING'S UP TO DATE."));
		}
		else
		{
			outcome = std::string(_("COULDN'T FINISH")) + " - " + OfflineAchievements::scanWhy(s.why);
			action.push_back(_("IT'LL TRY AGAIN NEXT TIME YOU'RE CONNECTED."));
		}
		card->updateTitle(TROPHY + _("RETROACHIEVEMENTS (OFFLINE)"));
		card->updateText(CloudText::outcomeCandidates(outcome), action);
		card->updatePercent(ok ? 100 : -1);
		LOG(LogInfo) << "ProxyCards: the top-up card ended rc " << rc << " cached " << s.cached << " added " << s.added << " ready " << s.ready
			<< " says added " << (ok && s.ran && s.when >= startedAt ? (s.added >= 0 ? s.added : s.cached) : 0)
			<< " ready " << (ok && s.ran && s.when >= startedAt ? s.ready : OfflineAchievements::readyCount());
		std::this_thread::sleep_for(std::chrono::milliseconds(5000));
		card->close();
	}

	// The one watcher (PL-056): runs what was asked for, and what is asked
	// for while it runs, then lets go. The index's run goes first when both
	// are waiting. A run asked for while a game has the screen waits for the
	// game to end rather than start under it: the ctl reads the network and
	// the store, and a player who chose STOP IT AND PLAY was told it would
	// try again later, not the moment the game started.
	void topUpWatcher(Window* window)
	{
		bool first = true;
		for (;;)
		{
			const int wanted = sTopUpWanted.exchange(0);
			if (wanted == 0)
			{
				sTopUpRunning = false;
				// A request that came between the exchange and the store found
				// the flag still up and left itself here: take it, unless a
				// watcher it started has taken the flag first.
				if (sTopUpWanted.load() == 0 || sTopUpRunning.exchange(true))
					return;
				continue;
			}
			if (!first)
				while (FileData::GetRunningGame() != nullptr)
					std::this_thread::sleep_for(std::chrono::seconds(1));
			first = false;
			if (wanted & 2)
				runTopUp(window, true);
			if (wanted & 1)
				runTopUp(window, false);
		}
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
			if (saves && savesOwed())
			{
				// Saves first (#305): the owed sync now, when the screen is free
				// of a game and another sync (it waits for those as the send
				// card would); the batch follows from its card's end.
				sBatchOwed = true;
				const auto started = std::chrono::steady_clock::now();
				while ((ThreadedCloudSync::isRunning() || FileData::GetRunningGame() != nullptr)
					&& std::chrono::steady_clock::now() - started < std::chrono::seconds(120))
					std::this_thread::sleep_for(std::chrono::seconds(1));
				window->postToUiThread([window] { startOwedSaves(window); });
				return;
			}
			if (saves)
			{
				// No saves owed: the batch at once -- this probe goes on as the
				// send's, and the top-up beside it.
				OfflineAchievements::topUpWhenOnline(window);
			}
			if (pending <= 0 && !flushed)
				return;
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
			window->postToUiThread([window]
			{
				if (FileData::GetRunningGame() != nullptr || ThreadedCloudSync::isRunning())
				{
					sSendRunning = false;   // the proxy sends anyway; the screen is taken
					return;
				}
				sSendShowing = true;
				AsyncNotificationComponent* card = window->createAsyncNotificationComponent(true);
				card->updateTitle(TROPHY + _("RETROACHIEVEMENTS"));
				card->updateText(_("STARTING..."));
				card->updatePercent(-1);
				std::thread(runSend, window, card).detach();
			});
		}).detach();
	}

	// The RetroAchievements batch of a link's return (#305): the send card
	// when the proxy holds awards or its stamp says a batch went, and the
	// top-up (the ctl decides whether it has anything to do). After the
	// owed saves when there were any, at once otherwise. Beside probe(),
	// which it calls: the first cut declared it in the file's anonymous
	// namespace and the link found no such function (x64 run 84).
	static void batch(Window* window)
	{
		LOG(LogInfo) << "ProxyCards: the link's RetroAchievements batch starts";
		probe(window, false);
		OfflineAchievements::topUpWhenOnline(window);
	}

	void linkReturned(Window* window)
	{
		if (!OfflineAchievements::available() || sSendRunning)
			return;
		probe(window, true);
	}

	void afterSync(Window* window)
	{
		if (!OfflineAchievements::available() || sSendRunning)
			return;
		if (sBatchOwed.exchange(false))
			batch(window);
		else
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
		sTopUpWanted |= afterIndex ? 2 : 1;
		if (sTopUpRunning.exchange(true))
			return;   // the watcher that holds the flag runs this request when its run ends
		std::thread(topUpWatcher, window).detach();
	}

	bool topUpRunning()
	{
		return OfflineAchievements::available() && OfflineAchievements::runningProgress().running;
	}

	bool stopTopUp()
	{
		return OfflineAchievements::stopRun();
	}

	void indexRanOffline(Window* window, int games)
	{
		if (!OfflineAchievements::available() || !OfflineAchievements::toggleOn())
			return;
		LOG(LogInfo) << "ProxyCards: the index ran offline with " << games << " game(s) to identify; the card says the new games are enabled once reconnected, and the ctl is told (index-offline)";
		offlineIndexCard(window);
		std::thread([] { OfflineAchievements::markIndexOffline(); }).detach();
	}
}
