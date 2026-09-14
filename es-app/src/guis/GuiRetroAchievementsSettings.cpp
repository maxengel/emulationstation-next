#include "GuiRetroAchievementsSettings.h"
#include "ThreadedHasher.h"
#include "GuiHashStart.h"
#include "SystemConf.h"
#include "ApiSystem.h"
#include "RetroAchievements.h"
#include "utils/Platform.h"
#include "utils/FileSystemUtil.h"
#include "utils/StringUtil.h"

#include "guis/GuiMsgBox.h"
#include "guis/GuiOfflineScan.h"
#include "CloudText.h"
#include "OfflineAchievements.h"
#include "Settings.h"
#include "components/ComponentList.h"
#include "components/MultiLineMenuEntry.h"
#include "utils/TimeUtil.h"
#include "components/SwitchComponent.h"
#include "components/OptionListComponent.h"
#include "components/TextComponent.h"
#include "components/MenuComponent.h"
#include "renderers/Renderer.h"
#include "math/Misc.h"
#include "ThemeData.h"
#include "Window.h"
#include "LocaleES.h"
#include "Log.h"
#include <memory>

#if defined(ROCKNIX)
// A line of the page's own text, at the standard text size, wrapped to the
// page's width when it needs a second line: what the OFFLINE ACHIEVEMENTS
// page is for is room to read (D-RA-003, D-UI-023: two lines, never three).
// Not selectable, and inset like the selectable rows so it lines up.
//
// The width is settled here rather than left to the list, because
// TextComponent measures its wrap at its full width and draws at its padded
// one (es-code-traps.md): given the width without the inset, the height it
// budgets is for the lines it will actually draw, and only then is the inset
// put back with the height fixed. The width is the menu's own rule
// (MenuComponent::updateSize): the whole screen on a handheld panel, else
// the shorter of the screen's height and nine tenths of its width.
static void addInfoRow(GuiSettings* s, Window* window, const std::string& text)
{
	auto theme = ThemeData::getMenuTheme();
	const float inset = 10.0f;
	const float width = Renderer::ScreenSettings::fullScreenMenus()
		? (float) Renderer::getScreenWidth()
		: (float) Math::min((int) Renderer::getScreenHeight(), (int) (Renderer::getScreenWidth() * 0.90f));

	auto tc = std::make_shared<TextComponent>(window, text, theme->Text.font, theme->Text.color, ALIGN_LEFT,
		Vector3f::Zero(), Vector2f(width - 2 * inset, 0));
	const float height = tc->getSize().y();
	tc->setPadding(Vector4f(inset, 0, inset, 0));
	tc->setSize(width, height);
	tc->setVerticalAlignment(ALIGN_TOP);

	ComponentListRow row;
	row.selectable = false;
	row.addElement(tc, true);
	s->addRow(row);
}

// SCAN GAMES FOR OFFLINE ACHIEVEMENTS (fork #179, D-RA-010): the proxy
// serves a game offline only from what it cached while online, so a game
// never started with a connection earned nothing offline. The row runs
// raofflineproxy-ctl scan on a page of its own (GuiOfflineScan, the fourth
// surface tier) and carries one line underneath (D-UI-023): why it cannot
// run now, else how the last scan went and how many games are ready.
//
// A row that cannot run yet is dimmed, not hidden (es-ui-style-guide.md),
// and the dim has to be applied by the entry itself: ComponentList::render
// sets every element's colour every frame from the theme, so a colour set
// once at construction is gone by the first frame (the same reason a QR
// code in a row needs UntintedImageComponent, es-native-ui.md).
class DimmableMenuEntry : public MultiLineMenuEntry
{
public:
	using MultiLineMenuEntry::MultiLineMenuEntry;
	void setDimmed(bool dimmed) { mDimmed = dimmed; }
	void setColor(unsigned int color) override
	{
		MultiLineMenuEntry::setColor(mDimmed ? (color & 0xFFFFFF00) | 0x50 : color);
	}
private:
	bool mDimmed = false;
};

static bool offlineScanOn()
{
	return SystemConf::getInstance()->getBool("global.retroachievements.offlineproxy");
}

