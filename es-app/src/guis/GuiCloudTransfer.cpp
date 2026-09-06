#include "guis/GuiCloudTransfer.h"

#include "Window.h"
#include "ThemeData.h"
#include "LocaleES.h"
#include "utils/StringUtil.h"
#include "utils/FileSystemUtil.h"
#include "Log.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <sys/wait.h>

GuiCloudTransfer::GuiCloudTransfer(Window* window, const std::string& command, const std::string& title)
	: GuiComponent(window), mBusyAnim(window), mBackground(window, ":/frame.png"),
	  mCommand(command), mTitleText(title), mFilesThisBlock(0), mPercent(-1),
	  mFinished(false), mExit(-1), mElapsedMs(0), mHandle(nullptr)
{
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
	mTitle    = std::make_shared<TextComponent>(window, Utils::String::toUpper(title), theme->Title.font, theme->Title.color, ALIGN_CENTER);
	mStatus   = std::make_shared<TextComponent>(window, _("PREPARING..."), mTextFont,  theme->Text.color,      ALIGN_CENTER);
	mFileLine = std::make_shared<TextComponent>(window, "",                mSmallFont, theme->TextSmall.color, ALIGN_CENTER);
	mUnit     = std::make_shared<TextComponent>(window, "",                mTextFont,  theme->Text.color,      ALIGN_CENTER);
	mUnitLine = std::make_shared<TextComponent>(window, "",                mSmallFont, theme->TextSmall.color, ALIGN_CENTER);
	mElapsed  = std::make_shared<TextComponent>(window, "",                mSmallFont, theme->TextSmall.color, ALIGN_CENTER);
	mFooter   = std::make_shared<TextComponent>(window, "",                mSmallFont, theme->TextSmall.color, ALIGN_CENTER);

	// One column, 0.78 of the width, every line fitted to it (fitOneLine) so a
	// long ROM name is clipped rather than wrapped into the line beneath -- the
	// overlap this layout replaces came from boxes that grew with their text
	// while their neighbours sat at fixed heights.
	const float w  = SW * 0.78f;
	const float cx = SW * 0.5f;
	mLineWidth = w;
	for (auto& t : { mTitle, mStatus, mFileLine, mUnit, mUnitLine, mElapsed, mFooter })
		t->setSize(w, 0);
	mTitle   ->setPosition(cx - w / 2.0f, SH * 0.20f);
	mStatus  ->setPosition(cx - w / 2.0f, SH * 0.29f);   // 1. the ROM
	mFileLine->setPosition(cx - w / 2.0f, SH * 0.34f);   // 2. its transfer
	mUnit    ->setPosition(cx - w / 2.0f, SH * 0.41f);   // 3. the system
	mUnitLine->setPosition(cx - w / 2.0f, SH * 0.46f);   // 4. the system's transfer
	mBusyAnim.setBackgroundVisible(false);                //   5. the bar
	mBusyAnim.setText("");
	mBusyAnim.setSize(w, SH * 0.05f);
	mBusyAnim.setPosition(cx - w / 2.0f, SH * 0.53f);
	mElapsed ->setPosition(cx - w / 2.0f, SH * 0.61f);   // 6. elapsed
	mFooter  ->setPosition(cx - w / 2.0f, SH * 0.66f);   // 7. the notice

	mPanelSize = Vector2f(w + SW * 0.06f, SH * 0.56f);
	mPanelPos  = Vector2f(cx - mPanelSize.x() / 2.0f, SH * 0.16f);
	mBackground.fitTo(mPanelSize, Vector3f(mPanelPos.x(), mPanelPos.y(), 0), Vector2f(-32, -32));

	mHandle = new std::thread(&GuiCloudTransfer::threadRun, this);
}

GuiCloudTransfer::~GuiCloudTransfer()
{
	if (mHandle != nullptr)
	{
		if (mHandle->joinable())
			mHandle->join();
		delete mHandle;
	}
}

// Input is refused while the transfer runs -- there is nothing to choose, and
// a stray press should not close a page somebody is waiting on. Once it has
// finished any button dismisses it, which is the whole point: the result waits
// for the person rather than the other way round.
bool GuiCloudTransfer::input(InputConfig* config, Input input)
{
	std::unique_lock<std::mutex> lock(mMutex);
	if (!mFinished || !input.value)
		return true;
	lock.unlock();
	delete this;
	return true;
}

