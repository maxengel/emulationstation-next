#include "SaveStateBookkeeper.h"
#include "SaveStateJobQueue.h"
#include "ApiSystem.h"
#include "Log.h"
#include "RunLock.h"
#include "utils/FileSystemUtil.h"
#include "utils/StringUtil.h"

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

// The cloud scripts' transfer lock. A macro so a test can hold one of its
// own (tests/app-unit, bookkeeper-tests); the device's is this one.
#ifndef CLOUD_SYNC_LOCK_PATH
#define CLOUD_SYNC_LOCK_PATH "/var/run/cloud_sync.lock"
#endif

namespace
{
	const char* const CAPTURE = "/usr/bin/cloud_capture";

	// The one worker and its order book. Made on first use and never freed:
	// a static std::thread would be destroyed at static teardown, before the
	// atexit hook that joins it, and a joinable thread's destructor ends the
	// process (D-UI-073, third rule). shutdown() joins by hand instead.
	struct Worker
	{
		std::mutex mutex;
		std::condition_variable wake;
		SaveStateJobQueue queue;
		std::thread thread;
		bool stop = false;
	};

	std::mutex gWorkerMutex;
	Worker* gWorker = nullptr;

	// A cloud transfer -- a back up, a restore, a sync, started here or from
	// a shell -- holds the scripts' lock while it runs, and a deletion made
	// under it retires and unlinks a state the transfer may be copying, in a
	// manifest the transfer may be reading (audit #307 PL-068). The manager
	// refuses a DELETE while the lock is held; a deletion already queued
	// when a transfer starts waits here for it to end, polled twice a
	// second, however long it takes -- the tile stays hidden meanwhile
	// (isPending). At exit it waits five seconds more and no longer: a
	// deletion still waiting then is not made, the file stays, and its tile
	// is back at the next start, which is honest where deleting under the
	// transfer would not be. True when the deletion may go ahead.
	bool transferGone(Worker* w, const SaveStateJob& job)
	{
		if (!RunLock::held(CLOUD_SYNC_LOCK_PATH))
			return true;
		LOG(LogInfo) << "save state deletion waits: a cloud transfer holds the lock (" << job.stateFile << ")";
		bool stopping = false;
		std::chrono::steady_clock::time_point giveUpAt;
		for (;;)
		{
			bool stop;
			{
				std::unique_lock<std::mutex> lock(w->mutex);
				if (!w->stop)
					w->wake.wait_for(lock, std::chrono::milliseconds(500), [w] { return w->stop; });
				stop = w->stop;
			}
			if (!RunLock::held(CLOUD_SYNC_LOCK_PATH))
			{
				LOG(LogInfo) << "save state deletion goes ahead: the cloud transfer has ended (" << job.stateFile << ")";
				return true;
			}
			if (!stop)
				continue;
			if (!stopping)
			{
				stopping = true;
				giveUpAt = std::chrono::steady_clock::now() + std::chrono::seconds(5);
			}
			else if (std::chrono::steady_clock::now() >= giveUpAt)
			{
				LOG(LogWarning) << "save state deletion not made at exit: a cloud transfer still held the lock; " << job.stateFile << " stays";
				return false;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(200));
		}
	}

	void runDelete(Worker* w, const SaveStateJob& job)
	{
		if (!transferGone(w, job))
			return;

		// Record the deletion before the file goes (#21 R3, D-CLOUD-053): the
		// next pass then propagates a decided deletion instead of asking about
		// an absence it cannot explain (D-CLOUD-037). With --unlink the script
		// takes the files too, once the row is on disk, and it ignores SIGTERM
		// while it runs, so the two cannot be split by a stop (D-CLOUD-133).
		if (Utils::FileSystem::exists(CAPTURE))
		{
			std::string retire = std::string(CAPTURE) + " --retire --unlink " + Utils::String::shellQuote(job.stateFile);
			if (!job.screenshot.empty())
				retire += " " + Utils::String::shellQuote(job.screenshot);

			int code = ApiSystem::executeScriptLegacy(retire, nullptr).second;
			if (code != 0)
				LOG(LogWarning) << "cloud_capture --retire --unlink exited " << code << " -- see /var/log/cloud_sync.log and /storage/.cache/cloud_sync/capture-failures";
		}

		// Whatever the script did or did not do -- absent on an image without
		// it, a usage error, a card gone -- the deletion the player asked for
		// proceeds: anything still there goes now.
		if (Utils::FileSystem::exists(job.stateFile, false))
			Utils::FileSystem::removeFile(job.stateFile);
		if (!job.screenshot.empty() && Utils::FileSystem::exists(job.screenshot, false))
			Utils::FileSystem::removeFile(job.screenshot);

		// The line says what is on disk, not what was asked: a file the
		// unlink was refused (immutable, a card gone read-only) is still
		// there, and the manager's compare (#207) puts its tile back.
		if (Utils::FileSystem::exists(job.stateFile, false))
			LOG(LogWarning) << "save state deletion did not take: " << job.stateFile << " is still present; the manager shows it again";
		else
			LOG(LogInfo) << "save state deleted: " << job.stateFile;
	}

