#pragma once

#include "GuiComponent.h"
#include "components/BusyComponent.h"
#include "components/NinePatchComponent.h"
#include "components/TextComponent.h"

#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// The page SCAN GAMES FOR OFFLINE ACHIEVEMENTS runs on (fork #179, D-RA-010).
//
// A scan hashes every game on the console and asks RetroAchievements about
// each one, with the client's own pauses between requests: minutes for a
// library, not seconds. That is the fourth surface tier (es-native-ui.md):
// a page that owns the screen, refuses input while the job runs, shows the
// live line and the elapsed time -- no bar, because the client gives no
// percentage worth drawing one from -- and stays with the outcome until the
// player dismisses it. GuiCloudTransfer's shape, without its rclone parser:
// the backend is raofflineproxy-ctl scan, which talks to this page through
// ">>> " lines (its header spells them) and exits with the codes CloudExit.h
// names for a run that did nothing.
class GuiOfflineScan : public GuiComponent
{
public:
	// onClosed runs after the page is gone, whichever button closed it, so
	// the row that opened it can re-read the stamp the scan wrote.
	GuiOfflineScan(Window* window, const std::string& command, const std::function<void()>& onClosed = nullptr);
	virtual ~GuiOfflineScan();

	void render(const Transform4x4f& parentTrans) override;
	bool input(InputConfig* config, Input input) override;
	std::vector<HelpPrompt> getHelpPrompts() override;
	void update(int deltaTime) override;

	// "N GAMES READY FOR OFFLINE PLAY": the sentence the row under SCAN GAMES
	// and this page's line 3 both end on, so the two never disagree.
	static std::string readyPhrase(int ready);

private:
	void threadRun();
	void handleLine(const std::string& line);
	void reset();
	// The done page's word (D-UI-028), from the exit code. Called with
	// mMutex held.
	struct Outcome
	{
		bool completed;
		bool skipped;      // a sentinel: not online, or another scan running
		std::string word;
	};
	Outcome outcome() const;
	static std::string fitOneLine(const std::shared_ptr<Font>& font, std::string text, float width);
	static std::string cleanLine(const std::string& raw);
	static std::string countsLine(int cached, int skipped);

	BusyComponent mBusyAnim;
	NinePatchComponent mBackground;

	std::shared_ptr<TextComponent> mTitle;
	// Seven lines under the title, GuiCloudTransfer's layout (D-UI-024/026):
	// the state, GAME i OF n, the game being looked at, the counts so far,
	// the spinner's row, elapsed, the notice. Once the run is over the same
	// rows carry the outcome, the counts, how many games are ready, the why,
	// and what to do next.
	std::shared_ptr<TextComponent> mStatus;    // 1. SCANNING... / the outcome word
	std::shared_ptr<TextComponent> mCounter;   // 2. GAME i OF n / the run's counts
	std::shared_ptr<TextComponent> mActivity;  // 3. the game / N GAMES READY FOR OFFLINE PLAY
	std::shared_ptr<TextComponent> mDetail;    // 4. the counts so far / the why
	std::shared_ptr<TextComponent> mNote;      // 5. where the spinner was: what to do next, once done
	std::shared_ptr<TextComponent> mElapsed;   // 6. elapsed
	std::shared_ptr<TextComponent> mFooter;    // 7. the notice / the buttons
	std::shared_ptr<Font> mTextFont;
	std::shared_ptr<Font> mSmallFont;
	float mLineWidth;

	std::string mCommand;
	std::function<void()> mOnClosed;

	Vector2f mPanelPos;
	Vector2f mPanelSize;

	std::mutex mMutex;
	bool mListing;        // ">>> doing listing": the library is being walked
	int mTotal;           // ">>> total n" / the n of ">>> game i|n|name"
	int mIndex;           // the i
	std::string mGame;    // the name
	int mCached, mSkipped, mReady;
	bool mLimit, mNothingNew;
	std::string mWhy;     // the ctl's token
	bool mFinished;
	int mExit;
	bool mShownFinished;

	int mElapsedMs;
	std::thread* mHandle;
};