std::vector<HelpPrompt> GuiCloudTransfer::getHelpPrompts()
{
	std::vector<HelpPrompt> prompts;
	std::unique_lock<std::mutex> lock(mMutex);
	if (mFinished)
		prompts.push_back(HelpPrompt("b", _("CLOSE")));
	return prompts;
}

void GuiCloudTransfer::render(const Transform4x4f& parentTrans)
{
	Transform4x4f trans = parentTrans * getTransform();
	Renderer::setMatrix(trans);
	// Dim the whole screen: this is a takeover, and the carousel showing
	// through would say the app is idle while a transfer is in flight.
	Renderer::drawRect(0.f, 0.f, mSize.x(), mSize.y(), 0x000000D0);

	auto theme = ThemeData::getMenuTheme();

	// A themed nine-patch panel and nothing else, the same surface a menu or a
	// dialog draws. What separates this page from what it covers is the dim
	// above, not an outline: a stroke of its own would make it the one panel
	// in the app that has one.
	mBackground.render(trans);

	for (auto& t : { mTitle, mStatus, mFileLine, mUnit, mUnitLine, mElapsed, mFooter })
		t->render(trans);

	std::unique_lock<std::mutex> lock(mMutex);
	const bool finished = mFinished;
	const int percent = mPercent;
	lock.unlock();

	if (!finished)
	{
		if (percent >= 0)
		{
			// A bar only where there is a real number behind it. An
			// indeterminate spinner is honest; a bar at an invented
			// position is not.
			const float bw = Renderer::getScreenWidth() * 0.6f;
			const float bx = Renderer::getScreenWidth() * 0.2f;
			const float by = Renderer::getScreenHeight() * 0.595f;
			const float bh = Renderer::getScreenHeight() * 0.014f;
			Renderer::setMatrix(trans);
			Renderer::drawRect(bx, by, bw, bh, (theme->Text.color & 0xFFFFFF00) | 0x40);
			Renderer::drawRect(bx, by, bw * (percent / 100.0f), bh, theme->Text.color);
		}
		else
			mBusyAnim.render(trans);
	}
}

// Clip to one line: a long ROM name gets an ellipsis, never a second line.
std::string GuiCloudTransfer::fitOneLine(const std::shared_ptr<Font>& font, std::string text, float width)
{
	if (!font || text.empty() || font->sizeText(text).x() <= width)
		return text;
	while (text.size() > 4 && font->sizeText(text + "...").x() > width)
		text.pop_back();
	return text + "...";
}

// rclone's fragments in the player's units and separators:
//   "45% /2.5Mi, 300Ki/s, 5s"                      -> "45% OF 2.5 MB · 300 KB/S · 5S LEFT"
//   "1.4 GiB / 2.0 GiB, 70%, 2.5 MiB/s, ETA 3m2s"  -> "1.4 GB OF 2.0 GB · 70% · 2.5 MB/S · 3M2S LEFT"
std::string GuiCloudTransfer::prettyRclone(std::string f)
{
	auto rep = [&f](const std::string& from, const std::string& to) { f = Utils::String::replace(f, from, to); };
	rep("GiB", "GB"); rep("MiB", "MB"); rep("KiB", "KB");
	rep("Gi", " GB"); rep("Mi", " MB"); rep("Ki", " KB");
	rep("ETA ", "");
	rep(" / ", " OF "); rep(" /", " OF ");
	rep(", ", " · ");
	f = Utils::String::toUpper(f);
	// a trailing duration -- digits then a unit letter, no percent, no bytes -- is time left
	size_t sep = f.rfind(" · ");
	std::string last = sep == std::string::npos ? f : f.substr(sep + 3);
	if (!last.empty() && isdigit((unsigned char) last[0]) && last.find('%') == std::string::npos
		&& last.find('B') == std::string::npos && !isdigit((unsigned char) last.back()))
		f += " " + std::string(_("LEFT"));
	return f;
}

