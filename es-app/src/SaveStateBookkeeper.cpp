#include "SaveStateBookkeeper.h"
#include "SaveStateJobQueue.h"
#include "ApiSystem.h"
#include "Log.h"
#include "utils/FileSystemUtil.h"
#include "utils/StringUtil.h"

#include <condition_variable>
#include <mutex>
#include <thread>

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

	void runDelete(const SaveStateJob& job)
	{
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
		if (code != 0)
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
				runDelete(job);
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
