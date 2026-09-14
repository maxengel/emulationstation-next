#include "guis/GuiOfflineScan.h"

#include "CloudExit.h"
#include "CloudText.h"
#include "OfflineAchievements.h"
#include "Window.h"
#include "ThemeData.h"
#include "LocaleES.h"
#include "utils/StringUtil.h"
#include "Log.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <sys/wait.h>

GuiOfflineScan::GuiOfflineScan(Window* window, const std::string& command, const std::function<void()>& onClosed)
	: GuiComponent(window), mBusyAnim(window, ""), mBackground(window, ":/frame.png"),
	  mCommand(command), mOnClosed(onClosed), mHandle(nullptr)
{
	reset();

	auto theme = ThemeData::getMenuTheme();
	mBackground.setImagePath(theme->Background.path);
	mBackground.setEdgeColor(theme->Background.color);
	mBackground.setCenterColor(theme->Background.centerColor);
	mBackground.setCornerSize(theme->Background.cornerSize);

	const float SW = Renderer::getScreenWidth();
	const float SH = Renderer::getScreenHeight();
	setSize(SW, SH);

	mTextFont  = theme->Text.font;
	mSmallFont = theme->TextSmall.font;
	mTitle    = std::make_shared<TextComponent>(window, _("SCANNING GAMES FOR OFFLINE ACHIEVEMENTS"), theme->Title.font, theme->Title.color, ALIGN_CENTER);
	mStatus   = std::make_shared<TextComponent>(window, _("PREPARING..."), mTextFont,  theme->Text.color,      ALIGN_CENTER);
	mCounter  = std::make_shared<TextComponent>(window, "",                mSmallFont, theme->TextSmall.color, ALIGN_CENTER);
	mActivity = std::make_shared<TextComponent>(window, "",                mTextFont,  theme->Text.color,      ALIGN_CENTER);
	mDetail   = std::make_shared<TextComponent>(window, "",                mSmallFont, theme->TextSmall.color, ALIGN_CENTER);
	mNote     = std::make_shared<TextComponent>(window, "",                mSmallFont, theme->TextSmall.color, ALIGN_CENTER);
	mElapsed  = std::make_shared<TextComponent>(window, "",                mSmallFont, theme->TextSmall.color, ALIGN_CENTER);
	mFooter   = std::make_shared<TextComponent>(window, "",                mSmallFont, theme->TextSmall.color, ALIGN_CENTER);

	// One column, 0.78 of the width, every line fitted to it, and the rows
	// stacked from the fonts' own heights -- GuiCloudTransfer's geometry,
	// so the two long-job pages of this app sit the same way on every panel
	// (its constructor carries the derivation and the numbers per panel).
	const float w  = SW * 0.78f;
	const float cx = SW * 0.5f;
	const float x  = cx - w / 2.0f;
	mLineWidth = w;
	for (auto& t : { mTitle, mStatus, mCounter, mActivity, mDetail, mNote, mElapsed, mFooter })
		t->setSize(w, 0);

	const float hT = theme->Title.font->getHeight();
	const float hM = mTextFont->getHeight();
	const float hS = mSmallFont->getHeight();
	const float natural = 2 * SH * 0.05f + hT + 3 * hM + 4 * hS + 5.5f * SH * 0.02f;
	const float fit = natural > SH * 0.9f ? (SH * 0.9f) / natural : 1.0f;
	const float pad = SH * 0.05f * fit;
	const float gap = SH * 0.02f * fit;
	const float rT = hT * fit, rM = hM * fit, rS = hS * fit;
	const float H = 2 * pad + rT + 3 * rM + 4 * rS + 5.5f * gap;
	const float top = (SH - H) / 2.0f;

	float y = top + pad;
	mTitle   ->setPosition(x, y);
	y += rT + 1.5f * gap;
	mStatus  ->setPosition(x, y);
	mCounter ->setPosition(x, y + rM);
	y += rM + rS + gap;
	mActivity->setPosition(x, y);
	mDetail  ->setPosition(x, y + rM);
	y += rM + rS + 0.5f * gap;
	// 5. The spinner while it runs, the note once done, one row. No caption
	// on the spinner: line 1 says what is happening, and BusyComponent's
	// setText("") is a no-op against its empty start (es-code-traps.md).
	mBusyAnim.setBackgroundVisible(false);
	mBusyAnim.setSize(w, rM);
	mBusyAnim.setPosition(x, y);
	mNote    ->setPosition(x, y + (rM - hS) / 2.0f);
	y += rM + 1.5f * gap;
	mElapsed ->setPosition(x, y);
	y += rS + gap;
	mFooter  ->setPosition(x, y);

	mPanelSize = Vector2f(w + SW * 0.06f, H);
	mPanelPos  = Vector2f(cx - mPanelSize.x() / 2.0f, top);
	mBackground.fitTo(mPanelSize, Vector3f(mPanelPos.x(), mPanelPos.y(), 0), Vector2f(-32, -32));

	mHandle = new std::thread(&GuiOfflineScan::threadRun, this);
}