void GuiCloudTransfer::update(int deltaTime)
{
	GuiComponent::update(deltaTime);
	mBusyAnim.update(deltaTime);
	std::unique_lock<std::mutex> lock(mMutex);
	if (!mFinished)
		mElapsedMs += deltaTime;
	const int mins = mElapsedMs / 60000;
	const int secs = (mElapsedMs / 1000) % 60;
	char elapsed[32];
	snprintf(elapsed, sizeof(elapsed), "%d:%02d", mins, secs);

	// 3 and 4: the system (or phase) and its totals -- shown in both states.
	std::string unit = Utils::String::toUpper(mUnitLabel);
	if (!unit.empty() && !mUnitIndex.empty() && !mUnitCount.empty())
		unit += "   " + mUnitIndex + " " + std::string(_("OF")) + " " + mUnitCount;
	std::string unitLine;
	if (!mFilesTotals.empty())
	{
		// "12 / 45, 27%" -> "12 OF 45 FILES · 27%"
		auto pct = mFilesTotals.find(", ");
		unitLine = Utils::String::replace(mFilesTotals.substr(0, pct), " / ", " " + std::string(_("OF")) + " ") + " " + std::string(_("FILES"));
		if (pct != std::string::npos)
			unitLine += " · " + mFilesTotals.substr(pct + 2);
	}
	if (!mTotals.empty())
		unitLine += (unitLine.empty() ? "" : "   ") + prettyRclone(mTotals);
	mUnit    ->setText(fitOneLine(mTextFont,  unit,     mLineWidth));
	mUnitLine->setText(fitOneLine(mSmallFont, unitLine, mLineWidth));

	if (mFinished)
	{
		mStatus->setText(mExit == 0 ? _("COMPLETED SUCCESSFULLY")
			: mExit == 130 ? _("STOPPED")
			: mExit == 3 ? _("SKIPPED - ANOTHER CLOUD SYNC IS RUNNING")
			: _("FAILED"));
		mFileLine->setText("");
		mElapsed ->setText(std::string(_("ELAPSED")) + " " + elapsed);
		mFooter  ->setText(_("PRESS ANY BUTTON TO CLOSE"));
	}
	else
	{
		// 1 and 2: the file it is on right now is the line that says it is
		// alive -- a thousand small BIOS files spend minutes between percentage
		// changes, and a frozen percentage is indistinguishable from a hang.
		if (mCurrent.empty())
		{
			mStatus  ->setText(mUnitLabel.empty() ? _("PREPARING...") : _("WORKING..."));
			mFileLine->setText("");
		}
		else
		{
			mStatus->setText(fitOneLine(mTextFont, mCurrent, mLineWidth));
			std::string fl = std::string(_("TRANSFERRING"));
			if (!mFileProgress.empty())
				fl += "  " + prettyRclone(mFileProgress);
			if (mFilesThisBlock > 1)
				fl += "  · " + std::string(_("AND")) + " " + std::to_string(mFilesThisBlock - 1) + " " + std::string(_("MORE"));
			mFileLine->setText(fitOneLine(mSmallFont, fl, mLineWidth));
		}
		mElapsed->setText(std::string(_("ELAPSED")) + " " + elapsed);
		mFooter ->setText(_("THIS CAN TAKE A WHILE. YOU CAN LEAVE IT RUNNING."));
	}
}

