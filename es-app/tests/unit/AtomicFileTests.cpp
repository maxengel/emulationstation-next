// The settings files' writer, reader and lock, against real files in a
// scratch directory (audit #307: PL-024, PL-041, PL-063, PL-064, PL-065;
// #308 F-ES-07, F-ES-08).
//
// A binary of its own, es-file-tests, because es-unit-tests is the pure
// code and touches no file (README.md). This one touches nothing but a
// directory it makes under $TMPDIR (or /tmp) and removes again, and the
// processes it forks to play the other writer -- the shell's set_setting,
// a second waiter on a stale lock. POSIX only, like the code it checks.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include "utils/AtomicFileUtil.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace
{
	// A directory of its own per case, removed with everything in it.
	struct ScratchDir
	{
		std::string path;
		ScratchDir()
		{
			const char* base = getenv("TMPDIR");
			std::string tmpl = std::string(base != nullptr && *base != '\0' ? base : "/tmp") + "/es-file-tests.XXXXXX";
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

	// Several lines of one letter: big enough that a write takes more than
	// one system call and two writers overlap, small enough to run quickly.
	std::string lines(char c, size_t count)
	{
		std::string line(99, c);
		line += '\n';
		std::string out;
		out.reserve(line.size() * count);
		for (size_t i = 0; i < count; i++)
			out += line;
		return out;
	}

	// A pid nobody holds: a child that has exited and been reaped.
	pid_t deadPid()
	{
		pid_t pid = fork();
		if (pid == 0)
			_exit(0);
		int status = 0;
		waitpid(pid, &status, 0);
		return pid;
	}

	// The line that starts with `key` replaced by `line`, the rest as it was:
	// the merge a save makes of one changed key.
	std::string withLine(const std::string& text, const std::string& key, const std::string& line)
	{
		std::string out;
		size_t pos = 0;
		while (pos < text.size())
		{
			size_t end = text.find('\n', pos);
			const std::string current = text.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
			out += (current.rfind(key, 0) == 0 ? line : current) + "\n";
			if (end == std::string::npos)
				break;
			pos = end + 1;
		}
		return out;
	}

	// Run fn in a child with a deadline; the child's exit code, or -1 when
	// the deadline killed it (a loop that never returns).
	template <typename F>
	int inChildWithin(int seconds, F fn)
	{
		pid_t pid = fork();
		if (pid == 0)
		{
			alarm((unsigned) seconds);
			_exit(fn());
		}
		int status = 0;
		waitpid(pid, &status, 0);
		if (WIFEXITED(status))
			return WEXITSTATUS(status);
		return -1;
	}
}

using namespace Utils::AtomicFile;

// ------------------------------------------------------------------ PL-063

TEST_CASE("two writers at once leave one whole file, never a mix of both (PL-063)")
{
	// Every writer shared path.tmp under O_TRUNC: the second truncated the
	// first's temporary under it, the first renamed the shared file into
	// place -- empty, or half one writer's bytes -- and the second's rename
	// then failed on a name that was gone, so a save that should have landed
	// reported failure. Two threads here; the shell's set_setting (awk into
	// system.cfg.tmp) was the same name from another process.
	ScratchDir dir;
	const std::string path = dir / "system.cfg";
	const std::string start = "start\n";
	const std::string a = lines('a', 200);
	const std::string b = lines('b', 200);
	put(path, start);

	std::atomic<bool> stop(false);
	std::atomic<int> torn(0);
	std::thread reader([&] {
		while (!stop)
		{
			// Never absent, never empty, never a mix: the rename is atomic,
			// so the name always holds one whole file.
			const std::string seen = get(path);
			if (seen != start && seen != a && seen != b)
				torn++;
		}
	});

	std::atomic<int> failed(0);
	auto writer = [&](const std::string& text) {
		for (int i = 0; i < 400; i++)
			if (!writeText(path, text))
				failed++;
	};
	std::thread wa(writer, a);
	std::thread wb(writer, b);
	wa.join();
	wb.join();
	stop = true;
	reader.join();

	const std::string last = get(path);
	CHECK((last == a || last == b));
	CHECK(torn.load() == 0);
	CHECK(failed.load() == 0);
	// Nothing left behind: every temporary was renamed or removed.
	CHECK(std::system(("test -z \"$(ls -A '" + dir.path + "' | grep -v '^system.cfg$')\"").c_str()) == 0);
}

// ------------------------------------------------------------------ PL-065

TEST_CASE("a read that fails after the file opened is not a read (PL-065)")
{
	// readText said ok once the stream opened, whatever the read did, and
	// SystemConf took the prefix it got as the whole file -- and recorded it
	// as the last known good. A directory opens for reading on Linux and
	// then fails with EISDIR; /proc/self/mem opens and fails with EIO at
	// offset 0. Neither is a file's text.
	ScratchDir dir;
	bool ok = true;
	readText(dir.path, &ok);
	CHECK_FALSE(ok);

	ok = true;
	readText("/proc/self/mem", &ok);
	CHECK_FALSE(ok);

	// And the two answers that were always right stay right.
	put(dir / "f", "a=1\n");
	ok = false;
	CHECK(readText(dir / "f", &ok) == "a=1\n");
	CHECK(ok);
	ok = true;
	CHECK(readText(dir / "missing", &ok).empty());
	CHECK_FALSE(ok);
	ok = false;
	put(dir / "empty", "");
	CHECK(readText(dir / "empty", &ok).empty());
	CHECK(ok);   // empty and missing are different answers
}

// ------------------------------------------------------------ F-ES-08

TEST_CASE("replacing a file keeps its mode (#308 8b gpt F-ES-08)")
{
	// The temporary was created 0644 and renamed over the file, so a
	// system.cfg somebody had made 0600 -- it carries wifi.key and the
	// RetroAchievements password -- came back 0644 from the next save.
	ScratchDir dir;
	const mode_t before = umask(022);
	const std::string path = dir / "system.cfg";
	put(path, "wifi.key=x\n");
	REQUIRE(chmod(path.c_str(), 0600) == 0);

	CHECK(writeText(path, "wifi.key=y\n"));
	struct stat st;
	REQUIRE(stat(path.c_str(), &st) == 0);
	CHECK((st.st_mode & 0777) == 0600);
	CHECK(get(path) == "wifi.key=y\n");

	// A file that did not exist is made as it always was: 0644.
	const std::string fresh = dir / "fresh.cfg";
	CHECK(writeText(fresh, "a=1\n"));
	REQUIRE(stat(fresh.c_str(), &st) == 0);
	CHECK((st.st_mode & 0777) == 0644);

	// And the last-known-good record beside a private file is as private
	// as the file, the first time it is made and every time after -- one an
	// earlier build left 0644 included.
	const std::string record = dir / "system.cfg.backup";
	CHECK(writeText(record, "wifi.key=y\n", modeOf(path, 0644)));
	REQUIRE(stat(record.c_str(), &st) == 0);
	CHECK((st.st_mode & 0777) == 0600);
	REQUIRE(chmod(record.c_str(), 0644) == 0);
	CHECK(writeText(record, "wifi.key=y\n", modeOf(path, 0644)));
	REQUIRE(stat(record.c_str(), &st) == 0);
	CHECK((st.st_mode & 0777) == 0600);
	CHECK(copy(path, dir / "copied.cfg"));
	REQUIRE(stat((dir / "copied.cfg").c_str(), &st) == 0);
	CHECK((st.st_mode & 0777) == 0600);
	CHECK(modeOf(dir / "missing", 0640) == 0640);
	umask(before);
}

// ------------------------------------------------------------------ PL-041

TEST_CASE("the lock carries its holder's pid from the moment it exists (PL-041)")
{
	// The lock was created empty (O_EXCL) and the pid written after, and the
	// write's result was thrown away. A waiter that read it in between saw
	// an empty lock -- which both sides treat as nobody's, and remove. A
	// reader spinning beside a thousand acquire/release pairs must never
	// see the file there and empty.
	ScratchDir dir;
	const std::string path = dir / ".system.cfg.lock";
	std::atomic<bool> stop(false);
	std::atomic<int> emptySeen(0);
	std::thread reader([&] {
		while (!stop)
		{
			int fd = ::open(path.c_str(), O_RDONLY);
			if (fd < 0)
				continue;
			char buf[32];
			const ssize_t n = ::read(fd, buf, sizeof(buf));
			::close(fd);
			if (n == 0)
				emptySeen++;
		}
	});
	for (int i = 0; i < 3000; i++)
	{
		PidLock lock(path);
		REQUIRE(lock.acquire(2000));
		lock.release();
	}
	stop = true;
	reader.join();
	CHECK(emptySeen.load() == 0);
}

TEST_CASE("two waiters on a stale lock never both hold it (PL-041)")
{
	// Both read the dead holder's pid, both decide the lock is stale; the
	// first removes it and takes it, and the second's remove -- two steps
	// after its read -- could take the first one's new lock with it. Forked
	// waiters, released together at a stale lock, each marking the inside of
	// its hold with an O_EXCL file: a second mark while one is there is two
	// holders.
	ScratchDir dir;
	const std::string lockPath = dir / ".system.cfg.lock";
	const std::string inside = dir / "inside";
	int overlaps = 0;
	int failures = 0;

	for (int round = 0; round < 200; round++)
	{
		put(lockPath, std::to_string((long long) deadPid()) + "\n");
		int gate[2];
		REQUIRE(pipe(gate) == 0);
		std::vector<pid_t> waiters;
		for (int w = 0; w < 3; w++)
		{
			pid_t pid = fork();
			if (pid == 0)
			{
				::close(gate[1]);
				char c;
				if (::read(gate[0], &c, 1) < 0)
					_exit(4);
				PidLock lock(lockPath);
				if (!lock.acquire(3000))
					_exit(2);
				int fd = ::open(inside.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0644);
				if (fd < 0)
					_exit(3);
				::close(fd);
				usleep(1000);
				::unlink(inside.c_str());
				lock.release();
				_exit(0);
			}
			waiters.push_back(pid);
		}
		::close(gate[0]);
		::close(gate[1]);   // everyone goes at once
		for (pid_t pid : waiters)
		{
			int status = 0;
			waitpid(pid, &status, 0);
			if (WIFEXITED(status) && WEXITSTATUS(status) == 3)
				overlaps++;
			else if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
				failures++;
		}
		::unlink(inside.c_str());
		::unlink(lockPath.c_str());
	}
	CHECK(overlaps == 0);
	CHECK(failures == 0);
}

// ------------------------------------------------------------ F-ES-07

TEST_CASE("a lock path that can never be a lock ends the wait within its budget (#308 8b gpt F-ES-07)")
{
	// A directory where the lock goes: the create fails with EEXIST, the
	// read fails, and the loop retried at once without looking at the clock
	// -- forever, on the interface thread. It must answer false in its
	// budget and leave the directory alone.
	ScratchDir dir;
	const std::string lockPath = dir / ".system.cfg.lock";
	REQUIRE(mkdir(lockPath.c_str(), 0755) == 0);

	const auto started = std::chrono::steady_clock::now();
	const int rc = inChildWithin(5, [&] {
		PidLock lock(lockPath);
		return lock.acquire(300) ? 1 : 0;
	});
	const long ms = (long) std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
	CHECK(rc == 0);          // -1 is the deadline killing a loop that never returned
	CHECK(ms < 3000);
	struct stat st;
	REQUIRE(stat(lockPath.c_str(), &st) == 0);
	CHECK(S_ISDIR(st.st_mode));
}

// ------------------------------------------------------------------ PL-024

TEST_CASE("a save that cannot get the lock writes nothing, and both writers' keys survive (PL-024)")
{
	// The interface waited five seconds for the settings lock and then saved
	// anyway. The shell's set_setting holds it across a read, a write of
	// system.cfg.tmp and a rename -- so a save made beside it put one
	// writer's snapshot over the other's: the interface's key, or the
	// script's, was gone. The other writer here is that shape, stuck for
	// longer than the interface waits; the interface's save must write
	// nothing, keep its change, and make it once the lock is free.
	ScratchDir dir;
	const std::string path = dir / "system.cfg";
	const std::string lockPath = dir / ".system.cfg.lock";
	put(path, "a=1\nb=1\n");

	int ready[2];
	REQUIRE(pipe(ready) == 0);
	pid_t other = fork();
	if (other == 0)
	{
		::close(ready[0]);
		// wait_lock's shape: noclobber create, then the pid.
		int fd = ::open(lockPath.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0644);
		if (fd < 0)
			_exit(2);
		const std::string pid = std::to_string((long long) getpid()) + "\n";
		if (::write(fd, pid.data(), pid.size()) != (ssize_t) pid.size())
			_exit(2);
		::close(fd);
		// write_setting_line's: read, write the temporary, rename.
		put(path + ".tmp", withLine(get(path), "b=", "b=2"));
		if (::write(ready[1], "x", 1) != 1)
			_exit(2);
		usleep(800 * 1000);
		if (::rename((path + ".tmp").c_str(), path.c_str()) != 0)
			_exit(3);
		::unlink(lockPath.c_str());
		_exit(0);
	}
	::close(ready[1]);
	char c;
	REQUIRE(::read(ready[0], &c, 1) == 1);
	::close(ready[0]);

	auto setA = [](const std::string& current) { return withLine(current, "a=", "a=2"); };
	const LockedSave first = saveUnderLock(path, lockPath, 200, setA);

	int status = 0;
	waitpid(other, &status, 0);
	CHECK(WIFEXITED(status));
	CHECK(WEXITSTATUS(status) == 0);   // the other writer's rename landed
	CHECK(first == LockedSave::LockBusy);

	// The interface still has its change, and saves it once the lock is free.
	if (first != LockedSave::Written)
		CHECK(saveUnderLock(path, lockPath, 1000, setA) == LockedSave::Written);
	const std::string last = get(path);
	CHECK(last.find("b=2\n") != std::string::npos);   // the other writer's key
	CHECK(last.find("a=2\n") != std::string::npos);   // and the interface's
}

// ------------------------------------------------------------------ PL-064

TEST_CASE("a cut live file beside a whole temporary and no record loads the temporary (PL-064)")
{
	// The writer before #102 wrote system.cfg.tmp whole, then truncated the
	// live file and copied the temporary into it. Killed in the copy, it
	// left a live file cut short -- empty, or a prefix -- beside a whole
	// .tmp, and a device that had never run the build that keeps .backup had
	// no record. The start deleted the .tmp unread and took the fragment:
	// empty read as nothing (defaults), a prefix with one key=value line in
	// it passed as usable and was recorded as the last known good.
	ScratchDir dir;
	const std::string path = dir / "system.cfg";
	const std::string whole = "system.hostname=RG35XXSP\nwifi.ssid=Home\nwifi.key=secret\naudio.volume=70\n";
	put(path + ".tmp", whole);

	SUBCASE("the live file truncated to nothing")
	{
		put(path, "");
		const LoadedConfig c = chooseConfig(path);
		CHECK(c.source == LoadedConfig::Source::Temporary);
		CHECK(c.text == whole);
		CHECK(c.record);
	}
	SUBCASE("the live file cut part way through a line")
	{
		put(path, whole.substr(0, 46));   // "system.hostname=RG35XXSP\nwifi.ssid=Home\nwifi.k"
		const LoadedConfig c = chooseConfig(path);
		CHECK(c.source == LoadedConfig::Source::Temporary);
		CHECK(c.text == whole);
	}
	SUBCASE("the live file missing")
	{
		const LoadedConfig c = chooseConfig(path);
		CHECK(c.source == LoadedConfig::Source::Temporary);
		CHECK(c.text == whole);
	}
}

TEST_CASE("usable means complete: a live file cut mid-line is read, and never recorded (PL-064)")
{
	// No temporary to compare it with: the cut file is still the best there
	// is and is read as it always was, but it is not whole, so it must not
	// replace a good record.
	ScratchDir dir;
	const std::string path = dir / "system.cfg";
	put(path, "system.hostname=RG35XXSP\nwifi.ssid=Ho");
	LoadedConfig c = chooseConfig(path);
	CHECK(c.source == LoadedConfig::Source::Live);
	CHECK_FALSE(c.record);

	// A whole one is read and recorded, as ever.
	put(path, "system.hostname=RG35XXSP\nwifi.ssid=Home\n");
	c = chooseConfig(path);
	CHECK(c.source == LoadedConfig::Source::Live);
	CHECK(c.record);
}

TEST_CASE("the choices that were already right stay right (PL-064)")
{
	ScratchDir dir;
	const std::string path = dir / "system.cfg";
	const std::string good = "system.hostname=A\n";

	// A whole live file wins over a leftover temporary: a temporary beside
	// a whole file is a save that never reached its rename, or the shell's
	// own write in flight, and neither is the file.
	put(path, good);
	put(path + ".tmp", "system.hostname=A\nwifi.ssid=B\n");
	CHECK(chooseConfig(path).source == LoadedConfig::Source::Live);
	::unlink((path + ".tmp").c_str());

	// An unusable live file and a record: the record.
	put(path, std::string("\0\0\0", 3));
	put(path + ".backup", good);
	LoadedConfig c = chooseConfig(path);
	CHECK(c.source == LoadedConfig::Source::Backup);
	CHECK(c.text == good);

	// An unusable live file and nothing else: read as it is, recorded nowhere.
	::unlink((path + ".backup").c_str());
	put(path, "# only a comment\n");
	c = chooseConfig(path);
	CHECK(c.source == LoadedConfig::Source::Damaged);
	CHECK_FALSE(c.record);

	// Nothing at all.
	::unlink(path.c_str());
	CHECK(chooseConfig(path).source == LoadedConfig::Source::Missing);

	// A temporary that is not whole itself is never taken.
	put(path, "");
	put(path + ".tmp", "system.hostname=A\nwifi.ss");
	CHECK(chooseConfig(path).source == LoadedConfig::Source::Damaged);
}

