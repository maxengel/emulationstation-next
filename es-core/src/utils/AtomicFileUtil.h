#pragma once
#ifndef ES_CORE_UTILS_ATOMIC_FILE_UTIL_H
#define ES_CORE_UTILS_ATOMIC_FILE_UTIL_H

#include <string>

// Whole-file writes that leave either the old file or the new one on disk,
// never a torn one, and the pid lock the shell's settings functions take.
//
// The two configuration files EmulationStation owns -- system.cfg and
// es_settings.cfg -- used to be rewritten in place: truncate, then write.
// A process killed between the two, or a battery that dies there, leaves an
// empty or half-written file, and the boot-time check then replaced the good
// copy with it (fork #102, D-CLOUD-078). Written to a temporary name, synced,
// and renamed into place, the file on disk is always a complete one; the
// directory is synced afterwards so the rename itself survives a power cut.
namespace Utils
{
	namespace AtomicFile
	{
		// Write `text` to `path` through `path`.tmp: write, fsync, rename over
		// `path`, fsync the directory. True only once the rename has landed;
		// on any failure the temporary is removed and `path` is untouched.
		bool writeText(const std::string& path, const std::string& text);

		// Read `path` whole. `ok` (when given) says whether it could be
		// opened at all, since an empty file and a missing one both read "".
		std::string readText(const std::string& path, bool* ok = nullptr);

		// readText(src) then writeText(dst): dst is replaced whole or not at
		// all. False when src could not be read or dst could not be written.
		bool copy(const std::string& src, const std::string& dst);

		// The settings lock the shell takes around every get_setting and
		// set_setting (wait_lock in profile.d/001-functions): a file created
		// with O_EXCL holding the owner's pid. Same rules, so both sides can
		// wait on each other: a lock whose pid is alive is waited for; one
		// whose pid is gone, or that names no pid, is stale and removed --
		// after a re-read, so a lock released and retaken by another process
		// between the read and the remove is not stolen from it. Released
		// only while it still carries this process's pid, for the same
		// reason. The shell waits forever; acquire() takes a budget, because
		// it runs on the interface thread.
		class PidLock
		{
		public:
			explicit PidLock(const std::string& path);
			~PidLock();

			// True when the lock is held. False when the budget ran out with
			// a live holder still on it, or when the lock file could not be
			// created at all (its directory missing) -- the caller decides
			// whether to go on without it.
			bool acquire(int timeoutMs);
			void release();
			bool held() const { return mHeld; }

		private:
			std::string mPath;
			bool mHeld;
		};
	}
}

#endif // ES_CORE_UTILS_ATOMIC_FILE_UTIL_H
