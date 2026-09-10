#pragma once

#include "GuiComponent.h"
#include "components/BusyComponent.h"
#include "components/NinePatchComponent.h"
#include "components/TextComponent.h"

#include <mutex>
#include <string>
#include <thread>
#include <vector>

// A screen for a transfer that takes minutes, not seconds.
//
// The progress card is the right surface for work you can keep playing
// through -- a scrape, a hash. It is the wrong one for a restore: the card
// closes itself when the job ends, and a 1.4 GiB restore is precisely the
// thing somebody walks away from. Come back to a sleeping handheld and the
// only record that anything happened is a log file.
//
// So a long transfer owns the screen and stays there until it is dismissed.
// The result is the last thing on it, not the first thing to disappear.
class GuiCloudTransfer : public GuiComponent
{
public:
	// itemsExpected: how many items ES chained into this run -- one for saves,
	// one per system the picker left ticked, one for settings -- so row 2 can
	// read ITEM 1 OF 4 before the first script has said anything. 0 means
	// unknown, and row 2 shows ITEM i alone until a script says. A content
	// script announces its own count and corrects it (it may add bios to
	// what was ticked); itemsAfterContent is how many single-item phases ES
	// chained after the content phase, so that correction still counts them.
	GuiCloudTransfer(Window* window, const std::string& command, const std::string& title,
		int itemsExpected = 0, int itemsAfterContent = 0);
	virtual ~GuiCloudTransfer();

	void render(const Transform4x4f& parentTrans) override;
	bool input(InputConfig* config, Input input) override;
	std::vector<HelpPrompt> getHelpPrompts() override;
	void update(int deltaTime) override;

	// A size at the precision it has: "200 KB", "1.2 MB", "1.20 GB". The one
	// formatter for every size a cloud page prints -- this page's summary,
	// the content picker's rows, and the match confirmation -- so no two of
	// them disagree on a number.
	static std::string sizeLabel(unsigned long bytes);

private:
	void threadRun();
	void handleLine(const std::string& line);
	void refreshPercent();
	void foldUnit();
	// Every counter the constructor set, back to its starting value, and the
	// done-state rows cleared: TRY AGAIN (input) re-runs the same command
	// from here once the finished worker has been joined.
	void reset();
	// The done page's word and what follows it (D-UI-028), from the tiers,
	// the failed items and the exit code. Called with mMutex held.
	struct Outcome
	{
		bool completed;   // every part finished (0 or 9)
		bool partial;     // some finished and some did not; a failure to the player
		bool skipped;     // a sentinel, and nothing else to report
		std::string word; // line 1
	};
	Outcome outcome() const;
	bool completed() const { return mExit == 0 || mExit == 9; }
	static std::string cleanLine(const std::string& raw);
	static int parsePercent(const std::string& body);
	static long parseBytes(const std::string& field);

	BusyComponent mBusyAnim;
	NinePatchComponent mBackground;

	std::shared_ptr<TextComponent> mTitle;
	// Seven lines under the title (maintainer's layout, 2026-09-06, D-UI-024;
	// the item first and numbered across the run, 2026-09-09, D-UI-026): the
	// item, ITEM i OF n, what it is doing on it, that item's files and bytes,
	// the bar, elapsed, and the notice. Each is one line, fitted to the
	// width, so nothing wraps into the line below. Once the run is over the
	// same four rows carry the outcome, the run's summary and its detail.
	std::shared_ptr<TextComponent> mStatus;    // 1. the item (BIOS, NES, SAVES, SETTINGS); the outcome, once done
	std::shared_ptr<TextComponent> mCounter;   // 2. ITEM i OF n, counted across the whole run
	std::shared_ptr<TextComponent> mActivity;  // 3. what it is doing on that item; the run's summary, once done
	std::shared_ptr<TextComponent> mDetail;    // 4. that item's files and bytes so far; the summary's detail, once done
	std::shared_ptr<TextComponent> mNote;      // 5. where the bar was: what to do next, once done
	std::shared_ptr<TextComponent> mElapsed;   // 6. elapsed
	std::shared_ptr<TextComponent> mFooter;    // 7. the notice / press any button
	std::shared_ptr<Font> mTextFont;
	std::shared_ptr<Font> mSmallFont;
	float mLineWidth;
	static std::string fitOneLine(const std::shared_ptr<Font>& font, std::string text, float width);
	// Drop whole sentences from the end before clipping: "WHAT WAS SENT IS IN
	// YOUR CLOUD. THE REST IS STILL ON THIS DEVICE." keeps its first sentence
	// on a panel too narrow for both, rather than ending mid-word in an
	// ellipsis. The last sentence standing is clipped if even it does not fit.
	static std::string fitSentences(const std::shared_ptr<Font>& font, std::string text, float width);
	static std::string prettyRclone(std::string fragment);
	static std::string roundSizes(const std::string& fragment);

	std::string mCommand;
	std::string mTitleText;
	int mItemsExpected, mItemsAfterContent;   // the constructor's estimate, kept for reset()

