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

// ------------------------------------------------------------ G2-E-core-06

TEST_CASE("a reload that reads nothing keeps every change still waiting to be saved (audit of the fix round, gpt G2-E-core-06)")
{
	// The reload cleared the pending changes, read nothing, and then
	// compared them with the empty reading: every change to a key the file
	// held looked like somebody had removed the key since, and was dropped
	// -- from the pending set and from memory -- before the false came back.
	// Nothing read is no evidence about any key.
	ScratchDir dir;
	const std::string path = dir / "system.cfg";
	const std::string lockPath = dir / ".system.cfg.lock";
	put(path, "system.hostname=A\naudio.volume=70\n");
	SystemConf* conf = SystemConfTestAccess::fresh(path, lockPath, 300);
	REQUIRE(conf->get("audio.volume") == "70");

	// The player's change; the save is refused for the lock (PL-024) and kept.
	CHECK(conf->set("audio.volume", "40"));
	{
		PidLock other(lockPath);
		REQUIRE(other.acquire(1000));
		CHECK_FALSE(conf->saveSystemConf());
	}

	// The file and its record gone for the moment: the reload reads nothing.
	REQUIRE(std::rename(path.c_str(), (dir / "aside").c_str()) == 0);
	REQUIRE(std::remove((path + ".backup").c_str()) == 0);
	CHECK_FALSE(conf->loadSystemConf(true));
	CHECK(conf->get("audio.volume") == "40");

	// The file back: the next save makes the change.
	REQUIRE(std::rename((dir / "aside").c_str(), path.c_str()) == 0);
	CHECK(conf->saveSystemConf());
	CHECK(get(path) == "system.hostname=A\naudio.volume=40\n");
}

// ------------------------------------------------------------ G2-E-core-07

TEST_CASE("a change is compared with the file it was made against, not with memory (audit of the fix round, gpt G2-E-core-07, claude G2-E-core-09)")
{
	// set() took a change's base from confMap, which also holds keys the
	// file no longer has -- a key another writer removed since the last
	// load, a key the last save wrote away (an empty value is a removed
	// line). A reload compared that base with the file, found the key
	// missing where the base said it was there, and dropped the change as
	// removed by somebody since, when nobody had touched the file.
	ScratchDir dir;
	const std::string path = dir / "system.cfg";
	const std::string lockPath = dir / ".system.cfg.lock";

	SUBCASE("a key another writer removed before the change was made")
	{
		put(path, "system.hostname=A\ntone=old\nkeep=1\n");
		SystemConf* conf = SystemConfTestAccess::fresh(path, lockPath, 300);
		put(path, "system.hostname=A\nkeep=1\n");   // a script removed tone
		REQUIRE(conf->loadSystemConf(true));

		CHECK(conf->set("tone", "new"));
		{
			PidLock other(lockPath);
			REQUIRE(other.acquire(1000));
			CHECK_FALSE(conf->saveSystemConf());
		}
		REQUIRE(conf->loadSystemConf(true));   // the file as it was when the change was made
		CHECK(conf->get("tone") == "new");
		CHECK(conf->saveSystemConf());
		CHECK(get(path) == "system.hostname=A\nkeep=1\ntone=new\n");
	}
	SUBCASE("a key the last save wrote away")
	{
		put(path, "system.hostname=A\na=1\n");
		SystemConf* conf = SystemConfTestAccess::fresh(path, lockPath, 300);
		CHECK(conf->set("a", ""));
		REQUIRE(conf->saveSystemConf());
		REQUIRE(get(path) == "system.hostname=A\n");

		CHECK(conf->set("a", "3"));
		{
			PidLock other(lockPath);
			REQUIRE(other.acquire(1000));
			CHECK_FALSE(conf->saveSystemConf());
		}
		REQUIRE(conf->loadSystemConf(true));
		CHECK(conf->get("a") == "3");
		CHECK(conf->saveSystemConf());
		CHECK(get(path) == "system.hostname=A\na=3\n");
	}
	SUBCASE("a key somebody else really did change is still theirs")
	{
		put(path, "system.hostname=A\nwifi.ssid=Home\n");
		SystemConf* conf = SystemConfTestAccess::fresh(path, lockPath, 300);
		CHECK(conf->set("wifi.ssid", "Cafe"));
		{
			PidLock other(lockPath);
			REQUIRE(other.acquire(1000));
			CHECK_FALSE(conf->saveSystemConf());
		}
		put(path, "system.hostname=A\nwifi.ssid=Library\n");   // wifictl join wrote it
		REQUIRE(conf->loadSystemConf(true));
		CHECK(conf->get("wifi.ssid") == "Library");
	}
}
