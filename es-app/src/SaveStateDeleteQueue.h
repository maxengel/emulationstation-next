#pragma once
//
// SaveStateDeleteQueue - the order book for the save state manager's
// deletions (fork #205, D-UI-073): which files are on their way out, in what
// order, and how many have gone.
//
// Pure on purpose: no thread, no file, no script, so es-app/tests/unit can
// hold it to its rules. SaveStateDeleter wraps one of these in the worker
// that records each deletion and unlinks the files (D-CLOUD-053 keeps its
// order there: the retire before the unlink). The rules this class carries:
//
//   - one job at a time: take() hands out nothing while a job runs, so two
//     deletions never put two writers on the manifest;
//   - a file queued or running is pending, and the manager hides its tile;
//   - a file is never queued twice while it is pending;
//   - completed() counts finished jobs, so a page can remember the number
//     it last saw and reload when it moves, holding no pointer to anything.

#include <deque>
#include <set>
#include <string>

struct SaveStateDeleteJob
{
	std::string stateFile;   // the slot's file; never empty for a real job
	std::string screenshot;  // its thumbnail, or empty when it has none
};

class SaveStateDeleteQueue
{
public:
	// Queue a deletion. Returns false when nothing was queued: an empty
	// state path, or a file that is already pending.
	bool enqueue(const std::string& stateFile, const std::string& screenshot);

	// Move the next queued job to running and hand it out. False when
	// nothing is queued or a job is already running.
	bool take(SaveStateDeleteJob& out);

	// The running job is done: its files stop being pending and the count
	// of finished deletions goes up by one. Nothing running: nothing changes.
	void finish();

	// Queued or running.
	bool isPending(const std::string& stateFile) const;

	// Finished deletions since this queue was made.
	unsigned completed() const { return mCompleted; }

	bool running() const { return mRunning; }
	size_t queued() const { return mQueue.size(); }
	bool idle() const { return mQueue.empty() && !mRunning; }

private:
	std::deque<SaveStateDeleteJob> mQueue;
	std::set<std::string> mPending;
	SaveStateDeleteJob mCurrent;
	bool mRunning = false;
	unsigned mCompleted = 0;
};