	// Panel geometry, decided once in the constructor and never re-derived:
	// fitTo() is given this rectangle, the text rows are stacked inside it,
	// and the bar is drawn at mBar*, on the row the spinner and the done-note
	// share -- so the frame, the rows and the bar cannot drift apart.
	Vector2f mPanelPos;
	Vector2f mPanelSize;
	float mBarX, mBarY, mBarW, mBarH;

	std::mutex mMutex;
	std::string mCurrent;       // "name.zip" -- the head of the current block
	std::string mFileProgress;  // "45% /2.5Mi, 300Ki/s, 5s" -- that file's own line
	std::string mTotals;        // "1.4 GiB / 2.0 GiB, 70%, 2.5 MiB/s, ETA 3m2s"
	std::string mFilesTotals;   // "12 / 45, 27%" -- the count line of the same block
	std::string mUnitLabel;     // ">>> unit nes|2|5" from the script: what is being copied
	std::string mDoing;         // ">>> doing archive": what the item is busy with before rclone runs
	// ITEM i OF n across the whole run (D-UI-026). One run chains several
	// scripts and each numbers only its own units, so the page counts: a
	// ">>> unit" whose label differs from the current one is the next item,
	// the same label again is a re-announcement and is not. mItemCount
	// starts as ES's estimate (0 = unknown) and is corrected by a script
	// that announces its own count: items counted before that script's
	// first announcement (mScriptBase) + its count + the single-item phases
	// ES chained after it (mTrailing). mLastScriptIndex is the last
	// announcement's own index, 0 when it carried none, so a count that
	// starts over is a new script.
	int mItemIndex, mItemCount, mTrailing, mScriptBase, mLastScriptIndex;
	// What this unit's rclone has moved so far, as its last "Transferred:"
	// pair read: the bytes line's first field and the count line's first
	// number. Folded into the run's totals when the next unit starts and
	// when the command exits (foldUnit), so the done page can say what the
	// whole run moved rather than what its last unit did. mRunSized records
	// that a byte line was parsed at all: without one there is no number to
	// show, and the page says so rather than inventing a zero.
	long mUnitBytes, mUnitFiles;
	long mRunBytes, mRunFiles;
	bool mRunSized;
	// ">>> removed 14|314572800|snes:12:300000000,gb:2:14572800" -- a match's
	// summary, rendered on the last screen in place of rclone's totals, which
	// for a deletion read "0 B / 0 B" (maintainer, 2026-09-07).
	long mRemovedFiles, mRemovedBytes;
	std::vector<std::string> mRemovedDetail;   // "SNES 12 FILES · 300 MB", per system
	bool mAnyTransferred;       // some block moved bytes: the device's ROMs changed
	int mFilesThisBlock;
	bool mSeenBlock;
	// "Checks:  12 / 45, 27%, Listed 300" -- what rclone compared rather than
	// moved. A run where everything is already in the cloud is nothing but
	// checks: no bytes, no " * file" lines, and without these it looked hung.
	long mChecksDone, mChecksTotal, mListed;
	std::string mChecking;      // " * name: checking" -- the file being compared, if rclone caught one in flight
	int mChecksThisBlock;
	// One block of rclone's output carries up to three percentages: bytes,
	// files transferred, files checked. Each is that line's last word (-1
	// when it printed no number) so the one shown (mPercent) is chosen in
	// refreshPercent(), not by whichever line came last.
	int mBytePercent, mFilePercent, mCheckPercent;
	int mPercent;               // -1 until a block has produced a real number
	bool mFinished;
	int mExit;
	// What each part of a composed run reported as it ended (">>> tier
	// <label>|<rc>", echoed by GuiMenu's run composition after each part),
	// and which items did not finish, with why (D-UI-028). A ">>> why
	// <sentence>" from a script is attached to the unit it arrived under --
	// or, printed before any unit, to the tier that reports next -- so the
	// done page can name NES and SETTINGS and say what stopped them. A tier
	// that fails with no why under it is itself the item that did not
	// finish, with the code's phrase; its units are not presumed to have
	// finished, because nothing said they did.
	struct Tier { std::string label; int rc; int unitsAnnounced; int unitsFailed; bool tierLevelFail; };
	struct Failed { std::string label; std::string why; };
	std::vector<Tier> mTiers;
	std::vector<Failed> mFailed;
	std::vector<Failed> mPendingWhys;          // since the last tier line; label "" before any unit
	std::vector<std::string> mUnitsSinceTier;  // items announced since the last tier line
	std::string mWhy;                          // the last why of the run, for a command with no tiers
	// The frame's copy of (mFinished, mPercent), taken in update() under the
	// lock that also reads the text -- render() draws the bar from these, so
	// the bar and the eight rows are always the same stats block. Reading
	// mPercent again in render() let the worker advance it between the two,
	// and the bar ran one block ahead of the text for a frame.
	bool mShownFinished;
	int mShownPercent;

	int mElapsedMs;
	std::thread* mHandle;
};
