#pragma once
#ifndef ES_APP_OFFLINE_SCAN_JOB_H
#define ES_APP_OFFLINE_SCAN_JOB_H

#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

class Window;

// One run of raofflineproxy-ctl scan (fork #179, D-RA-010), apart from the
// page that shows it (GuiOfflineScan), so the page can be left and the run
// goes on (audit #186 PL-07). A first scan of a thousand games is an hour;
// the fourth surface tier (es-native-ui.md) is a page that outlives the
// job, not one that holds the player for the job's length.
//
// The run is a thread of its own reading the ctl's stdout -- ">>> " lines,
// the header of raofflineproxy-ctl is the contract -- into the state below,
// and it keeps itself alive (the thread holds the shared_ptr) until the ctl
// exits, whether or not a page is watching. One run at a time: start()
// hands back the run in flight when there is one, as the ctl itself would
// refuse a second (exit 75). The row under SCAN GAMES reads the run through
// current() while it is in flight and the stamp the ctl writes once it is
// done; a page opened while a run is in flight attaches to it.
class OfflineScanJob : public std::enable_shared_from_this<OfflineScanJob>
{
public:
	struct State
	{
		bool listing = false;     // ">>> doing listing": the library is being walked
		int total = 0;            // ">>> total n" / the n of ">>> game i|n|name"
		int index = 0;            // the i
		std::string game;         // the name
		int cached = 0, skipped = 0, ready = -1;
		bool limit = false, nothingNew = false;
		std::string why;          // the ctl's token
		bool finished = false;
		int exit = -1;
		int elapsedMs = 0;        // since the run began; frozen once finished
	};

	// The run in flight or the last one this session; null when none has run.
	static std::shared_ptr<OfflineScanJob> current();
	// Whether a run is in flight.
	static bool running();
	// Start a run of command, or hand back the one in flight.
	static std::shared_ptr<OfflineScanJob> start(Window* window, const std::string& command);

	State state() const;
	const std::string& command() const { return mCommand; }

	// Run on the interface thread whenever the run's state changes and once
	// when it ends -- at most one waiting at a time -- so the row that
	// offered the scan can follow it with no page open. The page sets it on
	// attach; the last setter wins.
	void setOnChanged(const std::function<void()>& onChanged);

private:
	OfflineScanJob(Window* window, const std::string& command);
	void run(std::shared_ptr<OfflineScanJob> self);
	void handleLine(const std::string& line);
	void changed();
	static std::string cleanLine(const std::string& raw);

	Window* mWindow;
	std::string mCommand;
	mutable std::mutex mMutex;
	State mState;
	std::chrono::steady_clock::time_point mStarted;
	std::function<void()> mOnChanged;
	bool mPostPending = false;   // a changed() already waits on the interface thread

	static std::mutex sMutex;
	static std::shared_ptr<OfflineScanJob> sCurrent;
};

#endif // ES_APP_OFFLINE_SCAN_JOB_H