	void runCopy(const SaveStateJob& job)
	{
		// A slot the manager copied had no entry from any capture mode (#206):
		// exit mode records only files written after the launch, and the
		// verify passes adopt nothing new. --adopt records it as the source's
		// version at the new path, or as a version of unknown provenance when
		// the source itself had no entry.
		if (!Utils::FileSystem::exists(CAPTURE))
			return;

		const std::string adopt = std::string(CAPTURE) + " --adopt " + Utils::String::shellQuote(job.stateFile)
			+ " --from " + Utils::String::shellQuote(job.source)
			+ " --system " + Utils::String::shellQuote(job.system)
			+ " --rom " + Utils::String::shellQuote(job.rom);
		int code = ApiSystem::executeScriptLegacy(adopt, nullptr).second;
		// 3 is the script's "not a state of this unit; nothing recorded"
		// (cloud_capture's header) -- it used to exit 0 there and this line
		// logged a recording that had not happened (audit #258 PL-016).
		if (code == 3)
			LOG(LogWarning) << "cloud_capture --adopt recorded nothing: " << job.stateFile << " is not a state of this game on disk";
		else if (code != 0)
			LOG(LogWarning) << "cloud_capture --adopt exited " << code << " -- see /var/log/cloud_sync.log and /storage/.cache/cloud_sync/capture-failures";
		else
			LOG(LogInfo) << "save state copy recorded: " << job.stateFile;
	}

	void loop(Worker* w)
	{
		for (;;)
		{
			SaveStateJob job;
			{
				std::unique_lock<std::mutex> lock(w->mutex);
				w->wake.wait(lock, [w] { return w->stop || w->queue.queued() > 0; });
				if (!w->queue.take(job))
				{
					// Nothing queued: leave only when told to. A stop with
					// jobs still queued drains them first -- each is
					// something the player asked for.
					if (w->stop)
						return;
					continue;
				}
			}

			if (job.kind == SaveStateJob::Kind::Delete)
				runDelete(w, job);
			else
				runCopy(job);

			{
				std::lock_guard<std::mutex> lock(w->mutex);
				w->queue.finish();
			}
		}
	}

	Worker* worker(bool create)
	{
		std::lock_guard<std::mutex> lock(gWorkerMutex);
		if (gWorker == nullptr && create)
		{
			gWorker = new Worker();
			gWorker->thread = std::thread(loop, gWorker);
		}
		return gWorker;
	}
}

void SaveStateBookkeeper::deleteLater(const std::string& stateFile, const std::string& screenshot)
{
	Worker* w = worker(true);
	{
		std::lock_guard<std::mutex> lock(w->mutex);
		if (!w->queue.enqueueDelete(stateFile, screenshot))
			return;
	}
	LOG(LogInfo) << "save state deletion queued: " << stateFile;
	w->wake.notify_one();
}

void SaveStateBookkeeper::recordCopy(const std::string& stateFile, const std::string& source,
	const std::string& system, const std::string& rom)
{
	Worker* w = worker(true);
	{
		std::lock_guard<std::mutex> lock(w->mutex);
		if (!w->queue.enqueueCopy(stateFile, source, system, rom))
			return;
	}
	LOG(LogInfo) << "save state copy queued for the record: " << stateFile;
	w->wake.notify_one();
}

bool SaveStateBookkeeper::isPending(const std::string& stateFile)
{
	Worker* w = worker(false);
	if (w == nullptr)
		return false;
	std::lock_guard<std::mutex> lock(w->mutex);
	return w->queue.isPending(stateFile);
}

unsigned SaveStateBookkeeper::completed()
{
	Worker* w = worker(false);
	if (w == nullptr)
		return 0;
	std::lock_guard<std::mutex> lock(w->mutex);
	return w->queue.completed();
}

void SaveStateBookkeeper::shutdown()
{
	Worker* w = worker(false);
	if (w == nullptr)
		return;
	{
		std::lock_guard<std::mutex> lock(w->mutex);
		w->stop = true;
	}
	w->wake.notify_all();
	if (w->thread.joinable())
		w->thread.join();
}
