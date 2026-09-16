#include "SaveStateDeleter.h"
#include "SaveStateDeleteQueue.h"
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
		SaveStateDeleteQueue queue;
		std::thread thread;
		bool stop = false;
	};

	std::mutex gWorkerMutex;
	Worker* gWorker = nullptr;

	// One deletion, in D-CLOUD-053's order.
	void runOne(const SaveStateDeleteJob& job)
	{
		// Record the deletion before the file goes (#21 R3): the next pass
		// then propagates a decided deletion instead of asking about an
		// absence it cannot explain (D-CLOUD-037). The deletion proceeds
		// whatever the script says -- the player asked for it -- and a retire
		// that could not record is logged, as launchGame logs a capture that
		// could not, so the absence has a trace somewhere.
		if (Utils::FileSystem::exists(CAPTURE))
		{
			std::string retire = std::string(CAPTURE) + " --retire " + Utils::String::shellQuote(job.stateFile);
			if (!job.screenshot.empty())
				retire += " " + Utils::String::shellQuote(job.screenshot);

			int code = ApiSystem::executeScriptLegacy(retire, nullptr).second;
			if (code != 0)
				LOG(LogWarning) << "cloud_capture --retire exited " << code << " -- see /var/log/cloud_sync.log and /storage/.cache/cloud_sync/capture-failures";
		}

		Utils::FileSystem::removeFile(job.stateFile);
		if (!job.screenshot.empty())
			Utils::FileSystem::removeFile(job.screenshot);

		// No --rescan here (D-CLOUD-132): it re-keyed the slots a renumber had
		// moved, and since D-UI-069 nothing moves -- the retire above is the
		// whole record of this deletion.
		LOG(LogInfo) << "save state deleted: " << job.stateFile;
	}

	void loop(Worker* w)
	{
		for (;;)
		{
			SaveStateDeleteJob job;
			{
				std::unique_lock<std::mutex> lock(w->mutex);
				w->wake.wait(lock, [w] { return w->stop || w->queue.queued() > 0; });
				if (!w->queue.take(job))
				{
					// Nothing queued: leave only when told to. A stop with
					// jobs still queued drains them first -- each is a
					// deletion the player asked for.
					if (w->stop)
						return;
					continue;
				}
			}

			runOne(job);

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

void SaveStateDeleter::enqueue(const std::string& stateFile, const std::string& screenshot)
{
	Worker* w = worker(true);
	{
		std::lock_guard<std::mutex> lock(w->mutex);
		if (!w->queue.enqueue(stateFile, screenshot))
			return;
	}
	LOG(LogInfo) << "save state deletion queued: " << stateFile;
	w->wake.notify_one();
}

bool SaveStateDeleter::isPending(const std::string& stateFile)
{
	Worker* w = worker(false);
	if (w == nullptr)
		return false;
	std::lock_guard<std::mutex> lock(w->mutex);
	return w->queue.isPending(stateFile);
}

unsigned SaveStateDeleter::completed()
{
	Worker* w = worker(false);
	if (w == nullptr)
		return 0;
	std::lock_guard<std::mutex> lock(w->mutex);
	return w->queue.completed();
}

void SaveStateDeleter::shutdown()
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
