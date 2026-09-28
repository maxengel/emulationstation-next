// The two long-job runners the transfer and scan pages sit in front of,
// compiled from their shipped sources against the doubles under fakes/:
// real processes, real process groups, real signals.
#include "doctest/doctest.h"
#include "CloudTransferJob.h"
#include "Log.h"
#include "ThreadedCloudSync.h"
#include "Window.h"
#include "utils/FileSystemUtil.h"

#include <atomic>
#include <chrono>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace
{
	bool waitFor(const std::function<bool()>& done, int seconds)
	{
		const auto start = std::chrono::steady_clock::now();
		while (!done())
		{
			if (std::chrono::steady_clock::now() - start > std::chrono::seconds(seconds))
				return false;
			std::this_thread::sleep_for(std::chrono::milliseconds(20));
		}
		return true;
	}

	bool logged(const std::string& needle)
	{
		std::lock_guard<std::mutex> g(FakeLog::lock());
		for (auto& l : FakeLog::lines())
			if (l.find(needle) != std::string::npos)
				return true;
		return false;
	}

	void clearLog()
	{
		std::lock_guard<std::mutex> g(FakeLog::lock());
		FakeLog::lines().clear();
	}

	std::atomic<int> sPosts{ 0 };
}

// ---- Double definitions ------------------------------------------------------

namespace FakeLog
{
	std::vector<std::string>& lines() { static std::vector<std::string> l; return l; }
	std::mutex& lock() { static std::mutex m; return m; }
}

void ThreadedCloudSync::writeStamp(const std::string&, int, const std::string&, const std::string&) {}
std::string ThreadedCloudSync::whyForCode(int) { return "SOMETHING WENT WRONG"; }

void Window::postToUiThread(const std::function<void()>& func, void*) { sPosts++; func(); }

std::string Utils::FileSystem::getFileName(const std::string& path)
{
	const size_t slash = path.find_last_of('/');
	return slash == std::string::npos ? path : path.substr(slash + 1);
}

// ---- Cases -------------------------------------------------------------------

// #308 5-cloud-sync-and-saves gpt F-CS-24 and claude F-CS-26: a stop that
// arrives before the run's ">>> pid" line used to set the flag and signal
// nothing, and nothing signalled it later -- the command ran to its end and
// the page then called it stopped. The stop is kept and sent when the pid
// arrives.
static void stopBeforeThePid(const std::string& who, const std::function<bool()>& stop)
{
	clearLog();
	const auto started = std::chrono::steady_clock::now();
	auto job = CloudTransferJob::start("sleep 4", "TEST", 1, 0);
	REQUIRE(job != nullptr);
	REQUIRE(stop());
	// The case is the seam: the stop found no pid yet. If the line had
	// already arrived this run proves nothing, so it does not pass.
	REQUIRE_MESSAGE(logged("group 0"), who << ": the pid had arrived before the stop; the case did not happen");

	REQUIRE(waitFor([&] { return job->finished(); }, 10));
	const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
	INFO(who << ": the run took " << ms << " ms");
	CHECK(ms < 2000);
	CloudTransferJob::dismiss(job);
}

TEST_CASE("transfer job: a cancel before the run says its pid reaches the run when it does")
{
	stopBeforeThePid("stopByPlayer", [] { return CloudTransferJob::stopByPlayer(); });
}

TEST_CASE("transfer job: a stop for a game before the run says its pid reaches the run when it does")
{
	stopBeforeThePid("stopForLaunch", [] { return CloudTransferJob::stopForLaunch(false); });
}

// The other half of gpt F-CS-24: a stop that finds the command's work done
// -- here a command that ignores SIGTERM and ends 0, the same as one whose
// last line was already written -- is not a stopped run. The command's own
// 0 says it completed, and the page words it COMPLETED.
TEST_CASE("transfer job: a run that completed is not called stopped because a stop came")
{
	clearLog();
	auto job = CloudTransferJob::start("trap '' TERM; sleep 1", "TEST", 1, 0);
	REQUIRE(job != nullptr);
	// Long enough for the pid line, so the stop is sent and not kept.
	std::this_thread::sleep_for(std::chrono::milliseconds(300));
	REQUIRE(CloudTransferJob::stopByPlayer());
	REQUIRE_FALSE(logged("group 0"));
	REQUIRE(waitFor([&] { return job->finished(); }, 10));
	CHECK_FALSE(job->stoppedByPlayer());
	CloudTransferJob::dismiss(job);
}
