#pragma once

#include "GuiComponent.h"
#include "components/BusyComponent.h"
#include "components/NinePatchComponent.h"
#include "components/TextComponent.h"

#include <mutex>
#include <string>
#include <thread>

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

private:
	void threadRun();
	void handleLine(const std::string& line);
	static std::string cleanLine(const std::string& raw);

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
	std::shared_ptr<TextComponent> mElapsed;   // 6. elapsed
	std::shared_ptr<TextComponent> mFooter;    // 7. the notice / press any button
	std::shared_ptr<Font> mTextFont;
	std::shared_ptr<Font> mSmallFont;
	float mLineWidth;
	static std::string fitOneLine(const std::shared_ptr<Font>& font, std::string text, float width);
	static std::string prettyRclone(std::string fragment);

	std::string mCommand;
	std::string mTitleText;

	// Panel geometry, computed once in the constructor: render() draws a
	// border around exactly the rectangle fitTo() was given, so the two
	// cannot drift apart.
	Vector2f mPanelPos;
	Vector2f mPanelSize;

	std::mutex mMutex;
	std::string mCurrent;       // "name.zip" -- the head of the current block
	std::string mFileProgress;  // "45% /2.5Mi, 300Ki/s, 5s" -- that file's own line
	std::string mTotals;        // "1.4 GiB / 2.0 GiB, 70%, 2.5 MiB/s, ETA 3m2s"
	std::string mFilesTotals;   // "12 / 45, 27%" -- the count line of the same block
	std::string mUnitLabel;     // ">>> unit nes|2|5" from the script: what is being copied
	std::string mUnitIndex, mUnitCount;
	int mFilesThisBlock;
	int mPercent;
	bool mFinished;
	int mExit;

	int mElapsedMs;
	std::thread* mHandle;
};