// The cheap question, for a row built at page open: an address on any
// interface. Whether RetroAchievements itself answers is the ctl's to ask
// when the row is pressed -- a network round trip never runs at page build
// (es-ui-style-guide.md, Gating).
static bool offlineScanOnline()
{
	return !Utils::Platform::queryIPAddress().empty();
}

// The line under the row. Why it cannot run now, else how the last run went
// and the count that is the point of the row -- longest form first, and the
// row's own small font decides which fits (D-UI-035). An automatic top-up
// says so in place of the date (D-UI-032).
static std::string offlineScanDetail(bool on, bool online)
{
	if (!on)
		return _("TURN ON OFFLINE ACHIEVEMENTS FIRST.");
	if (!online)
		return _("YOU'RE NOT ONLINE.");

	const std::string ready = GuiOfflineScan::readyPhrase(OfflineAchievements::readyCount());
	const CloudText::ScanStamp last = OfflineAchievements::lastScan();
	std::vector<std::string> candidates;
	if (!last.ran)
		candidates = { _("NOT SCANNED YET") + std::string("  ·  ") + ready, ready };
	else
	{
		const std::string outcome = last.code == 0 ? _("COMPLETED") : _("COULDN'T FINISH");
		std::string head;
		if (last.topup)
			head = _("WHEN YOU CAME ONLINE");
		else
		{
			// The player's own date format and clock, as the cloud rows.
			const std::string fmt = Utils::Time::getSystemDateFormat()
				+ (Settings::ClockMode12() ? " %I:%M %p" : " %H:%M");
			head = _("LAST") + std::string(" ") + Utils::Time::timeToString(last.when, fmt);
		}
		candidates = { head + "  -  " + outcome + "  ·  " + ready, outcome + "  ·  " + ready, ready };
	}

	// The description is drawn in the menu's small font, in a row that
	// spans the menu less its insets and the arrow; 0.86 of the menu width
	// is what a 640x480 frame showed the line to have.
	auto theme = ThemeData::getMenuTheme();
	const float menuWidth = Renderer::ScreenSettings::fullScreenMenus()
		? (float) Renderer::getScreenWidth()
		: (float) Math::min((int) Renderer::getScreenHeight(), (int) (Renderer::getScreenWidth() * 0.90f));
	std::shared_ptr<Font> font = theme->TextSmall.font;
	return CloudText::chooseThatFits(candidates, menuWidth * 0.86f,
		[font](const std::string& t) { return font ? font->sizeText(t).x() : 0.0f; });
}

static void offlineScanRefresh(const std::weak_ptr<DimmableMenuEntry>& weak)
{
	auto entry = weak.lock();
	if (!entry)
		return;
	const bool on = offlineScanOn();
	const bool online = offlineScanOnline();
	entry->setDimmed(!on || !online);
	entry->setDescription(offlineScanDetail(on, online));
}

// A press on the row: the reason when it cannot run, else the confirmation
// (D-UI-023: what the scan does and costs is read at the moment of
// deciding, with why the last one could not finish as its second paragraph,
// D-UI-029), then the page. YES first, NO last so B answers NO.
static void offlineScanPressed(Window* window, std::weak_ptr<DimmableMenuEntry> weak)
{
	if (!offlineScanOn())
	{
		window->pushGui(new GuiMsgBox(window, _("TURN ON OFFLINE ACHIEVEMENTS FIRST."), _("OK")));
		return;
	}
	if (!offlineScanOnline())
	{
		window->pushGui(new GuiMsgBox(window, _("YOU'RE NOT ONLINE. TRY AGAIN WHEN YOU'RE ONLINE."), _("OK")));
		return;
	}

	std::string text = _("SCAN GAMES FOR OFFLINE ACHIEVEMENTS?") + std::string("\n\n")
		+ _("LOOKS AT EVERY GAME ON THIS CONSOLE AND SAVES ITS ACHIEVEMENT DATA SO IT EARNS WHILE OFFLINE. TAKES A WHILE FOR A LARGE LIBRARY AND ASKS RETROACHIEVEMENTS ONCE PER GAME.");
	const CloudText::ScanStamp last = OfflineAchievements::lastScan();
	if (last.ran && last.code != 0 && !last.why.empty())
		text += "\n\n" + _("LAST TIME IT COULDN'T FINISH:") + " " + OfflineAchievements::scanWhy(last.why) + ".";

	window->pushGui(new GuiMsgBox(window, text,
		_("YES"), [window, weak]
		{
			window->pushGui(new GuiOfflineScan(window, "/usr/bin/raofflineproxy-ctl scan",
				[weak] { offlineScanRefresh(weak); }));
		},
		_("NO"), nullptr));
}

