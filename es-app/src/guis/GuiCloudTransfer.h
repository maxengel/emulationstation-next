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
	GuiCloudTransfer(Window* window, const std::string& command, const std::string& title);
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
	static std::string cleanLine(const std::string& raw);
	static int parsePercent(const std::string& body);
	static long parseBytes(const std::string& field);

	BusyComponent mBusyAnim;
	NinePatchComponent mBackground;

	std::shared_ptr<TextComponent> mTitle;
	// Seven lines under the title (maintainer's layout, 2026-09-06): the ROM,
	// its transfer, the system, the system's transfer, the bar, elapsed, and
	// the notice. Each is one line, fitted to the width, so nothing wraps into
	// the line below.
	std::shared_ptr<TextComponent> mStatus;    // 1. the file being moved right now
	std::shared_ptr<TextComponent> mFileLine;  // 2. that file's own progress
	std::shared_ptr<TextComponent> mUnit;      // 3. the system (or phase) being copied
	std::shared_ptr<TextComponent> mUnitLine;  // 4. its files and bytes so far
	std::shared_ptr<TextComponent> mNote;      // 5. where the bar was: what to do next, once done
	std::shared_ptr<TextComponent> mElapsed;   // 6. elapsed
	std::shared_ptr<TextComponent> mFooter;    // 7. the notice / press any button
	std::shared_ptr<Font> mTextFont;
	std::shared_ptr<Font> mSmallFont;
	float mLineWidth;
	static std::string fitOneLine(const std::shared_ptr<Font>& font, std::string text, float width);
	static std::string prettyRclone(std::string fragment);

	std::string mCommand;
	std::string mTitleText;

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
	std::string mUnitIndex, mUnitCount;
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
