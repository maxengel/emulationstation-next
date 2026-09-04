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

	BusyComponent mBusyAnim;
	NinePatchComponent mBackground;

	std::shared_ptr<TextComponent> mTitle;
	std::shared_ptr<TextComponent> mStatus;
	std::shared_ptr<TextComponent> mFooter;

	std::string mCommand;
	std::string mTitleText;

	std::mutex mMutex;
	std::string mLine;
	int mPercent;
	bool mFinished;
	int mExit;

	int mElapsedMs;
	std::thread* mHandle;
};
