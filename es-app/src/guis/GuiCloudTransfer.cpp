#include "guis/GuiCloudTransfer.h"

#include "Window.h"
#include "ThemeData.h"
#include "LocaleES.h"
#include "utils/StringUtil.h"
#include "utils/FileSystemUtil.h"
#include "Log.h"
#include "SystemData.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sys/wait.h>

GuiCloudTransfer::GuiCloudTransfer(Window* window, const std::string& command, const std::string& title)
	: GuiComponent(window), mBusyAnim(window, ""), mBackground(window, ":/frame.png"),
	  mCommand(command), mTitleText(title),
	  mUnitBytes(0), mUnitFiles(0), mRunBytes(0), mRunFiles(0), mRunSized(false),
	  mRemovedFiles(0), mRemovedBytes(0), mAnyTransferred(false),
	  mFilesThisBlock(0), mSeenBlock(false),
	  mChecksDone(0), mChecksTotal(0), mListed(0), mChecksThisBlock(0),
	  mBytePercent(-1), mFilePercent(-1), mCheckPercent(-1), mPercent(-1),
	  mFinished(false), mExit(-1), mShownFinished(false), mShownPercent(-1),
	  mElapsedMs(0), mHandle(nullptr)
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
	mNote     = std::make_shared<TextComponent>(window, "",                mSmallFont, theme->TextSmall.color, ALIGN_CENTER);
	mElapsed  = std::make_shared<TextComponent>(window, "",                mSmallFont, theme->TextSmall.color, ALIGN_CENTER);
	mFooter   = std::make_shared<TextComponent>(window, "",                mSmallFont, theme->TextSmall.color, ALIGN_CENTER);

	// One column, 0.78 of the width, every line fitted to it (fitOneLine) so a
	// long ROM name is clipped rather than wrapped into the line beneath -- the
	// overlap this layout replaces came from boxes that grew with their text
	// while their neighbours sat at fixed heights.
	const float w  = SW * 0.78f;
	const float cx = SW * 0.5f;
	const float x  = cx - w / 2.0f;
	mLineWidth = w;
	for (auto& t : { mTitle, mStatus, mFileLine, mUnit, mUnitLine, mNote, mElapsed, mFooter })
		t->setSize(w, 0);

	// Rows are stacked from the fonts' own heights, not from a table of
	// fractions. A TextComponent given (w, 0) is exactly font->getHeight()
	// tall -- 1.5 x its tallest glyph -- and the menu fonts are the theme's:
	// the shipped theme sets the title at 0.029 of the screen where ES's
	// default is 0.085, so a table that clears the default is three times too
	// loose for what ships. Fonts scale with min(W, H), so every row here is
	// screen-relative all the same; the fractions below are what the sums
	// come to.
	//
	//   pad  0.05 SH   above the title and below the footer. Equal: the
	//                  footer used to sit on the panel's bottom edge with
	//                  0.04 above the title (maintainer, 2026-09-08).
	//   gap  0.02 SH   the unit of space between groups
	//
	//   title                          hT
	//   1.5 gap
	//   1 the file    2 its line       hM + hS   a tight pair
	//   gap
	//   3 the system  4 its line       hM + hS   a tight pair
	//   0.5 gap                                  the bar is the system's, so it sits close
	//   5 bar / spinner / done-note    hM        one row, whichever of the three is showing
	//   1.5 gap                                  elapsed is the whole run's, so the gap opens beneath the bar
	//   6 elapsed                      hS
	//   gap
	//   7 footer                       hS
	//
	//   H = 2 pad + hT + 3 hM + 4 hS + 5.5 gap
	//
	// Shipped theme (Roboto Bold; title 0.029, text 0.033, small 0.025 of
	// min(W, H), x1.31 menu scale under 720 px):
	//   640x480    hT 28.5  hM 30    hS 24    pad 24  gap 9.6   H 315 = 0.66 SH, top 0.17
	//   1280x800   hT 37.5  hM 40.5  hS 31.5  pad 40  gap 16    H 453 = 0.57 SH
	//   1920x1080  hT 48    hM 54    hS 42    pad 54  gap 21.6  H 605 = 0.56 SH
	// ES defaults (ubuntu condensed 0.085 / 0.045 / 0.035), for a theme that
	// sets no menu fonts:
	//   640x480    hT 76.5  hM 42    hS 30                      H 423 = 0.88 SH
	//   1920x1080  hT 135   hM 70.5  hS 55.5                    H 795 = 0.74 SH
	// That is the tallest a real theme makes it. Past 0.9 SH every pitch is
	// scaled down together; a glyph is two thirds of its row, so rows stay
	// apart down to a factor of 0.67, further than any theme pushes it.
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
	mStatus  ->setPosition(x, y);          // 1. the file
	mFileLine->setPosition(x, y + rM);     // 2. its transfer
	y += rM + rS + gap;
	mUnit    ->setPosition(x, y);          // 3. the system
	mUnitLine->setPosition(x, y + rM);     // 4. the system's transfer
	y += rM + rS + 0.5f * gap;
	// 5. The bar, the spinner and the done-note share one row, centred on it.
	// The bar used to be drawn a row below the spinner it replaced, so the
	// page's centre of gravity moved every time the percentage came and went.
	// No caption on the spinner: line 1 already says WORKING..., and the
	// caption is set at construction because BusyComponent::setText("") is a
	// no-op against its empty initial state -- the default WORKING... showed
	// beside the spinner, twice on one screen (maintainer, 2026-09-06).
	mBusyAnim.setBackgroundVisible(false);
	mBusyAnim.setSize(w, rM);
	mBusyAnim.setPosition(x, y);
	mBarW = SW * 0.6f;
	mBarH = SH * 0.014f;
	mBarX = cx - mBarW / 2.0f;
	mBarY = y + (rM - mBarH) / 2.0f;
	// Once the run is over the bar's row is free, and it is where the one
	// thing left to do goes: a content run that changed the ROMs on this
	// device is not visible in the game lists until they are rebuilt, and
	// nobody should have to know that (maintainer, 2026-09-07).
	mNote    ->setPosition(x, y + (rM - hS) / 2.0f);
	y += rM + 1.5f * gap;
	mElapsed ->setPosition(x, y);          // 6. elapsed
	y += rS + gap;
	mFooter  ->setPosition(x, y);          // 7. the notice; y + rS + pad == top + H

	mPanelSize = Vector2f(w + SW * 0.06f, H);
	mPanelPos  = Vector2f(cx - mPanelSize.x() / 2.0f, top);
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
	// A restore may have brought files into folders the lists scanned at
	// boot -- screenshots in particular (#82). Re-read what changed once this
	// page is gone.
	const bool restored = mExit == 0 && mCommand.find("restore") != std::string::npos;
	lock.unlock();
	Window* window = mWindow;
	delete this;
	if (restored)
		window->postToUiThread([] { SystemData::rescanChangedFolders(); });
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

	for (auto& t : { mTitle, mStatus, mFileLine, mUnit, mUnitLine, mNote, mElapsed, mFooter })
		t->render(trans);

	// The bar is drawn from the snapshot update() took under the lock, along
	// with the text above it -- not from mPercent, which the worker may have
	// moved on since. One block per frame, for every row and the bar alike.
	if (!mShownFinished)
	{
		if (mShownPercent >= 0)
		{
			// A bar only where there is a real number behind it. An
			// indeterminate spinner is honest; a bar at an invented
			// position is not. The rectangle is the constructor's, on the
			// spinner's row, so the two take turns in one place.
			Renderer::setMatrix(trans);
			Renderer::drawRect(mBarX, mBarY, mBarW, mBarH, (theme->Text.color & 0xFFFFFF00) | 0x40);
			Renderer::drawRect(mBarX, mBarY, mBarW * (mShownPercent / 100.0f), mBarH, theme->Text.color);
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

// rclone's size units, once: how each is spelt in its output (the torn
// "Ki"/"Mi"/"Gi" is a per-file line cut at 80 columns), what the page calls
// it, and how many bytes it is. prettyRclone renames by this table and
// parseBytes reads by it, so a unit the page can show is a unit it can add
// up. Longest spelling first: "GiB" must be matched before "Gi".
namespace
{
	struct RcloneUnit { const char* rclone; const char* shown; double bytes; };
	const RcloneUnit RCLONE_UNITS[] = {
		{ "TiB", "TB",  1024.0 * 1024 * 1024 * 1024 },
		{ "GiB", "GB",  1024.0 * 1024 * 1024 },
		{ "MiB", "MB",  1024.0 * 1024 },
		{ "KiB", "KB",  1024.0 },
		{ "Ti",  " TB", 1024.0 * 1024 * 1024 * 1024 },
		{ "Gi",  " GB", 1024.0 * 1024 * 1024 },
		{ "Mi",  " MB", 1024.0 * 1024 },
		{ "Ki",  " KB", 1024.0 },
		{ "B",   "B",   1.0 },
	};
}

// "200 KB", "1.2 MB", "1.20 GB": a whole KB below a megabyte, one decimal
// below a gigabyte, two above. That is the precision a listing has and the
// precision a player can act on. kiloBytesToString prints two decimals of
// whatever unit it lands on, and fed a size already rounded up to a whole
// KB it read "200.00 KB" -- two digits that could only ever be zero (#85).
// A size that is not zero rounds up, so one byte reads "1 KB", never "0 KB".
// The unit is chosen from the value as it will print, so nothing reads
// "1024 KB" or "1024.0 MB" a byte short of the next unit.
std::string GuiCloudTransfer::sizeLabel(unsigned long bytes)
{
	char buf[32];
	const unsigned long kb = (bytes + 1023UL) / 1024UL;
	const double mb = bytes / (1024.0 * 1024.0);
	if (kb < 1024UL)
		snprintf(buf, sizeof(buf), "%lu KB", kb);
	else if (mb < 1023.95)
		snprintf(buf, sizeof(buf), "%.1f MB", mb);
	else
		snprintf(buf, sizeof(buf), "%.2f GB", bytes / (1024.0 * 1024.0 * 1024.0));
	return buf;
}

// The bytes in one rclone size field: "80 KiB" -> 81920, "1.4 GiB" ->
// 1503238553, "0 B" -> 0. -1 when the field carries no number or a unit
// the table above does not know: a value that was not printed is never
// added to a total that will be shown. strtod also reads "inf" and "nan",
// which no size is and whose cast to long is undefined; rclone never prints
// them, and they are refused all the same.
long GuiCloudTransfer::parseBytes(const std::string& field)
{
	const std::string t = Utils::String::trim(field);
	char* end = nullptr;
	const double v = strtod(t.c_str(), &end);
	if (end == t.c_str() || !std::isfinite(v) || v < 0)
		return -1;
	const std::string unit = Utils::String::trim(std::string(end));
	for (const auto& u : RCLONE_UNITS)
		if (unit == u.rclone)
			return (long) (v * u.bytes + 0.5);
	return -1;
}

// The unit's last "Transferred:" pair becomes the run's. Called with mMutex
// held: at the next ">>> unit", when the command exits, and when the byte
// counter drops -- rclone's never does within one invocation, so a drop
// means a second one started inside the unit, and what the first moved is
// banked before its numbers are replaced. A drop of the count line folds
// the files alone, in handleLine: the byte line of the same block comes
// first and has settled the bytes by then, and folding both again here
// banked the new rclone's first bytes twice whenever the old one had moved
// nothing but empty files (review, 2026-09-08).
void GuiCloudTransfer::foldUnit()
{
	mRunBytes += mUnitBytes;
	mRunFiles += mUnitFiles;
	mUnitBytes = 0;
	mUnitFiles = 0;
}

// rclone's fragments in the player's units and separators:
//   "45% /2.5Mi, 300Ki/s, 5s"                      -> "45% OF 2.5 MB . 300 KB/S . 5S LEFT"
//   "1.4 GiB / 2.0 GiB, 70%, 2.5 MiB/s, ETA 3m2s"  -> "1.4 GB OF 2.0 GB . 70% . 2.5 MB/S . 3M2S LEFT"
std::string GuiCloudTransfer::prettyRclone(std::string f)
{
	// Piped -- there is no terminal here -- rclone cuts every per-file line
	// at 80 columns, so the last field often arrives torn: "5.722 MiB/",
	// "976.547 Ki". A field is a percentage, a size (ends in B), a speed
	// (ends in /s), or a time (digits and h/m/s); anything else is a
	// fragment and is dropped rather than shown as "5.722 MB/". A bare "-"
	// is rclone's word for a value it does not have yet -- the ETA of a
	// transfer that has not started, the percentage of nothing -- and is
	// dropped too: it is honest, but on the page it read as broken
	// ("0 B OF 0 B . - . 0 B/S . -", review 2026-09-08).
	{
		std::vector<std::string> kept;
		for (auto& raw : Utils::String::split(f, ',', true))
		{
			std::string t = Utils::String::trim(raw);
			if (t.empty()) continue;
			std::string tail = t;
			if (tail.rfind("ETA ", 0) == 0) tail = tail.substr(4);
			const bool pct   = tail.back() == '%';
			const bool size  = tail.back() == 'B' || (tail.size() > 2 && tail.compare(tail.size() - 2, 2, "iB") == 0);
			const bool speed = tail.size() > 2 && tail.compare(tail.size() - 2, 2, "/s") == 0;
			bool time = !tail.empty() && isdigit((unsigned char) tail[0]);
			for (char ch : tail) if (!(isdigit((unsigned char) ch) || ch == 'h' || ch == 'm' || ch == 's')) { time = false; break; }
			if (time && isdigit((unsigned char) tail.back())) time = false;
			// the first field may be "45% /2.5Mi" -- a percentage and a size in one
			const bool pctSize = t.find('%') != std::string::npos && t.find('/') != std::string::npos && (size || t.back() == 'i');
			if (pct || size || speed || time || pctSize)
				kept.push_back(t);
		}
		f.clear();
		for (size_t i = 0; i < kept.size(); i++)
			f += (i ? ", " : "") + kept[i];
	}
	auto rep = [&f](const std::string& from, const std::string& to) { f = Utils::String::replace(f, from, to); };
	for (const auto& u : RCLONE_UNITS)
		if (std::string(u.rclone) != u.shown)
			rep(u.rclone, u.shown);
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
	// One snapshot per frame for the bar (render() draws from it) and the
	// rows below, so they cannot show two different stats blocks.
	mShownFinished = mFinished;
	mShownPercent  = mPercent;
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
		// "12 / 45, 27%" -> "12 OF 45 FILES . 27%"
		auto pct = mFilesTotals.find(", ");
		unitLine = Utils::String::replace(mFilesTotals.substr(0, pct), " / ", " " + std::string(_("OF")) + " ") + " " + std::string(_("FILES"));
		if (pct != std::string::npos)
			unitLine += " · " + mFilesTotals.substr(pct + 2);
	}
	// "0 B / 0 B, -, 0 B/s, ETA -" is the byte line while nothing is queued
	// to move -- the whole of a run that only compares -- and it is true of
	// nothing anybody asked about. Line 2 says what such a run is doing;
	// this line stays blank rather than read "0 B OF 0 B . 0 B/S".
	if (!mTotals.empty() && mTotals.rfind("0 B / 0 B", 0) != 0)
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
		const bool restore = mCommand.find("restore") != std::string::npos;
		if (mRemovedFiles > 0)
		{
			// A match is mostly deletion, and rclone's totals for a deletion
			// are "0 B / 0 B" -- true and useless. Lines 3 and 4 carry what the
			// confirmation showed instead: what went, per system.
			std::string removed = std::string(_("REMOVED")) + " " + std::to_string(mRemovedFiles) + " "
				+ std::string(mRemovedFiles == 1 ? _("FILE FROM THIS DEVICE") : _("FILES FROM THIS DEVICE"));
			if (mRemovedBytes > 0)
				removed += " · " + sizeLabel(mRemovedBytes);
			mUnit->setText(fitOneLine(mTextFont, removed, mLineWidth));
			std::string detail;
			for (auto& d : mRemovedDetail)
				detail += (detail.empty() ? "" : "   ") + d;
			mUnitLine->setText(fitOneLine(mSmallFont, detail, mLineWidth));
			if (mAnyTransferred)
				mFileLine->setText(fitOneLine(mSmallFont, _("FILES YOUR CLOUD HAD AND THIS DEVICE DID NOT WERE DOWNLOADED TOO."), mLineWidth));
		}
		else
		{
			// Lines 3 and 4 answer for the whole run, not its last unit: the
			// page used to end on "SNES  3 OF 3" over that unit's totals, or
			// over nothing when the last unit only compared (#85). Line 3
			// carries the sum of every unit's final "Transferred:" pair, in
			// the run's own verb -- the row and the font the match branch
			// above gives its own summary, so the run's answer to "did that
			// work?" is not the smallest text on the page under a blank row
			// (review, 2026-09-08). Line 4 is left clear: a system's name
			// over a run-wide number would claim the number was its. Only
			// what rclone printed: a run that never printed a byte line says
			// COMPLETED SUCCESSFULLY and nothing more.
			//
			// The count is of finished files. The bytes are rclone's
			// bytes-read counter, and on a run that stopped or failed that
			// includes what the transfers in flight had read when it died
			// and never completed -- so a run that did not exit 0 names its
			// files and no size, rather than claim as BACKED UP bytes that
			// were not.
			std::string summary;
			const bool sized = mExit == 0 && mRunBytes > 0;
			if (mRunSized && (mRunFiles > 0 || sized))
			{
				if (mRunFiles > 0)
					summary = std::to_string(mRunFiles) + " " + std::string(mRunFiles == 1 ? _("FILE") : _("FILES"));
				if (sized)
					summary += (summary.empty() ? "" : " · ") + sizeLabel((unsigned long) mRunBytes);
				summary += " " + std::string(restore ? _("RESTORED") : _("BACKED UP"));
			}
			else if (mRunSized && mExit == 0)
			{
				// Nothing moved and the run succeeded: everything was there
				// already. On a failure the same zero means something else,
				// so the sentence is not offered. The clause after the dash
				// is dropped whole on a panel too narrow for it, rather than
				// ending in an ellipsis -- measured in the font it is set in.
				summary = restore ? _("NOTHING NEW TO RECEIVE - EVERYTHING WAS ALREADY ON THIS DEVICE")
				                  : _("NOTHING NEW TO SEND - EVERYTHING WAS ALREADY IN YOUR CLOUD");
				if (mTextFont && mTextFont->sizeText(summary).x() > mLineWidth)
					summary = restore ? _("NOTHING NEW TO RECEIVE") : _("NOTHING NEW TO SEND");
			}
			mUnit    ->setText(fitOneLine(mTextFont, summary, mLineWidth));
			mUnitLine->setText("");
		}
		// The ROMs on this device changed: the game lists do not know until
		// they are rebuilt. Says where, in the words of the row that does it.
		const bool contentRun = mCommand.find("cloud_content_restore") != std::string::npos;
		if (mExit == 0 && contentRun && (mRemovedFiles > 0 || mAnyTransferred))
			mNote->setText(fitOneLine(mSmallFont, _("UPDATE GAMELISTS UNDER GAME SETTINGS TO SEE THE CHANGE."), mLineWidth));
		mElapsed ->setText(std::string(_("ELAPSED")) + " " + elapsed);
		mFooter  ->setText(_("PRESS ANY BUTTON TO CLOSE"));
	}
	else
	{
		// 1 and 2: the file it is on right now is the line that says it is
		// alive -- a thousand small BIOS files spend minutes between percentage
		// changes, and a frozen percentage is indistinguishable from a hang.
		if (!mCurrent.empty())
		{
			mStatus->setText(fitOneLine(mTextFont, mCurrent, mLineWidth));
			// "TRANSFERRING 45% OF 2.5 MB . 300 KB/S . AND 3 MORE FILES": a
			// single space inside a segment and " . " between them, the same
			// as every other row. Two spaces read as a gap twice the width of
			// the word gaps beside it (#85).
			std::string fl = std::string(_("TRANSFERRING"));
			if (!mFileProgress.empty())
				fl += " " + prettyRclone(mFileProgress);
			if (mFilesThisBlock > 1)
				fl += " · " + std::string(_("AND")) + " " + std::to_string(mFilesThisBlock - 1) + " " + std::string(_("MORE FILES"));
			mFileLine->setText(fitOneLine(mSmallFont, fl, mLineWidth));
		}
		else if (mChecksTotal > 0 || mListed > 0)
		{
			// Nothing in flight, plenty happening: rclone is comparing what
			// is here with what is there, and for a device whose saves are
			// all in the cloud already that is the whole run. It prints no
			// per-file line for a comparison, so this is the count it does
			// print -- and the name, when it caught one mid-comparison. A
			// run that showed neither looked hung until it said COMPLETED
			// (maintainer, 2026-09-08). Before anything is queued to compare
			// the only count is what rclone has listed, and that counts both
			// sides -- 40 saves list as 80 -- so it is not shown as a number
			// the player would try to reconcile with their files; the spinner
			// on row 5 is the sign of life until the first check is queued.
			mStatus->setText(mChecking.empty() ? _("WORKING...") : fitOneLine(mTextFont, mChecking, mLineWidth));
			std::string fl;
			if (mChecksTotal > 0)
				fl = std::string(_("CHECKING")) + " " + std::to_string(mChecksDone) + " " + std::string(_("OF")) + " "
					+ std::to_string(mChecksTotal) + " " + std::string(_("FILES"));
			else
				fl = _("CHECKING FILES...");
			mFileLine->setText(fitOneLine(mSmallFont, fl, mLineWidth));
		}
		else
		{
			mStatus  ->setText(mUnitLabel.empty() ? _("PREPARING...") : _("WORKING..."));
			mFileLine->setText("");
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
	if (line.rfind(">>> removed ", 0) == 0)
	{
		auto parts = Utils::String::split(line.substr(12), '|', false);
		mRemovedFiles = parts.size() > 0 ? atol(Utils::String::trim(parts[0]).c_str()) : 0;
		mRemovedBytes = parts.size() > 1 ? atol(Utils::String::trim(parts[1]).c_str()) : 0;
		mRemovedDetail.clear();
		if (parts.size() > 2)
		{
			for (auto& item : Utils::String::split(Utils::String::trim(parts[2]), ',', true))
			{
				auto f = Utils::String::split(item, ':', false);
				if (f.size() < 2)
					continue;
				const std::string n = Utils::String::trim(f[1]);
				std::string d = Utils::String::toUpper(Utils::String::trim(f[0])) + " " + n + " " + std::string(n == "1" ? _("FILE") : _("FILES"));
				long b = f.size() > 2 ? atol(Utils::String::trim(f[2]).c_str()) : 0;
				if (b > 0)
					d += " · " + sizeLabel((unsigned long) b);
				mRemovedDetail.push_back(d);
			}
		}
		return;
	}
	if (line.rfind(">>> unit ", 0) == 0)
	{
		foldUnit();   // the unit that just ended: its last totals are the run's now
		auto parts = Utils::String::split(line.substr(9), '|', false);
		mUnitLabel = parts.size() > 0 ? Utils::String::trim(parts[0]) : "";
		mUnitIndex = parts.size() > 1 ? Utils::String::trim(parts[1]) : "";
		mUnitCount = parts.size() > 2 ? Utils::String::trim(parts[2]) : "";
		mCurrent.clear(); mFileProgress.clear(); mTotals.clear(); mFilesTotals.clear(); mChecking.clear();
		mFilesThisBlock = 0; mChecksThisBlock = 0; mSeenBlock = false;
		mChecksDone = 0; mChecksTotal = 0; mListed = 0;
		mBytePercent = -1; mFilePercent = -1; mCheckPercent = -1; mPercent = -1;
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
			mFilePercent = parsePercent(body);
			refreshPercent();
			// "12 / 45" -- twelve files done so far in this unit; it only
			// grows, so a smaller number is a new rclone inside the unit.
			// Files only -- the bytes were settled by this block's byte line,
			// which comes first (foldUnit).
			const long files = atol(body.c_str());
			if (files < mUnitFiles)
				mRunFiles += mUnitFiles;
			mUnitFiles = files;
			return;
		}

		mTotals = body;
		if (body.rfind("0 B /", 0) != 0)
			mAnyTransferred = true;
		// "80 KiB / 300 KiB" -- what this unit has moved so far, read from the
		// first field. Recorded only when it parsed; a byte line that was
		// seen at all is what lets the done page show any number.
		const long bytes = parseBytes(body.substr(0, body.find('/')));
		if (bytes >= 0)
		{
			mRunSized = true;
			if (bytes < mUnitBytes)
				foldUnit();
			mUnitBytes = bytes;
		}
		// A block that carried no per-file line had nothing in flight -- the
		// unit's files are done or being checked -- so the name row does not
		// keep showing a file that finished a block ago. The same for a name
		// caught mid-comparison.
		if (mSeenBlock && mFilesThisBlock == 0)
		{
			mCurrent.clear();
			mFileProgress.clear();
		}
		if (mSeenBlock && mChecksThisBlock == 0)
			mChecking.clear();
		mSeenBlock = true;
		mFilesThisBlock = 0;    // a new block: the next " * " line is the head of it
		mChecksThisBlock = 0;

		// Each percentage is its own line's last word, and a "-" parses to
		// -1, so a line that prints no number clears its own value. The
		// other two are not reset here: rclone prints the Checks and the
		// count lines in every block once it has printed them at all (their
		// counters only grow), so a value left standing is one it is about
		// to overwrite a few microseconds on -- and resetting it made the
		// bar read the bytes alone for that instant, 100% over a run still
		// comparing. The one on screen is left as it is until this block
		// has produced a number: a bar that fell back to the spinner for
		// the instant between two lines would flicker once a second.
		mBytePercent = parsePercent(body);
		refreshPercent();
		return;
	}
	// "Checks:                12 / 45, 27%, Listed 300" -- what rclone has
	// compared rather than moved, and how much of both sides it has listed.
	// Before anything is queued to compare it reads "0 / 0, -, Listed 300"
	// (rclone 1.75 prints the line once checks, their total, or the listing
	// is non-zero), so the listing count is the first sign of life on a run
	// against a large remote. Older rclones end the line at the percentage.
	if (line.rfind("Checks:", 0) == 0)
	{
		std::string body = Utils::String::trim(line.substr(7));
		auto slash = body.find(" / ");
		if (slash == std::string::npos)
			return;
		mChecksDone  = atol(body.substr(0, slash).c_str());
		mChecksTotal = atol(body.substr(slash + 3).c_str());
		auto listed = body.find("Listed ");
		if (listed != std::string::npos)
			mListed = atol(body.substr(listed + 7).c_str());
		mCheckPercent = parsePercent(body);
		refreshPercent();
		return;
	}

	// " *   Some Game.zip: 45% /2.5Mi, 300Ki/s, 5s" -- one per parallel
	// transfer, four by default. The first names what to show; the rest are
	// counted, because "and 3 more" is the difference between a device that
	// looks stalled on one file and one that is saturating the link.
	if (!line.empty() && line[0] == '*')
	{
		std::string body = Utils::String::trim(line.substr(1));

		// " *   name: checking" -- a file rclone caught mid-comparison, under
		// its "Checking:" heading. Not a transfer: it is the name for the
		// CHECKING line, and counting it would promise a file that never
		// moves. " *   name: transferring" is one that is queued with no
		// bytes yet, so there is no percentage to split on; the name is
		// real and the progress is empty.
		static const std::string CHECKING = ": checking";
		static const std::string QUEUED   = ": transferring";
		if (body.size() > CHECKING.size() && body.compare(body.size() - CHECKING.size(), CHECKING.size(), CHECKING) == 0)
		{
			mChecking = Utils::FileSystem::getFileName(Utils::String::trim(body.substr(0, body.size() - CHECKING.size())));
			mChecksThisBlock++;
			return;
		}
		if (body.size() > QUEUED.size() && body.compare(body.size() - QUEUED.size(), QUEUED.size(), QUEUED) == 0)
			body = Utils::String::trim(body.substr(0, body.size() - QUEUED.size()));

		if (mFilesThisBlock == 0)
		{
			// "name.zip: 45% /2.5Mi, 300Ki/s, 5s" -- the name and its progress
			// arrive on one line; shown on two, so neither wraps.
			//
			// rclone prints the percentage as %3d, so the separator is ": " at
			// 45% and ":" at 100% ("name.zip:100% /40Mi, 1Mi/s, 0s"). Splitting
			// on ": " put the whole line on the name row at 100%, and the
			// basename of "1Mi/s, 0s" is "s" -- the file the maintainer saw
			// called "S". The separator is the last ':' followed by a
			// percentage; a name may contain ':' but not ':' + digits + '%'.
			size_t sep = std::string::npos;
			for (size_t i = body.size(); i-- > 0; )
			{
				if (body[i] != ':')
					continue;
				size_t j = i + 1;
				while (j < body.size() && body[j] == ' ')
					j++;
				size_t d = j;
				while (d < body.size() && isdigit((unsigned char) body[d]))
					d++;
				if (d > j && d < body.size() && body[d] == '%')
				{
					sep = i;
					break;
				}
			}
			std::string name = sep == std::string::npos ? body : body.substr(0, sep);
			mFileProgress = sep == std::string::npos ? "" : Utils::String::trim(body.substr(sep + 1));
			mCurrent = Utils::FileSystem::getFileName(Utils::String::trim(name));
		}
		mFilesThisBlock++;
		return;
	}
}

// The first percentage in an rclone stats field: "12 / 45, 27%, Listed 300"
// gives 27; "0 B / 0 B, -, 0 B/s, ETA -" gives -1. Only a number that was
// printed is ever returned -- the caller shows a spinner for -1 rather than
// a bar at a position nobody measured.
int GuiCloudTransfer::parsePercent(const std::string& body)
{
	auto pp = body.find('%');
	if (pp == std::string::npos || pp == 0)
		return -1;
	size_t st = pp;
	while (st > 0 && isdigit((unsigned char) body[st - 1]))
		st--;
	if (st == pp)
		return -1;
	const int v = atoi(body.substr(st, pp - st).c_str());
	return (v >= 0 && v <= 100) ? v : -1;
}

// The bar shows the work furthest from done, which is the lower of two
// percentages: the transfer's and the checks'. A run is both -- rclone
// compares as it lists and moves what differs -- and a saves backup with
// one changed save among hundreds moves its bytes in a second, then spends
// the run comparing. Bytes alone pinned the bar at 100% over a live
// CHECKING 120 OF 400 FILES for all of it (review, 2026-09-08); the lower
// of the two reads 27 -> 0 -> 89 -> 100 block by block on that run, every
// number rclone's.
//
// The transfer's percentage is bytes: they say how long this will take when
// the files are a save game and a disc image. The count of files
// transferred stands in only when the bytes have no number (nothing but
// empty files queued), because it counts completed files -- four disc
// images moving in parallel read 0 / 4 until the first lands, and a bar
// that took the lowest of all three would sit at zero for most of such a
// run. A run with nothing to move has neither, and then the count of files
// checked is the only measure there is -- a true one, which is more than
// the spinner it replaces could say. Never a number nobody printed: with
// none, mPercent keeps its last real value. Called with mMutex held.
void GuiCloudTransfer::refreshPercent()
{
	int p = mBytePercent >= 0 ? mBytePercent : mFilePercent;
	if (mCheckPercent >= 0 && (p < 0 || mCheckPercent < p))
		p = mCheckPercent;
	if (p >= 0)
		mPercent = p;
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
	foldUnit();   // the last unit: no ">>> unit" follows it
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
		// Printable ASCII and every UTF-8 byte: rclone shortens a long name
		// with U+2026, and dropping it as "unprintable" turned "Ikari n...ge"
		// into "Ikari nge" on the page. Only C0 controls and DEL are noise.
		if (((unsigned char) raw[i] >= 32 && (unsigned char) raw[i] < 127) || (unsigned char) raw[i] >= 0x80)
			clean += raw[i];
	}
	return Utils::String::trim(clean);
}
