#include "utils/AtomicFileUtil.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <thread>

#if !defined(_WIN32)
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#endif

namespace Utils
{
	namespace AtomicFile
	{
#if !defined(_WIN32)
		// fsync the directory holding `path`, so the rename that just landed
		// in it is on disk too. Best effort: a filesystem that refuses to
		// open a directory for reading still has the rename in its journal.
		static void syncDirectoryOf(const std::string& path)
		{
			auto slash = path.find_last_of('/');
			const std::string dir = slash == std::string::npos ? "." : slash == 0 ? "/" : path.substr(0, slash);
			int fd = ::open(dir.c_str(), O_RDONLY | O_DIRECTORY);
			if (fd < 0)
				return;
			::fsync(fd);
			::close(fd);
		}
#endif

		bool writeText(const std::string& path, const std::string& text)
		{
			const std::string tmp = path + ".tmp";
#if defined(_WIN32)
			{
				std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
				if (!out)
					return false;
				out.write(text.data(), (std::streamsize) text.size());
				if (!out)
				{
					out.close();
					std::remove(tmp.c_str());
					return false;
				}
			}
			std::remove(path.c_str());
			if (std::rename(tmp.c_str(), path.c_str()) != 0)
			{
				std::remove(tmp.c_str());
				return false;
			}
			return true;
#else
			// 0644 before umask, as the shell's redirections make it; the
			// rename then carries the mode over to the live name.
			int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
			if (fd < 0)
				return false;

			const char* data = text.data();
			size_t left = text.size();
			bool ok = true;
			while (left > 0)
			{
				ssize_t n = ::write(fd, data, left);
				if (n < 0)
				{
					if (errno == EINTR)
						continue;
					ok = false;
					break;
				}
				data += n;
				left -= (size_t) n;
			}
			// The data must be on disk before the rename makes it the file
			// everything reads: rename first and a power cut leaves a
			// complete-looking name over empty blocks.
			if (ok && ::fsync(fd) != 0)
				ok = false;
			if (::close(fd) != 0)
				ok = false;
			if (!ok)
			{
				::unlink(tmp.c_str());
				return false;
			}
			if (std::rename(tmp.c_str(), path.c_str()) != 0)
			{
				::unlink(tmp.c_str());
				return false;
			}
			syncDirectoryOf(path);
			return true;
#endif
		}

		std::string readText(const std::string& path, bool* ok)
		{
			std::ifstream in(path, std::ios::binary);
			if (!in)
			{
				if (ok != nullptr)
					*ok = false;
				return "";
			}
			std::ostringstream ss;
			ss << in.rdbuf();
			if (ok != nullptr)
				*ok = true;
			return ss.str();
		}

		bool copy(const std::string& src, const std::string& dst)
		{
			bool ok = false;
			const std::string text = readText(src, &ok);
			if (!ok)
				return false;
			return writeText(dst, text);
		}

		PidLock::PidLock(const std::string& path) : mPath(path), mHeld(false)
		{
		}

		PidLock::~PidLock()
		{
			release();
		}

		bool PidLock::acquire(int timeoutMs)
		{
#if defined(_WIN32)
			mHeld = true;
			return true;
#else
			if (mHeld)
				return true;

			const auto started = std::chrono::steady_clock::now();
			const std::string mine = std::to_string((long long) ::getpid()) + "\n";
			for (;;)
			{
				int fd = ::open(mPath.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0644);
				if (fd >= 0)
				{
					// The pid is what tells the next waiter whether to wait
					// or to clear a lock its owner never released.
					ssize_t n = ::write(fd, mine.data(), mine.size());
					::close(fd);
					(void) n;
					mHeld = true;
					return true;
				}
				// Not "already there": the directory is missing, or the path
				// is not writable. Nothing to wait for and nothing to hold.
				if (errno != EEXIST)
					return false;

				// Held -- or was. Read who by, and ask the kernel.
				bool ok = false;
				std::string holder = readText(mPath, &ok);
				if (!ok)
					continue;   // released between the create and the read: try again at once
				while (!holder.empty() && (holder.back() == '\n' || holder.back() == '\r' || holder.back() == ' '))
					holder.pop_back();

				bool numeric = !holder.empty();
				for (char c : holder)
					if (c < '0' || c > '9')
						numeric = false;

				bool stale;
				if (!numeric)
				{
					// Empty, or not a pid: nobody can be holding it. An
					// empty file is also what a holder's own create looks
					// like between its open and its write, for a few
					// microseconds -- so give it a moment before deciding,
					// and re-read before removing (below).
					std::this_thread::sleep_for(std::chrono::milliseconds(20));
					stale = true;
				}
				else
				{
					const pid_t pid = (pid_t) atol(holder.c_str());
					// kill 0 asks without signalling. ESRCH is the one
					// answer that means gone; EPERM is a live process this
					// one may not signal, which is still a live holder.
					stale = pid > 0 && ::kill(pid, 0) != 0 && errno == ESRCH;
				}

				if (stale)
				{
					// Only if it still says what was read above: between
					// that read and this remove the holder may have released
					// it and another process taken it, and an unconditional
					// remove would steal the newcomer's lock.
					bool again = false;
					std::string current = readText(mPath, &again);
					while (!current.empty() && (current.back() == '\n' || current.back() == '\r' || current.back() == ' '))
						current.pop_back();
					if (again && current == holder)
						::unlink(mPath.c_str());
					continue;   // retry at once: there is nothing to wait for
				}

				const long elapsed = (long) std::chrono::duration_cast<std::chrono::milliseconds>(
					std::chrono::steady_clock::now() - started).count();
				if (elapsed >= timeoutMs)
					return false;
				std::this_thread::sleep_for(std::chrono::milliseconds(50));
			}
#endif
		}

		void PidLock::release()
		{
			if (!mHeld)
				return;
			mHeld = false;
#if !defined(_WIN32)
			// Only a lock this process still holds: another process may own
			// the file by now if this one's was removed as stale.
			bool ok = false;
			std::string holder = readText(mPath, &ok);
			while (!holder.empty() && (holder.back() == '\n' || holder.back() == '\r' || holder.back() == ' '))
				holder.pop_back();
			if (ok && holder == std::to_string((long long) ::getpid()))
				::unlink(mPath.c_str());
#endif
		}
	}
}
