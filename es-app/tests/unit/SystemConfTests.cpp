// SystemConf itself -- the load, the reload, the save and the record --
// against real files in a scratch directory (the audit of the fix round).
//
// es-file-tests checks AtomicFileUtil's pieces one at a time; the defects
// the fix round's audit found were in how SystemConf strings them together:
// what a reload does when nothing could be read, which base a change is
// compared with, the mode a recovery's record gets, whether the recovery's
// write-back waits for the settings lock. So this binary compiles the
// shipped SystemConf.cpp and AtomicFileUtil.cpp, with the few things they
// call from the rest of the interface -- the log, Settings, Paths, SDL's
// clock -- defined here as doubles. A fresh SystemConf per case, through
// SystemConfTestAccess (a friend SystemConf.h names), pointed at a scratch
// system.cfg and a scratch settings lock. POSIX only, like the code.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include "Log.h"
#include "Paths.h"
#include "Settings.h"
#include "SystemConf.h"
#include "utils/AtomicFileUtil.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

// ------------------------------------------------------------ the doubles

LogLevel Log::mReportingLevel = LogDebug;
bool Log::mEnabled = true;

namespace
{
	std::mutex gLogMutex;
	std::string gLog;   // every line SystemConf logged, for a case to read
	thread_local std::ostringstream tlLine;
}

Log::Log(LogLevel level) : mLevel(level)
{
	tlLine.str("");
	tlLine.clear();
}

Log::~Log()
{
	std::lock_guard<std::mutex> lock(gLogMutex);
	gLog += tlLine.str() + "\n";
}

std::ostringstream& Log::stream()
{
	return tlLine;
}

Paths* Paths::_instance = nullptr;
Paths::Paths() {}

// SystemConf goes to Settings only when it has no system.cfg path, which no
// case here gives it.
Settings* Settings::getInstance() { return nullptr; }
bool Settings::saveFile() { return false; }
std::string Settings::getString(const std::string&) { return ""; }
bool Settings::setString(const std::string&, const std::string&) { return false; }
bool Settings::getBool(const std::string&) { return false; }
bool Settings::setBool(const std::string&, bool) { return false; }

struct SystemConfTestAccess
{
	// A new SystemConf, reading `path` as the constructor does, with the
	// settings lock at `lockPath` and a budget of `budgetMs`.
	static SystemConf* fresh(const std::string& path, const std::string& lockPath, int budgetMs)
	{
		delete SystemConf::sInstance;
		SystemConf::sInstance = nullptr;
		SystemConf::sRecovered = false;
		SystemConf::sLockPath = lockPath;
		SystemConf::sLockBudgetMs = budgetMs;
		Paths::getSystemConfFilePath() = path;
		{
			std::lock_guard<std::mutex> lock(gLogMutex);
			gLog.clear();
		}
		return SystemConf::getInstance();
	}

	static bool recovered() { return SystemConf::sRecovered; }
};

namespace
{
	struct ScratchDir
	{
		std::string path;
		ScratchDir()
		{
			const char* base = getenv("TMPDIR");
			std::string tmpl = std::string(base != nullptr && *base != '\0' ? base : "/tmp") + "/es-conf-tests.XXXXXX";
			std::vector<char> buf(tmpl.begin(), tmpl.end());
			buf.push_back('\0');
			REQUIRE(mkdtemp(buf.data()) != nullptr);
			path = buf.data();
		}
		~ScratchDir()
		{
			const std::string cmd = "rm -rf '" + path + "'";
			if (std::system(cmd.c_str()) != 0)
				fprintf(stderr, "could not remove %s\n", path.c_str());
		}
		std::string operator/(const std::string& name) const { return path + "/" + name; }
	};

	void put(const std::string& path, const std::string& text)
	{
		std::ofstream out(path, std::ios::binary | std::ios::trunc);
		out << text;
	}

	std::string get(const std::string& path)
	{
		std::ifstream in(path, std::ios::binary);
		return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	}

	int modeOf(const std::string& path)
	{
		struct stat st;
		return ::stat(path.c_str(), &st) == 0 ? (int) (st.st_mode & 07777) : -1;
	}

	std::string logged()
	{
		std::lock_guard<std::mutex> lock(gLogMutex);
		return gLog;
	}
}

using Utils::AtomicFile::PidLock;

// ------------------------------------------------------------ the harness itself

TEST_CASE("the harness: a load, a change and a save, as the interface makes them")
{
	ScratchDir dir;
	const std::string path = dir / "system.cfg";
	put(path, "system.hostname=A\naudio.volume=70\n");

	SystemConf* conf = SystemConfTestAccess::fresh(path, dir / ".system.cfg.lock", 500);
	CHECK(conf->get("audio.volume") == "70");
	CHECK(conf->set("audio.volume", "40"));
	CHECK(conf->saveSystemConf());
	CHECK(get(path) == "system.hostname=A\naudio.volume=40\n");
	// A save onto a whole file is the record.
	CHECK(get(path + ".backup") == "system.hostname=A\naudio.volume=40\n");
	CHECK_FALSE(SystemConfTestAccess::recovered());
}