GuiOfflineScan::~GuiOfflineScan()
{
	if (mHandle != nullptr)
	{
		if (mHandle->joinable())
			mHandle->join();
		delete mHandle;
	}
}

// The run's starting state: from the constructor, and again from input()
// for TRY AGAIN once the finished worker has been joined.
void GuiOfflineScan::reset()
{
	mListing = false;
	mTotal = 0; mIndex = 0; mGame.clear();
	mCached = 0; mSkipped = 0; mReady = -1;
	mLimit = false; mNothingNew = false; mWhy.clear();
	mFinished = false; mExit = -1; mShownFinished = false;
	mElapsedMs = 0;
	if (mCounter) mCounter->setText("");
	if (mActivity) mActivity->setText("");
	if (mDetail) mDetail->setText("");
	if (mNote) mNote->setText("");
}

// Input is refused while the scan runs -- there is nothing to choose, and a
// stray press should not close a page somebody is waiting on. Once it has
// finished, any button dismisses it, and when the run did not complete, A
// runs it again from here: the surface that reported the failure carries
// the retry (D-UI-028).
bool GuiOfflineScan::input(InputConfig* config, Input input)
{
	std::unique_lock<std::mutex> lock(mMutex);
	if (!mFinished || !input.value)
		return true;
	const Outcome o = outcome();
	if (!o.completed && config->isMappedTo("a", input))
	{
		lock.unlock();
		if (mHandle != nullptr)
		{
			if (mHandle->joinable())
				mHandle->join();
			delete mHandle;
			mHandle = nullptr;
		}
		reset();
		mStatus->setText(_("PREPARING..."));
		mHandle = new std::thread(&GuiOfflineScan::threadRun, this);
		return true;
	}
	lock.unlock();
	// Copied out first: the page is gone by the time it runs.
	std::function<void()> onClosed = mOnClosed;
	delete this;
	if (onClosed)
		onClosed();
	return true;
}

std::vector<HelpPrompt> GuiOfflineScan::getHelpPrompts()
{
	std::vector<HelpPrompt> prompts;
	std::unique_lock<std::mutex> lock(mMutex);
	if (mFinished)
	{
		const Outcome o = outcome();
		if (!o.completed)
			prompts.push_back(HelpPrompt("a", _("TRY AGAIN")));
		prompts.push_back(HelpPrompt("b", _("CLOSE")));
	}
	return prompts;
}

// The word for the run (D-UI-028): COMPLETED, SKIPPED for the two sentinels
// the ctl exits with before touching anything -- not online, another scan
// or top-up holding the lock -- and COULDN'T FINISH for everything else,
// the refusals included: the why on line 4 says which.
GuiOfflineScan::Outcome GuiOfflineScan::outcome() const
{
	Outcome o;
	o.completed = mExit == 0;
	o.skipped = mExit == CloudExit::NoNetwork || mExit == CloudExit::LockHeld;
	if (o.completed)
		o.word = _("COMPLETED");
	else if (mExit == CloudExit::NoNetwork)
		o.word = _("SKIPPED - YOU'RE NOT ONLINE");
	else if (mExit == CloudExit::LockHeld)
		o.word = _("SKIPPED - A SCAN IS ALREADY RUNNING");
	else
		o.word = _("COULDN'T FINISH");
	return o;
}

void GuiOfflineScan::render(const Transform4x4f& parentTrans)
{
	Transform4x4f trans = parentTrans * getTransform();
	Renderer::setMatrix(trans);
	// Dim the whole screen: this is a takeover, and the carousel showing
	// through would say the app is idle while a scan is in flight.
	Renderer::drawRect(0.f, 0.f, mSize.x(), mSize.y(), 0x000000D0);

	mBackground.render(trans);

	for (auto& t : { mTitle, mStatus, mCounter, mActivity, mDetail, mNote, mElapsed, mFooter })
		t->render(trans);

	// A spinner and never a bar: the client reports each game as it
	// finishes it and nothing in between, and hashing a disc image can take
	// as long as ten cartridges, so a bar at i/n would be an invented
	// position (es-native-ui.md: a bar only where a real percentage exists).
	if (!mShownFinished)
		mBusyAnim.render(trans);
}

