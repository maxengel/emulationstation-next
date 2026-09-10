#include "guis/GuiCloudTransfer.h"

#include "CloudExit.h"
#include "ThreadedCloudSync.h"
#include "Window.h"
#include "ThemeData.h"
#include "LocaleES.h"
#include "utils/StringUtil.h"
#include "utils/FileSystemUtil.h"
#include "Log.h"
#include "SystemData.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sys/wait.h>

GuiCloudTransfer::GuiCloudTransfer(Window* window, const std::string& command, const std::string& title,
	int itemsExpected, int itemsAfterContent)
	: GuiComponent(window), mBusyAnim(window, ""), mBackground(window, ":/frame.png"),
	  mCommand(command), mTitleText(title),
	  mItemsExpected(itemsExpected > 0 ? itemsExpected : 0), mItemsAfterContent(itemsAfterContent > 0 ? itemsAfterContent : 0),
	  mHandle(nullptr)
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
	mTitle    = std::make_shared<TextComponent>(window, Utils::String::toUpper(title), theme->Title.font, theme->Title.color, ALIGN_CENTER);
	mStatus   = std::make_shared<TextComponent>(window, _("PREPARING..."), mTextFont,  theme->Text.color,      ALIGN_CENTER);
	mCounter  = std::make_shared<TextComponent>(window, "",                mSmallFont, theme->TextSmall.color, ALIGN_CENTER);
	mActivity = std::make_shared<TextComponent>(window, "",                mTextFont,  theme->Text.color,      ALIGN_CENTER);
	mDetail   = std::make_shared<TextComponent>(window, "",                mSmallFont, theme->TextSmall.color, ALIGN_CENTER);
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
	for (auto& t : { mTitle, mStatus, mCounter, mActivity, mDetail, mNote, mElapsed, mFooter })
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
	//   1 the item    2 ITEM i OF n    hM + hS   a tight pair
	//   gap
	//   3 the doing   4 its totals     hM + hS   a tight pair
	//   0.5 gap                                  the bar is the item's, so it sits close
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
	mStatus  ->setPosition(x, y);          // 1. the item
	mCounter ->setPosition(x, y + rM);     // 2. ITEM i OF n
	y += rM + rS + gap;
	mActivity->setPosition(x, y);          // 3. what it is doing on it
	mDetail  ->setPosition(x, y + rM);     // 4. that item's files and bytes
	y += rM + rS + 0.5f * gap;
	// 5. The bar, the spinner and the done-note share one row, centred on it.
	// The bar used to be drawn a row below the spinner it replaced, so the
	// page's centre of gravity moved every time the percentage came and went.
	// No caption on the spinner: line 3 already says WORKING..., and the
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

// The run's starting state. Called from the constructor, and again from
// input() for TRY AGAIN once the finished worker has been joined -- so no
// other thread reads these while they are set. The rows that only the done
// state writes are cleared here too, since the running state never touches
// them and a retry would otherwise start under last time's note.
void GuiCloudTransfer::reset()
{
	mItemIndex = 0; mItemCount = mItemsExpected; mTrailing = mItemsAfterContent; mScriptBase = 0; mLastScriptIndex = 0;
	mUnitBytes = 0; mUnitFiles = 0; mRunBytes = 0; mRunFiles = 0; mRunSized = false;
	mRemovedFiles = 0; mRemovedBytes = 0; mRemovedDetail.clear(); mAnyTransferred = false;
	mFilesThisBlock = 0; mSeenBlock = false;
	mChecksDone = 0; mChecksTotal = 0; mListed = 0; mChecksThisBlock = 0;
	mBytePercent = -1; mFilePercent = -1; mCheckPercent = -1; mPercent = -1;
	mFinished = false; mExit = -1; mShownFinished = false; mShownPercent = -1;
	mElapsedMs = 0;
	mCurrent.clear(); mFileProgress.clear(); mTotals.clear(); mFilesTotals.clear(); mUnitLabel.clear(); mDoing.clear(); mChecking.clear();
	mTiers.clear(); mFailed.clear(); mPendingWhys.clear(); mUnitsSinceTier.clear(); mWhy.clear();
	if (mNote) mNote->setText("");
	if (mCounter) mCounter->setText("");
	if (mDetail) mDetail->setText("");
}

