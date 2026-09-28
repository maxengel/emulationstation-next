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