void GuiCloudTransfer::handleLine(const std::string& line)
{
	std::unique_lock<std::mutex> lock(mMutex);

	// "Transferred:   \t 1.4 GiB / 2.0 GiB, 70%, 2.5 MiB/s, ETA 3m2s"
	//
	// It appears twice per block: once for bytes, once for the file count
	// ("0 / 6, 0%"). The byte one is the one carrying a unit, which is also
	// the one somebody wants -- a count of files says nothing about how long
	// this will take when the files are a save game and a disc image.
	// ">>> unit nes|2|5" -- the script announces each system (or phase) as it
	// starts. Everything per-block is reset with it; the label survives.
	if (line.rfind(">>> unit ", 0) == 0)
	{
		auto parts = Utils::String::split(line.substr(9), '|', false);
		mUnitLabel = parts.size() > 0 ? Utils::String::trim(parts[0]) : "";
		mUnitIndex = parts.size() > 1 ? Utils::String::trim(parts[1]) : "";
		mUnitCount = parts.size() > 2 ? Utils::String::trim(parts[2]) : "";
		mCurrent.clear(); mFileProgress.clear(); mTotals.clear(); mFilesTotals.clear();
		mFilesThisBlock = 0; mPercent = -1;
		return;
	}
	if (line.rfind("Transferred:", 0) == 0)
	{
		std::string body = Utils::String::trim(line.substr(12));
		if (body.find('/') == std::string::npos)
			return;
		if (body.find("iB") == std::string::npos && body.find(" B") == std::string::npos)
		{
			mFilesTotals = body;   // the count line of the block: "12 / 45, 27%"
			return;
		}

		mTotals = body;
		mFilesThisBlock = 0;   // a new block: the next " * " line is the head of it

		auto pp = body.find('%');
		if (pp != std::string::npos && pp > 0)
		{
			size_t st = pp;
			while (st > 0 && isdigit((unsigned char) body[st - 1]))
				st--;
			if (st < pp)
			{
				int v = atoi(body.substr(st, pp - st).c_str());
				if (v >= 0 && v <= 100)
					mPercent = v;
			}
		}
		return;
	}

	// " *   Some Game.zip: 45% /2.5Mi, 300Ki/s, 5s" -- one per parallel
	// transfer, four by default. The first names what to show; the rest are
	// counted, because "and 3 more" is the difference between a device that
	// looks stalled on one file and one that is saturating the link.
	if (!line.empty() && line[0] == '*')
	{
		if (mFilesThisBlock == 0)
		{
			// "name.zip: 45% /2.5Mi, 300Ki/s, 5s" -- the name and its progress
			// arrive on one line; shown on two, so neither wraps.
			std::string body = Utils::String::trim(line.substr(1));
			auto sep = body.rfind(": ");
			std::string name = sep == std::string::npos ? body : body.substr(0, sep);
			mFileProgress = sep == std::string::npos ? "" : Utils::String::trim(body.substr(sep + 2));
			mCurrent = Utils::FileSystem::getFileName(name);
		}
		mFilesThisBlock++;
		return;
	}
}

void GuiCloudTransfer::threadRun()
{
	int ret = -1;
	// Braces around the whole command, not just " 2>&1" after it. The command
	// is a sequence, and a trailing redirection binds to its last element only
	// -- so everything the earlier tiers wrote to stderr went to the ES
	// process's own stderr and never reached this page.
	FILE* pipe = popen(("{ " + mCommand + " ; } 2>&1").c_str(), "r");
	if (pipe != nullptr)
	{
		// Read a character at a time, and treat three things as ending a line:
		// \n, \r, and the string "Transferred:" appearing mid-line.
		//
		// The third is not defensive programming, it is the observed format.
		// Piped (there is no terminal here), rclone ends each redraw after the
		// last " * file" line WITHOUT a newline, so the next block's
		// "Transferred:" is glued onto it:
		//
		//   * f4.bin: 26% / 3.8 MiB, 507 KiB/sTransferred: 6.1 MiB / 22.8 MiB...
		//
		// Splitting on newlines alone yields one line that is neither a file
		// line nor a totals line, and both halves are lost -- every block after
		// the first. \r is handled because a terminal-attached run does use it.
		std::string buf;
		int c;
		while ((c = fgetc(pipe)) != EOF)
		{
			if (c != '\n' && c != '\r')
			{
				if (buf.size() < 1024)
					buf += (char) c;

				static const std::string MARK = "Transferred:";
				if (buf.size() > MARK.size()
					&& buf.compare(buf.size() - MARK.size(), MARK.size(), MARK) == 0)
				{
					handleLine(cleanLine(buf.substr(0, buf.size() - MARK.size())));
					buf = MARK;
				}
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
}

// Drop ANSI escapes and anything unprintable, then trim. A terminal-attached
// rclone moves the cursor to redraw in place; those sequences are instructions
// to a terminal that is not here.
std::string GuiCloudTransfer::cleanLine(const std::string& raw)
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
		if ((unsigned char) raw[i] >= 32 && (unsigned char) raw[i] < 127)
			clean += raw[i];
	}
	return Utils::String::trim(clean);
}
