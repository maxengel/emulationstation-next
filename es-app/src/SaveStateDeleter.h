#pragma once
//
// SaveStateDeleter - the save state manager's deletions, off the interface
// thread (fork #205, D-UI-073).
//
// DELETE used to run `cloud_capture --retire` and `--rescan` through popen
// on the interface thread and redraw the grid only when both had exited:
// two bash starts, a dozen jq starts and a fork of a 350 MB process each,
// about a third of a second on the x86_64 guest and a second on a Cortex-A53,
// felt as the dialog hanging after YES. Now the manager hides the tile the
// same frame and hands the work to one worker thread, which keeps the order
// D-CLOUD-053 asks for -- the retire is recorded, then the files go -- one
// deletion at a time. The rescan is gone (D-CLOUD-132): it re-keyed slots
// the renumber had moved, and nothing renumbers since D-UI-069.
//
// The rules (D-UI-073), because each guards a way this can crash or lie:
//   - the worker holds two path strings per job and never a pointer to a
//     page or to the repository's SaveState, both of which die under it;
//     a page learns a job is done by polling completed() in update();
//   - one queue, one job at a time (SaveStateDeleteQueue), so two quick
//     deletions never race the manifest;
//   - shutdown() joins the worker at exit; a joinable std::thread destroyed
//     at static teardown would terminate the process instead.

#include <string>

class SaveStateDeleter
{
public:
	// Record and unlink on the worker; returns at once. The manager hides
	// the tile the same frame (isPending) and reloads when completed() moves.
	static void enqueue(const std::string& stateFile, const std::string& screenshot);

	// Queued or running: the tile is hidden, the file may still be on disk.
	static bool isPending(const std::string& stateFile);

	// Finished deletions since the process started.
	static unsigned completed();

	// Drain the queue and join the worker. Called at exit; harmless when
	// nothing ever ran, and a second call does nothing.
	static void shutdown();
};
