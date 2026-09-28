// The save state manager's queued deletion and the cloud transfer lock
// (audit #307 PL-068, E2's half): the shipped SaveStateBookkeeper.cpp,
// compiled against doubles for the script runner and the log, with the
// transfer lock at a path of the test's own (CLOUD_SYNC_LOCK_PATH). A
// real lock, held by a real process, as a cloud_backup run holds it.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include "ApiSystem.h"
#include "Log.h"
#include "SaveStateBookkeeper.h"
#include "utils/FileSystemUtil.h"

#include <chrono>
#include <csignal>
#include <cstdio>
#include <fstream>
#include <mutex>
#include <string>
#include <sys/stat.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace
{
	std::mutex gRunLock;
	std::vector<std::string> gRuns;

	bool fileExists(const std::string& p) { struct stat st; return ::stat(p.c_str(), &st) == 0; }

	pid_t holdLock(const std::string& path)
	{
		const pid_t child = fork();
		if (child == 0)
		{
			execl("/bin/sh", "sh", "-c", "exec 9>\"$0\"; flock 9; touch \"$0.held\"; exec sleep 60", path.c_str(), (char*) nullptr);
			_exit(127);
		}
		for (int i = 0; i < 200 && !fileExists(path + ".held"); i++)
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		return child;
	}

	void release(pid_t p, const std::string& path)
	{
		kill(p, SIGKILL);
		waitpid(p, nullptr, 0);
		std::remove((path + ".held").c_str());
	}

	int runsNamed(const std::string& part)
	{
		std::lock_guard<std::mutex> g(gRunLock);
		int n = 0;
		for (auto& r : gRuns)
			if (r.find(part) != std::string::npos)
				n++;
		return n;
	}

	bool waitFor(const std::function<bool()>& done, int ms)
	{
		const auto start = std::chrono::steady_clock::now();
		while (!done())
		{
			if (std::chrono::steady_clock::now() - start > std::chrono::milliseconds(ms))
				return false;
			std::this_thread::sleep_for(std::chrono::milliseconds(20));
		}
		return true;
	}

	std::string stateFile(const std::string& name)
	{
		const std::string p = std::string(CLOUD_SYNC_LOCK_PATH) + "." + name + ".state1";
		std::ofstream(p) << "state";
		return p;
	}
}

namespace FakeLog
{
	std::vector<std::string>& lines() { static std::vector<std::string> l; return l; }
	std::mutex& lock() { static std::mutex m; return m; }
}

std::pair<std::string, int> ApiSystem::executeScriptLegacy(const std::string& command, const std::function<void(const std::string)>&)
{
	std::lock_guard<std::mutex> g(gRunLock);
	gRuns.push_back(command);
	return std::make_pair(std::string(), 0);
}

namespace Utils { namespace FileSystem {
	bool exists(const std::string& path, bool) { return path == "/usr/bin/cloud_capture" || fileExists(path); }
	std::string readAllText(const std::string&) { return ""; }
	bool removeFile(const std::string& path) { return std::remove(path.c_str()) == 0; }
}}

TEST_CASE("bookkeeper: a queued deletion waits while a transfer holds the lock")
{
	const std::string lock = CLOUD_SYNC_LOCK_PATH;
	const std::string state = stateFile("a");
	const pid_t holder = holdLock(lock);

	SaveStateBookkeeper::deleteLater(state, "");
	std::this_thread::sleep_for(std::chrono::milliseconds(1500));
	CHECK_MESSAGE(fileExists(state), "the state was deleted under a transfer");
	CHECK(runsNamed("--retire --unlink") == 0);
	CHECK(SaveStateBookkeeper::isPending(state));

	release(holder, lock);
	REQUIRE(waitFor([&state] { return !fileExists(state); }, 5000));
	CHECK(runsNamed("--retire --unlink") == 1);
	CHECK_FALSE(SaveStateBookkeeper::isPending(state));
}

TEST_CASE("bookkeeper: with no transfer, a deletion goes at once")
{
	const std::string state = stateFile("b");
	const auto before = runsNamed("--retire --unlink");
	SaveStateBookkeeper::deleteLater(state, "");
	REQUIRE(waitFor([&state] { return !fileExists(state); }, 2000));
	CHECK(runsNamed("--retire --unlink") == before + 1);
}

// A lock the bookkeeper cannot ask is not a free one (engineering-practices.md,
// guards fail closed): a deletion waits on a lock file it cannot open, as it
// waits on a held one, and goes once the file can be asked again.
TEST_CASE("bookkeeper: a lock file that cannot be opened is taken as held")
{
	const std::string lock = CLOUD_SYNC_LOCK_PATH;
	{ std::ofstream touch(lock, std::ios::app); }
	REQUIRE(::chmod(lock.c_str(), 0) == 0);
	REQUIRE_MESSAGE(::access(lock.c_str(), R_OK) != 0, "running as a user who can read a mode-0 file; the case cannot happen");
	const std::string state = stateFile("d");

	SaveStateBookkeeper::deleteLater(state, "");
	std::this_thread::sleep_for(std::chrono::milliseconds(1500));
	CHECK_MESSAGE(fileExists(state), "the state was deleted past a lock nobody could ask");

	REQUIRE(::chmod(lock.c_str(), 0644) == 0);
	REQUIRE(waitFor([&state] { return !fileExists(state); }, 5000));
}

TEST_CASE("bookkeeper: exit does not wait out a transfer, and deletes nothing under it")
{
	const std::string lock = CLOUD_SYNC_LOCK_PATH;
	const std::string state = stateFile("c");
	const pid_t holder = holdLock(lock);
	SaveStateBookkeeper::deleteLater(state, "");
	std::this_thread::sleep_for(std::chrono::milliseconds(300));
	const auto start = std::chrono::steady_clock::now();
	SaveStateBookkeeper::shutdown();
	const auto took = std::chrono::steady_clock::now() - start;
	CHECK(took < std::chrono::seconds(10));
	CHECK_MESSAGE(fileExists(state), "the state was deleted under a transfer at exit");
	release(holder, lock);
	std::remove(state.c_str());
}