static std::shared_ptr<DimmableMenuEntry> addOfflineScanRow(GuiSettings* s, Window* window)
{
	auto entry = std::make_shared<DimmableMenuEntry>(window, _("SCAN GAMES FOR OFFLINE ACHIEVEMENTS"), "", false);
	std::weak_ptr<DimmableMenuEntry> weak = entry;
	offlineScanRefresh(weak);

	ComponentListRow row;
	row.addElement(entry, true);
	row.addElement(makeArrow(window), false);
	row.makeAcceptInputHandler([window, weak] { offlineScanPressed(window, weak); });
	s->addRow(row);
	return entry;
}

// The OFFLINE ACHIEVEMENTS page (fork #165, #173; D-RA-001..003): the
// switch, with what it does, that it is beta and casual-only and what that
// does to hardcore, and what the badge means, each with room. One
// system-wide switch, off by default, backed by raofflineproxy-ctl: enable
// starts the RAOfflineProxy service, records hardcore as it was and turns it
// off -- the proxy is casual-only and refuses hardcore awards -- and sets
// the toggle the launch scripts read at every game start; disable stops the
// service and puts hardcore back as recorded. Written by hand rather than
// through addSwitch: the script is the writer, not this page's save, and
// turning it on asks first, because it changes the HARDCORE MODE row on the
// page below (never silently). The script's last stdout line,
// hardcore=<0|1>, is what that row is set from afterwards, so the row shows
// what system.cfg holds; the same values are mirrored into SystemConf so the
// parent page's own save at close writes what the script wrote rather than
// what it read at open.
//
// hardcoreRow is the parent page's HARDCORE MODE switch, weak on purpose:
// this page's callbacks must never keep a row of the page below alive.
static void openOfflineAchievements(Window* window, std::weak_ptr<SwitchComponent> hardcoreRow)
{
	auto s = new GuiSettings(window, _("OFFLINE ACHIEVEMENTS").c_str());

	auto offline = std::make_shared<SwitchComponent>(window);
	offline->setState(SystemConf::getInstance()->getBool("global.retroachievements.offlineproxy"));
	s->addWithLabel(_("OFFLINE ACHIEVEMENTS"), offline);

	addInfoRow(s, window, _("EARN CASUAL ACHIEVEMENTS WITHOUT A CONNECTION. THEY ARE SENT WHEN YOU'RE BACK ONLINE."));
	addInfoRow(s, window, _("BETA. CASUAL ACHIEVEMENTS ONLY, SO TURNING IT ON TURNS HARDCORE MODE OFF."));
	// RetroArch's disconnected badge, explained where the RetroAchievements
	// choices are made (fork #162): rcheevos shows it while an award or a
	// score is waiting to reach the server, and says nothing about what it
	// means. It was the parent page's subtitle, two lines of small text at
	// 640x480 (#166); here it has a row of its own (D-RA-003).
	addInfoRow(s, window, _("!RA! IN A GAME'S CORNER MEANS AN ACHIEVEMENT HASN'T REACHED RETROACHIEVEMENTS YET."));

	// The scan, under the explanation of what the switch does: a row and
	// one line, dimmed with its reason until the switch is on and the
	// device has an address (fork #179, D-RA-010).
	std::weak_ptr<DimmableMenuEntry> scanRow = addOfflineScanRow(s, window);

	// A raw pointer on purpose: the callback lives inside the switch it
	// captures, so a shared_ptr here would be a cycle that keeps the page
	// alive forever. setState fires the change callback too, so a revert
	// made from inside it would re-enter it: quiet while the code, not the
	// player, sets the state.
	SwitchComponent* offlineRow = offline.get();
	auto quiet = std::make_shared<bool>(false);
	auto setQuietly = [offlineRow, quiet](bool state) { *quiet = true; offlineRow->setState(state); *quiet = false; };

	auto apply = [window, offlineRow, hardcoreRow, setQuietly, scanRow](bool on)
	{
		std::string last;
		// executeScriptLegacy: the public route that hands back the real
		// exit status and every line, as the cloud pages use it.
		auto result = ApiSystem::executeScriptLegacy(std::string("/usr/bin/raofflineproxy-ctl ") + (on ? "enable" : "disable") + " 2>/dev/null",
			[&last](const std::string line) { last = line; });
		if (result.second != 0 || !Utils::String::startsWith(last, "hardcore="))
		{
			setQuietly(!on);
			window->pushGui(new GuiMsgBox(window,
				on ? _("OFFLINE ACHIEVEMENTS COULDN'T BE TURNED ON.") : _("OFFLINE ACHIEVEMENTS COULDN'T BE TURNED OFF."),
				_("OK"), nullptr, GuiMsgBoxIcon::ICON_ERROR));
			return;
		}
		bool hardcoreNow = (last == "hardcore=1");
		if (auto row = hardcoreRow.lock())
			row->setState(hardcoreNow);
		SystemConf::getInstance()->set("global.retroachievements.offlineproxy", on ? "1" : "0");
		SystemConf::getInstance()->set("global.retroachievements.hardcore", hardcoreNow ? "1" : "0");
		// The scan row reads the switch: on, it offers the scan; off, it
		// says to turn the switch on first.
		offlineScanRefresh(scanRow);
	};

	offline->setOnChangedCallback([window, offlineRow, quiet, setQuietly, apply]
	{
		if (*quiet)
			return;

		if (!offlineRow->getState())
		{
			apply(false);
			return;
		}

		// The consequence, read at the moment of deciding (D-UI-023); the
		// page above already says what the feature does. NOT NOW is the
		// last button, so B answers NOT NOW (GuiMsgBox's accelerator) and
		// the switch goes back to off.
		window->pushGui(new GuiMsgBox(window,
			_("THIS IS A BETA FEATURE. IT WORKS FOR CASUAL ACHIEVEMENTS ONLY, AND TURNING IT ON TURNS HARDCORE MODE OFF."),
			_("TURN ON"), [apply] { apply(true); },
			_("NOT NOW"), [setQuietly] { setQuietly(false); }));
	});

	window->pushGui(s);
}
#endif