// Input is refused while the transfer runs -- there is nothing to choose, and
// a stray press should not close a page somebody is waiting on. Once it has
// finished the page waits for the person rather than the other way round: any
// button dismisses it, and when the run did not complete, A runs the same
// command again from this page -- the surface that reported the failure
// carries the retry (D-CLOUD-077, D-UI-028).
bool GuiCloudTransfer::input(InputConfig* config, Input input)
{
	std::unique_lock<std::mutex> lock(mMutex);
	if (!mFinished || !input.value)
		return true;
	const Outcome o = outcome();
	if (!o.completed && config->isMappedTo("a", input))
	{
		// The worker has set mFinished and is about to return, or has; join
		// it before the counters it wrote are reset under it. mCommand is
		// unchanged, so a backup that includes the settings archive
		// re-archives on retry -- rotation keeps three, and stripping the
		// parts that finished is not worth the complexity.
		lock.unlock();
		if (mHandle != nullptr)
		{
			if (mHandle->joinable())
				mHandle->join();
			delete mHandle;
			mHandle = nullptr;
		}
		reset();
		mHandle = new std::thread(&GuiCloudTransfer::threadRun, this);
		return true;
	}
	// A restore may have brought files into folders the lists scanned at
	// boot -- screenshots in particular (#82). Re-read what changed once this
	// page is gone -- when any part of the run finished, not only when all
	// of it did: a saves restore that landed under a ROMs restore that did
	// not still put screenshots where the lists cannot see them.
	bool anyTierOk = false;
	for (auto& t : mTiers)
		if (t.rc == 0 || t.rc == 9)
			anyTierOk = true;
	const bool restored = (o.completed || anyTierOk) && mCommand.find("restore") != std::string::npos;
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
	{
		if (!outcome().completed)
			prompts.push_back(HelpPrompt("a", _("TRY AGAIN")));
		prompts.push_back(HelpPrompt("b", _("CLOSE")));
	}
	return prompts;
}

