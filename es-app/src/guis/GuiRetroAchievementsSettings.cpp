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
#include "components/SwitchComponent.h"
#include "components/OptionListComponent.h"

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
	// OFFLINE RETROACHIEVEMENTS (fork #165; D-RA-001, D-RA-002). One
	// system-wide switch, off by default, backed by raofflineproxy-ctl:
	// enable starts the RAOfflineProxy service, records hardcore as it was
	// and turns it off -- the proxy is casual-only and refuses hardcore
	// awards -- and sets the toggle the launch scripts read at every game
	// start; disable stops the service and puts hardcore back as recorded.
	// Written by hand rather than through addSwitch: the script is the
	// writer, not this page's save, and turning it on asks first, because it
	// changes the HARDCORE MODE row above (never silently). The script's
	// last stdout line, hardcore=<0|1>, is what that row is set from
	// afterwards, so the row shows what system.cfg holds; the same values
	// are mirrored into SystemConf so the page's own save at close writes
	// what the script wrote rather than what it read at open.
	if (Utils::FileSystem::exists("/usr/bin/raofflineproxy-ctl"))
	{
		auto offline = std::make_shared<SwitchComponent>(mWindow);
		offline->setState(SystemConf::getInstance()->getBool("global.retroachievements.offlineproxy"));
		// The line under the label is sentence case like every description on
		// this page (HARDCORE MODE's "Disable loading states, ..."); the label
		// stays UPPERCASE like its siblings (#166).
		addWithDescription(_("OFFLINE RETROACHIEVEMENTS"), _("Beta. Casual achievements only, even without a connection."), offline);

		// Raw pointers on purpose: the callback lives inside the switch it
		// captures, and the HARDCORE MODE switch is a row of the same page,
		// so a shared_ptr here would be a cycle that keeps the page alive
		// forever. setState fires the change callback too, so a revert made
		// from inside it would re-enter it: quiet while the code, not the
		// player, sets the state.
		SwitchComponent* offlineRow = offline.get();
		SwitchComponent* hardcoreRow = hardcore.get();
		auto quiet = std::make_shared<bool>(false);
		auto setQuietly = [offlineRow, quiet](bool state) { *quiet = true; offlineRow->setState(state); *quiet = false; };

		auto apply = [window, offlineRow, hardcoreRow, setQuietly](bool on)
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
					on ? _("OFFLINE RETROACHIEVEMENTS COULDN'T BE TURNED ON.") : _("OFFLINE RETROACHIEVEMENTS COULDN'T BE TURNED OFF."),
					_("OK"), nullptr, GuiMsgBoxIcon::ICON_ERROR));
				return;
			}
			bool hardcoreNow = (last == "hardcore=1");
			hardcoreRow->setState(hardcoreNow);
			SystemConf::getInstance()->set("global.retroachievements.offlineproxy", on ? "1" : "0");
			SystemConf::getInstance()->set("global.retroachievements.hardcore", hardcoreNow ? "1" : "0");
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

			// NOT NOW is the last button, so B answers NOT NOW (GuiMsgBox's
			// accelerator) and the switch goes back to off.
			window->pushGui(new GuiMsgBox(window,
				_("THIS IS A BETA FEATURE. IT WORKS FOR CASUAL ACHIEVEMENTS ONLY, AND TURNING IT ON TURNS HARDCORE MODE OFF.") + "\n\n" +
				_("ACHIEVEMENTS YOU EARN OFFLINE ARE SENT TO RETROACHIEVEMENTS WHEN YOU'RE BACK ONLINE."),
				_("TURN ON"), [apply] { apply(true); },
				_("NOT NOW"), [setQuietly] { setQuietly(false); }));
		});
	}

	// RetroArch's disconnected badge, explained where the RetroAchievements
	// choices are made (fork #162): rcheevos shows it while an award or a
	// score is waiting to reach the server, and says nothing about what it
	// means.
	setSubTitle(_("!RA! IN A GAME'S CORNER MEANS AN ACHIEVEMENT HASN'T REACHED RETROACHIEVEMENTS YET."));
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

	addSaveFunc([retroachievementsEnabled, retroachievements_enabled, username, password, window]
	{
		bool newState = retroachievements_enabled->getState();
		std::string newUsername = SystemConf::getInstance()->get("global.retroachievements.username");
		std::string newPassword = SystemConf::getInstance()->get("global.retroachievements.password");
		std::string token = SystemConf::getInstance()->get("global.retroachievements.token");

		if (newState && (!retroachievementsEnabled || username != newUsername || password != newPassword || token.empty()))
		{
			std::string tokenOrError;
			if (RetroAchievements::testAccount(newUsername, newPassword, tokenOrError))
			{
				SystemConf::getInstance()->set("global.retroachievements.token", tokenOrError);
			}
			else
			{
				SystemConf::getInstance()->set("global.retroachievements.token", "");

				window->pushGui(new GuiMsgBox(window, _("UNABLE TO ACTIVATE RETROACHIEVEMENTS:") + "\n" + tokenOrError, _("OK"), nullptr, GuiMsgBoxIcon::ICON_ERROR));
				retroachievements_enabled->setState(false);
				newState = false;
			}
		}
		else if (!newState)
			SystemConf::getInstance()->set("global.retroachievements.token", "");

		if (SystemConf::getInstance()->setBool("global.retroachievements", newState))
			if (!ThreadedHasher::isRunning() && newState)
				ThreadedHasher::start(window, ThreadedHasher::HASH_CHEEVOS_MD5, false, true);
	});
}