// Clip to one line: a long game name gets an ellipsis, never a second line.
std::string GuiOfflineScan::fitOneLine(const std::shared_ptr<Font>& font, std::string text, float width)
{
	if (!font || text.empty() || font->sizeText(text).x() <= width)
		return text;
	while (text.size() > 4 && font->sizeText(text + "...").x() > width)
		text.pop_back();
	return text + "...";
}

// "N GAMES READY FOR OFFLINE PLAY": the one sentence the row under SCAN
// GAMES and this page's line 3 both end on, so they never disagree.
std::string GuiOfflineScan::readyPhrase(int ready)
{
	if (ready <= 0)
		return _("NO GAMES READY FOR OFFLINE PLAY YET");
	if (ready == 1)
		return std::string("1 ") + _("GAME READY FOR OFFLINE PLAY");
	return std::to_string(ready) + " " + std::string(_("GAMES READY FOR OFFLINE PLAY"));
}

// "GAMES WITH ACHIEVEMENTS ADDED: 3" -- what this run did. The games without
// a set are not counted here: the maintainer, on the RG SP with RC-5 (2026-09-14),
// "I'm not that concerned about the games that don't have achievements [...]
// I just want to know that it's scanning through the games with achievements."
// The count still travels in the stamp for the log (skipped), unused here.
std::string GuiOfflineScan::countsLine(int cached, int skipped)
{
	(void) skipped;
	return std::string(_("GAMES WITH ACHIEVEMENTS ADDED:")) + " " + std::to_string(cached);
}

void GuiOfflineScan::update(int deltaTime)
{
	GuiComponent::update(deltaTime);
	mBusyAnim.update(deltaTime);
	std::unique_lock<std::mutex> lock(mMutex);
	mShownFinished = mFinished;
	if (!mFinished)
		mElapsedMs += deltaTime;
	const int mins = mElapsedMs / 60000;
	const int secs = (mElapsedMs / 1000) % 60;
	char elapsed[32];
	snprintf(elapsed, sizeof(elapsed), "%d:%02d", mins, secs);

	if (mFinished)
	{
		// The same seven rows, now carrying the outcome (D-UI-028): 1 the
		// word; 2 what this run added and passed over; 3 how many games
		// earn offline now -- the answer the page exists to give; 4 why it
		// stopped, when it did; 5 what to do next; 6 elapsed; 7 the buttons.
		const Outcome o = outcome();
		mStatus->setText(fitOneLine(mTextFont, o.word, mLineWidth));

		const bool ran = mTotal > 0 || mCached > 0 || mSkipped > 0 || mNothingNew;
		mCounter->setText(ran && !mNothingNew ? fitOneLine(mSmallFont, countsLine(mCached, mSkipped), mLineWidth) : "");

		// The ctl's done line carries the count; a run that ended before it
		// said one reads the client's export directly.
		const int ready = mReady >= 0 ? mReady : OfflineAchievements::readyCount();
		mActivity->setText(fitOneLine(mTextFont, readyPhrase(ready), mLineWidth));

		std::string detail;
		if (!o.completed && !o.skipped && !mWhy.empty())
			detail = OfflineAchievements::scanWhy(mWhy);
		else if (!o.completed && !o.skipped)
			detail = _("SOMETHING WENT WRONG");
		mDetail->setText(fitOneLine(mSmallFont, detail, mLineWidth));

		std::string note;
		if (mLimit)
			note = _("THE LIMIT OF 100 GAMES WAS REACHED.");
		else if (mNothingNew)
			note = _("NOTHING NEW - EVERY GAME WAS ALREADY READY.");
		else if (mExit == CloudExit::NoNetwork)
			note = _("TRY AGAIN WHEN YOU'RE ONLINE.");
		mNote->setText(fitOneLine(mSmallFont, note, mLineWidth));

		mElapsed->setText(std::string(_("ELAPSED")) + " " + elapsed);
		mFooter ->setText(!o.completed ? _("A  TRY AGAIN     B  CLOSE") : _("PRESS ANY BUTTON TO CLOSE"));
	}
	else
	{
		// 1 what it is doing; 2 GAME i OF n; 3 the game it is on -- the line
		// that says it is alive, because a disc image can take a while and
		// a frozen count is indistinguishable from a hang; 4 the counts.
		if (mIndex > 0)
			mStatus->setText(_("SCANNING..."));
		else if (mListing)
			mStatus->setText(_("LOOKING THROUGH YOUR GAMES..."));
		else
			mStatus->setText(_("PREPARING..."));

		std::string counter;
		if (mIndex > 0)
		{
			counter = std::string(_("GAME")) + " " + std::to_string(mIndex);
			if (mTotal > 0)
				counter += " " + std::string(_("OF")) + " " + std::to_string(mTotal);
		}
		mCounter ->setText(counter);
		mActivity->setText(fitOneLine(mTextFont, mGame, mLineWidth));
		mDetail  ->setText(mIndex > 0 ? fitOneLine(mSmallFont, countsLine(mCached, mSkipped), mLineWidth) : "");
		mNote    ->setText("");
		mElapsed ->setText(std::string(_("ELAPSED")) + " " + elapsed);
		mFooter  ->setText(_("THIS CAN TAKE A WHILE. YOU CAN LEAVE IT RUNNING."));
	}
}