// The word for the run (D-UI-028; es-native-ui.md "Outcome vocabulary").
//
// COMPLETED when every part did -- rclone's 9, nothing needed moving,
// counts. Everything short of that is COULDN'T FINISH, including a run
// whose parts disagree: a tier that finished
// beside one that did not, a unit that finished inside a tier that did not
// (its later units failed with a why of their own), or a match cut off
// after it had already removed files. Those used to read COMPLETED WITH
// GAPS; a half-outcome the player cannot act on reads as "working kind of"
// and costs more trust than a plain failure does (maintainer, 2026-09-10,
// D-UI-030). `partial` survives as the flag that shapes the lines below --
// what moved is still said, and line 4 still names the items that did not
// finish -- but it no longer changes the word. SKIPPED for the two sentinels the
// scripts exit before touching anything (CloudExit.h), when that is all
// the run has to report; they are not failures, and FAILED would send
// somebody to a log to find nothing wrong. COULDN'T FINISH for everything
// else; the why goes on line 4 with the items it stopped. A run with no
// tier lines -- the match, or a command composed before they existed -- is
// read from its exit code alone, as it always was.
GuiCloudTransfer::Outcome GuiCloudTransfer::outcome() const
{
	Outcome o;
	o.completed = completed();
	bool anyOk = false, anyBad = false, anyUnitOk = false;
	int onlyCode = -2;   // the one code every failed tier shares, or -1 when they differ
	for (auto& t : mTiers)
	{
		const bool ok = t.rc == 0 || t.rc == 9;
		anyOk = anyOk || ok;
		anyBad = anyBad || !ok;
		if (!ok)
		{
			if (!t.tierLevelFail && t.unitsFailed > 0 && t.unitsAnnounced > t.unitsFailed)
				anyUnitOk = true;
			onlyCode = onlyCode == -2 ? t.rc : onlyCode == t.rc ? onlyCode : -1;
		}
	}
	const bool match = mCommand.find("--match") != std::string::npos;
	o.partial = !o.completed && ((anyOk && anyBad) || anyUnitOk || (match && mRemovedFiles > 0));
	const int code = mTiers.empty() ? mExit : onlyCode;
	o.skipped = !o.completed && !o.partial && (code == CloudExit::LockHeld || code == CloudExit::NoNetwork);
	if (o.completed)
		o.word = _("COMPLETED");
	else if (o.partial)
		o.word = _("COULDN'T FINISH");
	else if (code == CloudExit::LockHeld)
		o.word = _("SKIPPED - ANOTHER CLOUD SYNC IS RUNNING");
	else if (code == CloudExit::NoNetwork)
		o.word = _("SKIPPED - NO NETWORK CONNECTION");
	else
		o.word = _("COULDN'T FINISH");
	return o;
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

	for (auto& t : { mTitle, mStatus, mCounter, mActivity, mDetail, mNote, mElapsed, mFooter })
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

std::string GuiCloudTransfer::fitSentences(const std::shared_ptr<Font>& font, std::string text, float width)
{
	if (!font)
		return text;
	while (font->sizeText(text).x() > width)
	{
		// the last sentence boundary before the end: ". " with something after it
		const size_t end = text.find_last_not_of(" .");
		if (end == std::string::npos)
			break;
		const size_t cut = text.rfind(". ", end);
		if (cut == std::string::npos)
			break;
		text = text.substr(0, cut + 1);
	}
	return fitOneLine(font, text, width);
}

// rclone's size units, once: how each is spelt in its output (the torn
// "Ki"/"Mi"/"Gi" is a per-file line cut at 80 columns), what the page calls
// it, and how many bytes it is. roundSizes finds a size by this table,
// parseBytes reads it and sizeLabel re-renders it; prettyRclone's rename by
// the same table is the fallback for a token that did not parse. So a unit
// the page can show is a unit it can add up. Longest spelling first: "GiB"
// must be matched before "Gi".
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

// Every size and speed in an rclone fragment at sizeLabel's precision:
// "16.521 MiB / 16.521 MiB, 100%, 519.844 KiB/s" -> "16.5 MB / 16.5 MB,
// 100%, 520 KB/s". rclone prints three decimals of whatever unit it lands
// on, and on a 640px panel the totals row ran past its width and lost the
// time left to the ellipsis (VM frames, 2026-09-09). Each number-and-unit is
// parsed (parseBytes) and re-rendered (sizeLabel) rather than trimmed, so
// the page prints one precision for every size it shows, live or summed.
// A "/s" after the unit stays: a speed is a size per second. Percentages
// and times carry no unit from the table and pass through untouched; so
// does a number whose unit did not parse.
std::string GuiCloudTransfer::roundSizes(const std::string& f)
{
	std::string out;
	size_t i = 0;
	while (i < f.size())
	{
		// a number starts at a digit that does not continue a token ("3m2s")
		const bool starts = isdigit((unsigned char) f[i]) && (i == 0 || !(isalnum((unsigned char) f[i - 1]) || f[i - 1] == '.'));
		if (!starts)
		{
			out += f[i++];
			continue;
		}
		size_t j = i;
		while (j < f.size() && (isdigit((unsigned char) f[j]) || f[j] == '.'))
			j++;
		size_t k = j;
		while (k < f.size() && f[k] == ' ')
			k++;
		const RcloneUnit* unit = nullptr;
		for (const auto& u : RCLONE_UNITS)
		{
			const std::string spelt = u.rclone;
			if (f.compare(k, spelt.size(), spelt) == 0 && (k + spelt.size() == f.size() || !isalpha((unsigned char) f[k + spelt.size()])))
			{
				unit = &u;
				break;
			}
		}
		const size_t end = unit == nullptr ? j : k + std::string(unit->rclone).size();
		const long bytes = unit == nullptr ? -1 : parseBytes(f.substr(i, end - i));
		out += bytes < 0 ? f.substr(i, end - i) : sizeLabel((unsigned long) bytes);
		i = end;
	}
	return out;
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

// rclone's fragments in the player's units, precision and separators:
//   "45% /2.5Mi, 300Ki/s, 5s"                            -> "45% OF 2.5 MB . 300 KB/S . 5S LEFT"
//   "1.4 GiB / 2.0 GiB, 70%, 2.5 MiB/s, ETA 3m2s"        -> "1.40 GB OF 2.00 GB . 70% . 2.5 MB/S . 3M2S LEFT"
//   "16.521 MiB / 16.521 MiB, 100%, 519.844 KiB/s, ETA 0s" -> "16.5 MB OF 16.5 MB . 100% . 520 KB/S . 0S LEFT"
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
	f = roundSizes(f);
	auto rep = [&f](const std::string& from, const std::string& to) { f = Utils::String::replace(f, from, to); };
	for (const auto& u : RCLONE_UNITS)
		if (std::string(u.rclone) != u.shown)
			rep(u.rclone, u.shown);
	rep("ETA ", "");
	rep(" / ", " OF "); rep(" /", " OF ");
	rep(", ", " · ");
	f = Utils::String::toUpper(f);
	// a trailing duration -- digits then a unit letter, no percent, no bytes --
	// is time left. The separator is four bytes, not three: the middle dot is
	// two in UTF-8, and skipping three left a space on the front of the last
	// segment that isdigit() refused, so LEFT was never appended (2026-09-09).
	static const std::string SEP = " · ";
	size_t sep = f.rfind(SEP);
	std::string last = sep == std::string::npos ? f : f.substr(sep + SEP.size());
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

	if (mFinished)
	{
		// Seven lines, the same rows as the run (D-UI-024/026), now carrying
		// the outcome (D-UI-028): 1 the word; 2 how many items did not finish,
		// when partial; 3 what moved; 4 the items that did not finish and why; 5
		// what is in place, or what to do next; 6 elapsed; 7 the buttons.
		const Outcome o = outcome();
		const bool restore = mCommand.find("restore") != std::string::npos;
		const bool match = mCommand.find("--match") != std::string::npos;
		mStatus->setText(fitOneLine(mTextFont, o.word, mLineWidth));

		// 2. When partial, the count: N distinct items that did not finish of the
		// run's M -- the run's count when a script announced it, else what
		// was reached, never fewer than N.
		std::string counter;
		if (o.partial)
		{
			std::vector<std::string> names;
			for (auto& f : mFailed)
				if (std::find(names.begin(), names.end(), f.label) == names.end())
					names.push_back(f.label);
			const int n = (int) names.size();
			const int m = std::max(std::max(mItemCount, mItemIndex), n);
			counter = std::to_string(n) + " " + std::string(_("OF")) + " " + std::to_string(m) + " "
				+ std::string(n == 1 && m == 1 ? _("ITEM DID NOT FINISH") : _("ITEMS DID NOT FINISH"));
		}
		mCounter->setText(fitOneLine(mSmallFont, counter, mLineWidth));

		// 3 and 4: what moved, and what did not.
		if (mRemovedFiles > 0)
		{
			// A match is mostly deletion, and rclone's totals for a deletion
			// are "0 B / 0 B" -- true and useless. Line 3 carries what the
			// confirmation showed instead: what went. Line 4 is per system
			// when the match completed, and the items it did not reach when
			// it was cut (below).
			std::string removed = std::string(_("REMOVED")) + " " + std::to_string(mRemovedFiles) + " "
				+ std::string(mRemovedFiles == 1 ? _("FILE FROM THIS DEVICE") : _("FILES FROM THIS DEVICE"));
			if (mRemovedBytes > 0)
				removed += " · " + sizeLabel(mRemovedBytes);
			mActivity->setText(fitOneLine(mTextFont, removed, mLineWidth));
			if (o.completed)
			{
				std::string detail;
				for (auto& d : mRemovedDetail)
					detail += (detail.empty() ? "" : "   ") + d;
				mDetail->setText(fitOneLine(mSmallFont, detail, mLineWidth));
				if (mAnyTransferred)
					mCounter->setText(fitOneLine(mSmallFont, _("FILES YOUR CLOUD HAD AND THIS DEVICE DID NOT WERE DOWNLOADED TOO."), mLineWidth));
			}
		}
		else
		{
			// Line 3 answers for the whole run, not its last unit: the page
			// used to end on "SNES  3 OF 3" over that unit's totals, or over
			// nothing when the last unit only compared (#85). It carries the
			// sum of every unit's final "Transferred:" pair, in the run's own
			// verb -- shown on a partial run too, because what moved is the half of
			// the answer that is good news. Only what rclone printed: a run
			// that never printed a byte line says the word and nothing more.
			//
			// The count is of finished files. The bytes are rclone's
			// bytes-read counter, and on a run that stopped or failed that
			// includes what the transfers in flight had read when it died
			// and never completed -- so a run that did not complete names
			// its files and no size, rather than claim as BACKED UP bytes
			// that were not.
			std::string summary;
			const bool sized = o.completed && mRunBytes > 0;
			if (mRunSized && (mRunFiles > 0 || sized))
			{
				if (mRunFiles > 0)
					summary = std::to_string(mRunFiles) + " " + std::string(mRunFiles == 1 ? _("FILE") : _("FILES"));
				if (sized)
					summary += (summary.empty() ? "" : " · ") + sizeLabel((unsigned long) mRunBytes);
				summary += " " + std::string(restore ? _("RESTORED") : _("BACKED UP"));
			}
			else if (mRunSized && o.completed)
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
			mActivity->setText(fitOneLine(mTextFont, summary, mLineWidth));
			mDetail  ->setText("");
		}
		if (!o.completed && !o.skipped)
		{
			// 4. The items that did not finish, and why: "NES, SETTINGS - YOUR
			// CLOUD STOPPED ANSWERING". Items sharing a why share the line's
			// one dash; a second why gets its own group. A why with no item
			// (the scripts spoke before any unit, and no tier line followed)
			// stands alone. Not on a skip: line 1 has said the one thing
			// there is to say about every part, and a list of them under it
			// would read as a list of failures.
			std::vector<std::pair<std::string, std::vector<std::string>>> groups;
			for (auto& f : mFailed)
			{
				auto g = groups.begin();
				for (; g != groups.end(); ++g)
					if (g->first == f.why)
						break;
				if (g == groups.end())
					g = groups.insert(groups.end(), std::make_pair(f.why, std::vector<std::string>()));
				if (!f.label.empty() && std::find(g->second.begin(), g->second.end(), f.label) == g->second.end())
					g->second.push_back(f.label);
			}
			std::string detail;
			for (auto& g : groups)
			{
				std::string names;
				for (size_t i = 0; i < g.second.size(); i++)
					names += (i ? ", " : "") + g.second[i];
				detail += (detail.empty() ? "" : "  ·  ") + names + (names.empty() ? "" : " - ") + g.first;
			}
			mDetail->setText(fitOneLine(mSmallFont, detail, mLineWidth));
		}

		// 5. What is in place -- one clause per verb, true because rclone
		// renames each file into place when it is complete and the content
		// scripts delete nothing outside a match (D-CLOUD-077) -- when the
		// run did not complete. When it did, the one thing left to do: a
		// content run that changed the ROMs on this device is not visible in
		// the game lists until they are rebuilt, and nobody should have to
		// know that (maintainer, 2026-09-07).
		const bool contentRun = mCommand.find("cloud_content_restore") != std::string::npos;
		std::string note;
		if (!o.completed)
		{
			const bool moved = mAnyTransferred || mRunFiles > 0;
			if (match)
				note = mRemovedFiles == 0 ? _("NOTHING WAS REMOVED.")
					: mRemovedFiles == 1 ? _("1 FILE WAS REMOVED FROM THIS DEVICE. YOUR CLOUD STILL HAS IT.")
					: std::to_string(mRemovedFiles) + " " + std::string(_("FILES WERE REMOVED FROM THIS DEVICE. YOUR CLOUD STILL HAS THEM."));
			else if (restore)
				note = moved ? _("WHAT ARRIVED IS ON THIS DEVICE. THE REST IS AS IT WAS.") : _("NOTHING ARRIVED. THIS DEVICE IS AS IT WAS.");
			else
				note = moved ? _("WHAT WAS SENT IS IN YOUR CLOUD. THE REST IS STILL ON THIS DEVICE.") : _("NOTHING WAS SENT. YOUR CLOUD IS AS IT WAS.");
		}
		else if (contentRun && (mRemovedFiles > 0 || mAnyTransferred))
			note = _("UPDATE GAMELISTS UNDER GAME SETTINGS TO SEE THE CHANGE.");
		// Two sentences on a 640px panel do not fit the small font; the
		// first alone says what is in place, so it is what survives.
		mNote->setText(fitSentences(mSmallFont, note, mLineWidth));

		mElapsed ->setText(std::string(_("ELAPSED")) + " " + elapsed);
		// 7. The retry lives on the surface that reported the failure: A runs
		// the same command again (input), B closes; the help bar carries the
		// same two. A run that completed has nothing to retry.
		mFooter  ->setText(o.completed ? _("PRESS ANY BUTTON TO CLOSE") : _("A  TRY AGAIN     B  CLOSE"));
	}
	else
	{
		// 1 and 2: the item, and which of how many. The item is the row that
		// changes when the run moves on, so it leads; the file in flight used
		// to, and the page read bottom-up -- a file, its percentage, and only
		// then what they belonged to (maintainer, 2026-09-09, D-UI-026). The
		// count is the whole run's (handleLine), so settings, saves and two
		// systems read ITEM 1 OF 4 through ITEM 4 OF 4 whichever script is
		// speaking. Never an OF with nothing on either side: while the count
		// is unknown the row reads ITEM i alone.
		if (mItemIndex == 0)
		{
			mStatus ->setText(_("PREPARING..."));
			mCounter->setText("");
		}
		else
		{
			const std::string item = Utils::String::toUpper(mUnitLabel);
			mStatus->setText(item.empty() ? _("WORKING...") : fitOneLine(mTextFont, item, mLineWidth));
			std::string counter = std::string(_("ITEM")) + " " + std::to_string(mItemIndex);
			if (mItemCount > 0)
				counter += " " + std::string(_("OF")) + " " + std::to_string(mItemCount);
			mCounter->setText(counter);
		}

		// 3: what it is doing on this item -- the line that says it is alive.
		// A thousand small BIOS files spend minutes between percentage
		// changes, and a frozen percentage is indistinguishable from a hang.
		std::string doing;
		if (!mCurrent.empty())
		{
			// "TRANSFERRING name.zip . 45% OF 2.5 MB . 300 KB/S . AND 3 MORE
			// FILES": a single space inside a segment and " . " between them,
			// the same as every other row (#85). The name is the part that
			// has to show; the rest is shed a segment at a time, least useful
			// first, until the line fits this font: the AND N MORE count goes
			// before the file's own progress, and last the name alone is
			// clipped -- half a percentage after an ellipsis says nothing,
			// and line 4 carries the item's percentage regardless.
			const std::string head     = std::string(_("TRANSFERRING")) + " " + mCurrent;
			const std::string progress = mFileProgress.empty() ? "" : " · " + prettyRclone(mFileProgress);
			const std::string more     = mFilesThisBlock > 1
				? " · " + std::string(_("AND")) + " " + std::to_string(mFilesThisBlock - 1) + " " + std::string(_("MORE FILES")) : "";
			const auto fits = [this](const std::string& t) { return mTextFont && mTextFont->sizeText(t).x() <= mLineWidth; };
			if (fits(head + progress + more))
				doing = head + progress + more;
			else if (fits(head + progress))
				doing = head + progress;
			else
				doing = fitOneLine(mTextFont, head, mLineWidth);   // unchanged when it fits, clipped when it does not
		}
		else if (mChecksTotal > 0 || mListed > 0)
		{
			// Nothing in flight, plenty happening: rclone is comparing what
			// is here with what is there, and for a device whose saves are
			// all in the cloud already that is the whole run. It prints no
			// per-file line for a comparison, so this is the count it does
			// print -- and the name, when it caught one mid-comparison and
			// there is room beside the count. A run that showed neither
			// looked hung until it said COMPLETED (maintainer, 2026-09-08).
			// Before anything is queued to compare the only count is what
			// rclone has listed, and that counts both sides -- 40 saves list
			// as 80 -- so it is not shown as a number the player would try
			// to reconcile with their files; the spinner on row 5 is the sign
			// of life until the first check is queued.
			if (mChecksTotal > 0)
				doing = std::string(_("CHECKING")) + " " + std::to_string(mChecksDone) + " " + std::string(_("OF")) + " "
					+ std::to_string(mChecksTotal) + " " + std::string(_("FILES"));
			else
				doing = _("CHECKING FILES...");
			// the name is the line's one optional segment, and the first to go
			if (!mChecking.empty())
			{
				const std::string named = doing + " · " + mChecking;
				if (mTextFont && mTextFont->sizeText(named).x() <= mLineWidth)
					doing = named;
			}
		}
		else if (mDoing == "archive")
		{
			// backuptool is writing the settings archive and prints nothing
			// this page can use, so ES announces it (">>> doing archive") and
			// the settings item says what it is doing like every other item,
			// rather than sitting on a spinner (maintainer, 2026-09-09).
			doing = _("WRITING THE SETTINGS ARCHIVE...");
		}
		else if (mItemIndex > 0)
			doing = _("WORKING...");
		mActivity->setText(doing);

		// 4: this item's totals -- the count line and the byte line of the
		// last stats block. "12 / 45, 27%" -> "12 OF 45 FILES . 27%".
		std::string totals;
		if (!mFilesTotals.empty())
		{
			auto pct = mFilesTotals.find(", ");
			totals = Utils::String::replace(mFilesTotals.substr(0, pct), " / ", " " + std::string(_("OF")) + " ") + " " + std::string(_("FILES"));
			if (pct != std::string::npos)
				totals += " · " + mFilesTotals.substr(pct + 2);
		}
		// "0 B / 0 B, -, 0 B/s, ETA -" is the byte line while nothing is queued
		// to move -- the whole of a run that only compares -- and it is true of
		// nothing anybody asked about. Line 3 says what such a run is doing;
		// this line stays blank rather than read "0 B OF 0 B . 0 B/S".
		if (!mTotals.empty() && mTotals.rfind("0 B / 0 B", 0) != 0)
			totals += (totals.empty() ? "" : "   ") + prettyRclone(mTotals);
		mDetail->setText(fitOneLine(mSmallFont, totals, mLineWidth));

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
	//
	// The scripts talk to this page through three ">>> " markers on stdout:
	//
	//   ">>> unit <label>|<i>|<n>" -- an item starts: a system ("nes|2|5")
	//     or a phase ("SAVES||", "SETTINGS||"). Everything per-block is reset
	//     with it; the label survives. The page numbers items across the
	//     whole run itself (row 2, ITEM i OF n; D-UI-026), because one run
	//     chains several scripts and each counts only its own units: a label
	//     that differs from the current one is the next item, the same label
	//     again is a re-announcement and does not advance (ES announces
	//     SETTINGS before backuptool runs, then cloud_backup announces it
	//     again). The script's own i|n are read for one thing: an
	//     announcement carrying n says how many units that script has, so
	//     n = the items counted before that script's first announcement
	//     + its n + the single-item phases ES chained after it (mTrailing).
	//     Until a script says, n is ES's estimate from the constructor; 0 is
	//     unknown and row 2 reads ITEM i alone. i never exceeds n on the
	//     page. A script whose i starts over, or that carries a count after
	//     one that did not, is a new script.
	//   ">>> doing <keyword>" -- what the item is busy with while rclone is
	//     not running yet. "archive": backuptool is writing the settings
	//     archive (its own output is discarded), and row 3 says so until the
	//     next per-file, checks or totals line, or the next unit. Any other
	//     keyword is a newer script's and is ignored rather than shown raw.
	//   ">>> removed <files>|<bytes>|<per-system>" -- a match's summary, for
	//     the done page (below). A match cut off by the network prints it
	//     before exiting 69, so the page can say what had already gone.
	//   ">>> why <sentence>" -- a script saying, at the point of failure and
	//     in the player's words, what went wrong (D-UI-028). Attached to the
	//     unit it arrived under; before any unit, to the tier that reports
	//     next. The last one is the run's why for a command with no tiers.
	//   ">>> tier <label>|<rc>" -- GuiMenu's run composition reporting each
	//     of its parts as it ends (SAVES, ROMS AND BIOS, SETTINGS), so the
	//     done page can say which finished and which did not.
	if (line.rfind(">>> why ", 0) == 0)
	{
		std::string why = Utils::String::toUpper(Utils::String::trim(line.substr(8)));
		// Line 4 supplies its own punctuation; a full stop after the dash
		// reads as a typo.
		while (!why.empty() && why.back() == '.')
			why.pop_back();
		if (why.empty())
			return;
		mWhy = why;
		mPendingWhys.push_back({ Utils::String::toUpper(mUnitLabel), why });
		return;
	}
	if (line.rfind(">>> tier ", 0) == 0)
	{
		auto parts = Utils::String::split(line.substr(9), '|', false);
		Tier t;
		t.label = Utils::String::toUpper(parts.size() > 0 ? Utils::String::trim(parts[0]) : "");
		t.rc = parts.size() > 1 ? atoi(Utils::String::trim(parts[1]).c_str()) : -1;
		t.unitsAnnounced = (int) mUnitsSinceTier.size();
		t.unitsFailed = 0;
		t.tierLevelFail = false;
		if (t.label.empty())
			return;
		const bool ok = t.rc == 0 || t.rc == 9;
		if (!ok)
		{
			std::vector<std::string> named;
			for (auto& w : mPendingWhys)
			{
				// A why printed before any unit is the tier's own; the tier
				// as a whole is the item that did not finish.
				const std::string label = w.label.empty() ? t.label : w.label;
				if (w.label.empty())
					t.tierLevelFail = true;
				else if (std::find(named.begin(), named.end(), w.label) == named.end())
					named.push_back(w.label);
				// The last why for an item wins; the scripts may say more
				// than one thing about the same unit as they give up on it.
				bool seen = false;
				for (auto& f : mFailed)
					if (f.label == label) { f.why = w.why; seen = true; }
				if (!seen)
					mFailed.push_back({ label, w.why });
			}
			t.unitsFailed = (int) named.size();
			// A part that failed without a word: the part is the item, and
			// the code supplies the why. Its units are not presumed to have
			// finished, because nothing said they did.
			if (mPendingWhys.empty())
			{
				t.tierLevelFail = true;
				mFailed.push_back({ t.label, ThreadedCloudSync::whyForCode(t.rc) });
			}
		}
		mTiers.push_back(t);
		mPendingWhys.clear();
		mUnitsSinceTier.clear();
		return;
	}
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
		const std::string label = parts.size() > 0 ? Utils::String::trim(parts[0]) : "";
		const int scriptIndex   = parts.size() > 1 ? atoi(Utils::String::trim(parts[1]).c_str()) : 0;
		const int scriptCount   = parts.size() > 2 ? atoi(Utils::String::trim(parts[2]).c_str()) : 0;
		// The next item, unless it is the current one announced again. The
		// first announcement is an item whatever its label says.
		if (mItemIndex == 0 || label != mUnitLabel)
		{
			if (scriptCount > 0 && (mLastScriptIndex == 0 || scriptIndex <= mLastScriptIndex))
				mScriptBase = mItemIndex;   // a script's first announcement: what came before it is its base
			mItemIndex++;
			mUnitsSinceTier.push_back(Utils::String::toUpper(label));
		}
		mLastScriptIndex = scriptCount > 0 ? scriptIndex : 0;
		if (scriptCount > 0)
			mItemCount = mScriptBase + scriptCount + mTrailing;
		// Never ITEM 5 OF 4: a script that announced more than anybody
		// expected grows the count rather than overrun it.
		if (mItemCount > 0 && mItemIndex > mItemCount)
			mItemCount = mItemIndex;
		mUnitLabel = label;
		mDoing.clear();
		mCurrent.clear(); mFileProgress.clear(); mTotals.clear(); mFilesTotals.clear(); mChecking.clear();
		mFilesThisBlock = 0; mChecksThisBlock = 0; mSeenBlock = false;
		mChecksDone = 0; mChecksTotal = 0; mListed = 0;
		mBytePercent = -1; mFilePercent = -1; mCheckPercent = -1; mPercent = -1;
		return;
	}
	if (line.rfind(">>> doing ", 0) == 0)
	{
		const std::string what = Utils::String::trim(line.substr(10));
		if (what == "archive")
			mDoing = what;
		return;
	}
	// From here on the line is rclone's, so whatever the item was busy with
	// before rclone ran is over.
	if (line.rfind("Transferred:", 0) == 0)
	{
		std::string body = Utils::String::trim(line.substr(12));
		if (body.find('/') == std::string::npos)
			return;
		mDoing.clear();
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
		mDoing.clear();
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
		mDoing.clear();
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
	// A command with no tier lines (the match; anything composed before
	// them) that did not complete: what it said is its failed item -- the
	// unit the why arrived under, or the why alone -- and with no why at
	// all, the code's phrase for the unit that was running, if one was.
	if (ret != 0 && ret != 9 && mFailed.empty())
	{
		for (auto& w : mPendingWhys)
			mFailed.push_back(w);
		if (mFailed.empty() && ret != CloudExit::LockHeld && ret != CloudExit::NoNetwork)
			mFailed.push_back({ Utils::String::toUpper(mUnitLabel), ThreadedCloudSync::whyForCode(ret) });
	}
	mPendingWhys.clear();
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
