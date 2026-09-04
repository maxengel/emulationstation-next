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
	std::shared_ptr<TextComponent> mStatus;   // the file being moved right now
	std::shared_ptr<TextComponent> mDetail;   // bytes, rate, ETA
	std::shared_ptr<TextComponent> mFooter;

	std::string mCommand;
	std::string mTitleText;

	// Panel geometry, computed once in the constructor: render() draws a
	// border around exactly the rectangle fitTo() was given, so the two
	// cannot drift apart.
	Vector2f mPanelPos;
	Vector2f mPanelSize;

	std::mutex mMutex;
	std::string mCurrent;   // " * name.zip: 45% /2.5Mi, 300Ki/s, 5s"
	std::string mTotals;    // "1.4 GiB / 2.0 GiB, 70%, 2.5 MiB/s, ETA 3m2s"
	int mFilesThisBlock;
	int mPercent;
	bool mFinished;
	int mExit;

	int mElapsedMs;
	std::thread* mHandle;
};