// The ctl talks to this page through ">>> " lines (raofflineproxy-ctl's
// header is the contract):
//   ">>> doing listing"        the library is being walked
//   ">>> total <n>"            how many ROMs will be looked at
//   ">>> game <i>|<n>|<name>"  the one it is on now
//   ">>> cached <c>|<s>"       added so far, and passed over so far
//   ">>> note <TOKEN>"         LIMIT_REACHED, NOTHING_NEW
//   ">>> why <TOKEN>"          why it stopped, in the ctl's token
//   ">>> done <c>|<s>|<ready>|<limit>"
// Everything else on stdout is the client's own and is not shown.
void GuiOfflineScan::handleLine(const std::string& line)
{
	if (line.rfind(">>> ", 0) != 0)
		return;
	const std::string body = line.substr(4);
	const size_t space = body.find(' ');
	const std::string word = body.substr(0, space);
	const std::string rest = space == std::string::npos ? "" : Utils::String::trim(body.substr(space + 1));
	const std::vector<std::string> fields = Utils::String::split(rest, '|', false);
	auto num = [](const std::string& s) -> int
	{
		const std::string t = Utils::String::trim(s);
		if (t.empty() || t.size() > 9)
			return 0;
		for (char c : t)
			if (c < '0' || c > '9')
				return 0;
		return std::stoi(t);
	};

	std::unique_lock<std::mutex> lock(mMutex);
	if (word == "doing")
		mListing = rest == "listing";
	else if (word == "total")
		mTotal = num(rest);
	else if (word == "game" && fields.size() >= 3)
	{
		mListing = false;
		mIndex = num(fields[0]);
		mTotal = num(fields[1]);
		// The name may itself contain '|', so everything past the second is it.
		std::string name = fields[2];
		for (size_t i = 3; i < fields.size(); i++)
			name += "|" + fields[i];
		mGame = name;
	}
	else if (word == "cached" && fields.size() >= 2)
	{
		mCached = num(fields[0]);
		mSkipped = num(fields[1]);
	}
	else if (word == "note")
	{
		if (rest == "LIMIT_REACHED") mLimit = true;
		else if (rest == "NOTHING_NEW") mNothingNew = true;
	}
	else if (word == "why")
		mWhy = rest;
	else if (word == "done" && fields.size() >= 4)
	{
		mCached = num(fields[0]);
		mSkipped = num(fields[1]);
		mReady = num(fields[2]);
		mLimit = num(fields[3]) != 0;
	}
}

void GuiOfflineScan::threadRun()
{
	int ret = -1;
	// Braces around the whole command: a trailing redirection binds to the
	// last element of a sequence only (GuiCloudTransfer learnt it).
	FILE* pipe = popen(("{ " + mCommand + " ; } 2>&1").c_str(), "r");
	if (pipe != nullptr)
	{
		std::string buf;
		int c;
		while ((c = fgetc(pipe)) != EOF)
		{
			if (c != '\n' && c != '\r')
			{
				if (buf.size() < 1024)
					buf += (char) c;
				continue;
			}
			handleLine(cleanLine(buf));
			buf.clear();
		}
		if (!buf.empty())
			handleLine(cleanLine(buf));

		int status = pclose(pipe);
		if (WIFEXITED(status))
			ret = WEXITSTATUS(status);
	}

	std::unique_lock<std::mutex> lock(mMutex);
	mExit = ret;
	mFinished = true;
	LOG(LogInfo) << "GuiOfflineScan: " << mCommand << " exited " << ret
		<< " (cached " << mCached << ", skipped " << mSkipped << ", ready " << mReady << ")";
}

// Drop C0 controls and DEL, keep every UTF-8 byte (a game's name may carry
// one), then trim.
std::string GuiOfflineScan::cleanLine(const std::string& raw)
{
	std::string clean;
	for (size_t i = 0; i < raw.size(); ++i)
	{
		if (raw[i] == 0x1B)
		{
			while (i < raw.size() && !isalpha((unsigned char) raw[i]))
				i++;
			continue;
		}
		if (((unsigned char) raw[i] >= 32 && (unsigned char) raw[i] < 127) || (unsigned char) raw[i] >= 0x80)
			clean += raw[i];
	}
	return Utils::String::trim(clean);
}