GuiRetroAchievementsSettings::GuiRetroAchievementsSettings(Window* window) : GuiSettings(window, _("RETROACHIEVEMENTS SETTINGS").c_str())
{
	addGroup(_("SETTINGS"));

	bool retroachievementsEnabled = SystemConf::getInstance()->getBool("global.retroachievements");
	std::string username = SystemConf::getInstance()->get("global.retroachievements.username");
	std::string password = SystemConf::getInstance()->get("global.retroachievements.password");

	// retroachievements_enable
	auto retroachievements_enabled = std::make_shared<SwitchComponent>(mWindow);
	retroachievements_enabled->setState(retroachievementsEnabled);
	addWithLabel(_("RETROACHIEVEMENTS"), retroachievements_enabled);
	
	// retroachievements, username, password
	addInputTextConfigRow(_("USERNAME"), "global.retroachievements.username", false);
	addInputTextConfigRow(_("PASSWORD"), "global.retroachievements.password", true);
#ifndef CHEEVOS_DEV_LOGIN
	// A fork build compiles no developer pair in, so the menu pages authenticate
	// with the player's own web API key, from the account's settings page on
	// retroachievements.org (#68). Held back from backups like the password.
	addInputTextConfigRow(_("WEB API KEY"), "global.retroachievements.key", true);
#endif

	addGroup(_("OPTIONS"));

	auto hardcore = addSwitch(_("HARDCORE MODE"), _("Disable loading states, rewind and cheats for more points."), "global.retroachievements.hardcore", false, nullptr);

#if defined(ROCKNIX)
	// OFFLINE ACHIEVEMENTS: a row that opens a page (D-RA-003; es-player-text
	// "A row that leads somewhere is a label"). The switch and what it means
	// need more room than one line under a row gives them on a 3.5" panel,
	// so the row carries the label and one short line -- sentence case like
	// every description on this page (#166) -- and the page carries the
	// switch with the explanation beside it. Shown only where the backend
	// is: an image without the package has no toggle to offer.
	if (Utils::FileSystem::exists("/usr/bin/raofflineproxy-ctl"))
	{
		std::weak_ptr<SwitchComponent> hardcoreRow = hardcore;
		addWithDescription(_("OFFLINE ACHIEVEMENTS"), _("Beta. Casual achievements only."), makeArrow(mWindow),
			[window, hardcoreRow] { openOfflineAchievements(window, hardcoreRow); }, "", false, true);
	}
#endif
	addSwitch(_("LEADERBOARDS"), _("Compete in high-score and best time leaderboards (requires hardcore)."), "global.retroachievements.leaderboards", false, nullptr);
	addSwitch(_("VERBOSE MODE"), _("Show achievement progression on game launch and other notifications."), "global.retroachievements.verbose", false, nullptr);
	addSwitch(_("RICH PRESENCE"), "global.retroachievements.richpresence", false);
	addSwitch(_("ENCORE MODE"), _("Unlocked achievements can be earned again."), "global.retroachievements.encore", false, nullptr);
	addSwitch(_("AUTOMATIC SCREENSHOT"), _("Automatically take a screenshot when an achievement is earned."), "global.retroachievements.screenshot", false, nullptr);
	addSwitch(_("CHALLENGE INDICATORS"), _("Shows icons in the bottom right corner when eligible achievements can be earned."), "global.retroachievements.challenge_indicators", false, nullptr);
	// RetroArch's progress tracker is a separate widget from the challenge
	// indicators -- it counts measured progress (34/99 rings) while an
	// indicator marks a live "do X without Y" achievement -- and it had no
	// switch here, so it could not be turned off (maintainer, 2026-09-07).
	// On by default, as RetroArch ships it: absent reads as on, so an
	// upgraded device shows the state it is actually in. Written by hand
	// rather than through addSwitch, whose bool means "store in Settings",
	// not "default" -- the first version passed true there and the value
	// went to es_settings.cfg, where the launch script never looks.
	auto progressTracker = std::make_shared<SwitchComponent>(mWindow);
	progressTracker->setState(SystemConf::getInstance()->getBool("global.retroachievements.progress_tracker", true));
	addWithDescription(_("PROGRESS TRACKER"), _("Shows how far you are toward an achievement while you play."), progressTracker);
	addSaveFunc([progressTracker] { SystemConf::getInstance()->setBool("global.retroachievements.progress_tracker", progressTracker->getState()); });
	addSwitch(_("UNOFFICIAL ACHIEVEMENTS"), _("Enable unlocking of unofficial achievements."), "global.retroachievements.unofficial", false, nullptr);

	// Unlock sound
	auto installedRSounds = ApiSystem::getInstance()->getRetroachievementsSoundsList();
	if (installedRSounds.size() > 0)
	{
		std::string currentSound = SystemConf::getInstance()->get("global.retroachievements.sound");

		auto rsounds_choices = std::make_shared<OptionListComponent<std::string> >(mWindow, _("RETROACHIEVEMENTS UNLOCK SOUND"), false);
		rsounds_choices->add(_("none"), "none", currentSound.empty() || currentSound == "none");

		for (auto snd : installedRSounds)
			rsounds_choices->add(_(Utils::String::toUpper(snd).c_str()), snd, currentSound == snd);

		if (!rsounds_choices->hasSelection())
			rsounds_choices->selectFirstItem();

		addWithLabel(_("UNLOCK SOUND"), rsounds_choices);
		addSaveFunc([rsounds_choices] { SystemConf::getInstance()->set("global.retroachievements.sound", rsounds_choices->getSelected()); });
	}

#if defined(ROCKNIX)
        if (Utils::Platform::GetEnv("DEVICE_ANALOG_STICKS_LED_CONTROL") == "true") {
                // Enable LED Notifications
                auto cheevos_led_enabled = std::make_shared<SwitchComponent>(mWindow);
                bool cheevosledenabled = SystemConf::getInstance()->get("global.retroachievements.leds") == "1";
                cheevos_led_enabled->setState(SystemConf::getInstance()->getBool("global.retroachievements.leds"));
                //s->addWithLabel(_("LED NOTIFICATIONS"), cheevos_led_enabled);
                addWithLabel(_("LED NOTIFICATIONS"), cheevos_led_enabled);
                cheevos_led_enabled->setOnChangedCallback([cheevos_led_enabled] {
                        bool cheevosledenabled = cheevos_led_enabled->getState();
                                SystemConf::getInstance()->set("global.retroachievements.leds", cheevosledenabled ? "1" : "0");
                });
        }
#endif
	// retroachievements_hardcore_mode
	addSwitch(_("SHOW RETROACHIEVEMENTS ENTRY IN MAIN MENU"), _("View your RetroAchievements stats right from the main menu!"), "RetroachievementsMenuitem", true, nullptr);

	addGroup(_("GAME INDEXES"));
	addSwitch(_("INDEX NEW GAMES AT STARTUP"), "CheevosCheckIndexesAtStart", true);
	addEntry(_("INDEX GAMES"), true, [this]
	{
		if (ThreadedHasher::checkCloseIfRunning(mWindow))
			mWindow->pushGui(new GuiHashStart(mWindow, ThreadedHasher::HASH_CHEEVOS_MD5));
	});

	// The switch is the player's choice, and this save writes that choice
	// and nothing else (#175). The sign-in below decides the token only. It
	// used to decide the switch as well: a sign-in that failed because the
	// device was offline -- this page closed on a boot whose first attempt
	// had run before the network was up, so no token was there to skip it
	// -- wrote the switch off, and the RETROACHIEVEMENTS entry left the
	// main menu with nothing on screen to say why.
	//
	// What the player is told depends on why it failed and on what they
	// did. RetroAchievements turned the account down: said every time, the
	// credentials are wrong and no retry changes that. RetroAchievements
	// could not be reached after the switch was turned on or the account
	// changed: said once, with what happens next. Could not be reached and
	// nothing was changed: nothing to say -- the sign-in that runs when the
	// network comes up (NetworkThread) finishes this on its own.
	addSaveFunc([retroachievementsEnabled, retroachievements_enabled, username, password, window]
	{
		bool newState = retroachievements_enabled->getState();
		std::string newUsername = SystemConf::getInstance()->get("global.retroachievements.username");
		std::string newPassword = SystemConf::getInstance()->get("global.retroachievements.password");
		std::string token = SystemConf::getInstance()->get("global.retroachievements.token");

		bool accountChanged = !retroachievementsEnabled || username != newUsername || password != newPassword;
		if (newState && (accountChanged || token.empty()))
		{
			std::string tokenOrError;
			bool refused = false;
			if (RetroAchievements::testAccount(newUsername, newPassword, tokenOrError, &refused))
				SystemConf::getInstance()->set("global.retroachievements.token", tokenOrError);
			else
			{
				SystemConf::getInstance()->set("global.retroachievements.token", "");

				if (refused)
					window->pushGui(new GuiMsgBox(window,
						_("RETROACHIEVEMENTS DIDN'T ACCEPT YOUR SIGN-IN:") + "\n" + tokenOrError + "\n\n"
						+ _("RETROACHIEVEMENTS STAYS ON. CHECK YOUR USERNAME AND PASSWORD, THEN TRY AGAIN."),
						_("OK"), nullptr, GuiMsgBoxIcon::ICON_ERROR));
				else if (accountChanged)
					window->pushGui(new GuiMsgBox(window,
						_("COULDN'T REACH RETROACHIEVEMENTS TO SIGN YOU IN.\n\nRETROACHIEVEMENTS STAYS ON. IT'LL SIGN IN WHEN YOU'RE ONLINE."),
						_("OK")));
				else
					LOG(LogWarning) << "retroachievements: could not reach RetroAchievements for a token (" << tokenOrError << "); the switch stays as set, and the sign-in runs when the network is up";
			}
		}
		else if (!newState)
			SystemConf::getInstance()->set("global.retroachievements.token", "");

		if (SystemConf::getInstance()->setBool("global.retroachievements", newState))
			if (!ThreadedHasher::isRunning() && newState)
				ThreadedHasher::start(window, ThreadedHasher::HASH_CHEEVOS_MD5, false, true);
	});
}
