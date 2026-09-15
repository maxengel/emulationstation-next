#include "guis/GuiWifi.h"
#include "guis/GuiMsgBox.h"
#include "guis/GuiTextEditPopup.h"
#include "guis/GuiTextEditPopupKeyboard.h"
#include "components/TextComponent.h"
#include "ApiSystem.h"
#include "GuiLoading.h"
#include "LocaleES.h"
#include "Log.h"
#include "Settings.h"
#include "SystemConf.h"
#include "ThemeData.h"
#include "Window.h"
#include "utils/StringUtil.h"

GuiWifi::GuiWifi(Window* window, const std::string& title, const std::function<void()>& onJoined)
	: GuiComponent(window), mMenu(window, title.c_str()), mTitle(title), mOnJoined(onJoined), mWaitingLoad(false)
{
	addChild(&mMenu);

	// Fetched off the interface thread. wifictl list is itself a rescan --
	// it waits for the adapter, asks NetworkManager to scan, sleeps, then
	// lists -- so this constructor used to run up to twenty seconds of
	// nmcli with nothing drawn, and with NetworkManager unresponsive (fork
	// #102) nothing bounded it at all. Posted rather than pushed here,
	// because the page has to be on the stack before anything can go over
	// it; an empty first answer still turns into the full scan it always did.
	mWindow->postToUiThread([this]() { onRefresh(false); });

	mMenu.addButton(_("REFRESH"), "refresh", [&] { onRefresh(); });
	mMenu.addButton(_("INPUT MANUALLY"), "manual input", [&] { onManualInput(); });
	mMenu.addButton(_("BACK"), "back", [&] { delete this; });

	if (Renderer::ScreenSettings::fullScreenMenus())
		mMenu.setPosition((Renderer::getScreenWidth() - mMenu.getSize().x()) / 2, (Renderer::getScreenHeight() - mMenu.getSize().y()) / 2);
	else
		mMenu.setPosition((Renderer::getScreenWidth() - mMenu.getSize().x()) / 2, Renderer::getScreenHeight() * 0.15f);
}

void GuiWifi::load(const std::vector<WifiText::PickerRow>& rows)
{
	mMenu.clear();

	if (rows.empty())
		mMenu.addEntry(_("NO WI-FI NETWORKS FOUND"), false, [this] { onRefresh(); });
	else
		for (const auto& row : rows)
			addRow(row);

	mMenu.updateSize();

	if (Renderer::ScreenSettings::fullScreenMenus())
		mMenu.setPosition((Renderer::getScreenWidth() - mMenu.getSize().x()) / 2, (Renderer::getScreenHeight() - mMenu.getSize().y()) / 2);

	mWaitingLoad = false;
}

// The name as NetworkManager has it -- case and spaces kept, since a
// network's name is what the player recognises it by (addEntry would
// upper-case it) -- and, on the right at the row's own weight, CONNECTED for
// the network the device is on or SAVED for one it holds a profile for: the
// shape of MANAGE SAVED NETWORKS' IN USE, a fact beside the action.
void GuiWifi::addRow(const WifiText::PickerRow& network)
{
	auto theme = ThemeData::getMenuTheme();
	ComponentListRow row;

	auto name = std::make_shared<TextComponent>(mWindow, network.name, theme->Text.font, theme->Text.color);
	if (EsLocale::isRTL())
		name->setHorizontalAlignment(Alignment::ALIGN_RIGHT);
	row.addElement(name, true);

	if (network.connected || network.saved)
	{
		const std::string mark = network.connected ? _("CONNECTED") : _("SAVED");
		auto label = std::make_shared<TextComponent>(mWindow, mark, theme->Text.font, theme->Text.color, Alignment::ALIGN_RIGHT);
		label->setSize(theme->Text.font->sizeText(mark + "  ").x(), 0);
		row.addElement(label, false);
	}

	const WifiText::PickerRow chosen = network;
	row.makeAcceptInputHandler([this, chosen] { onSelect(chosen); });
	mMenu.addRow(row);
}

// A press: the network the device is on needs nothing; a saved one joins
// with the key NetworkManager holds; any other is asked for its key.
void GuiWifi::onSelect(const WifiText::PickerRow& row)
{
	if (mWaitingLoad)
		return;

	if (row.connected)
	{
		delete this;
		return;
	}

	if (row.saved)
		join(row.name);
	else
		askKeyAndConnect(row.name);
}

// INPUT MANUALLY: a hidden network's name, taken as a row would be -- a
// name NetworkManager holds a profile for joins, the one the device is on
// needs nothing, any other is asked for its key. Not trimmed: a name may
// begin or end with a space, and it is matched as NetworkManager has it.
void GuiWifi::onManualInput()
{
	auto onName = [this](const std::string& name)
	{
		if (name.empty())
			return;
		if (name == mCurrent)
		{
			delete this;
			return;
		}
		for (const auto& saved : mSaved)
		{
			if (saved.name == name)
			{
				join(name);
				return;
			}
		}
		askKeyAndConnect(name);
	};

	if (Settings::getInstance()->getBool("UseOSK"))
		mWindow->pushGui(new GuiTextEditPopupKeyboard(mWindow, mTitle, "", onName, false));
	else
		mWindow->pushGui(new GuiTextEditPopup(mWindow, mTitle, "", onName, false));
}

// A saved network: its profile, with the key NetworkManager holds, behind
// the spinner -- wifictl join waits up to 90 s for the association, as
// connect does, and the screen would otherwise freeze for it. The script
// also moves the settings wifi.ssid and wifi.key onto the joined network,
// so the paths that connect from the settings (the WI-FI KEY row, the
// restore wizard, the ENABLE WI-FI switch) still name the network the
// player is on; SystemConf is re-read so the interface sees the same file.
void GuiWifi::join(const std::string& name)
{
	Window* window = mWindow;
	mWaitingLoad = true;
	LOG(LogInfo) << "wifi picker: joining the saved network " << name;
	window->pushGui(new GuiLoading<bool>(window, _("CONNECTING TO WI-FI"),
		[name](IGuiLoadingHandler*) { return ApiSystem::getInstance()->joinWifiNetwork(name); },
		[this, window, name](bool ok)
		{
			mWaitingLoad = false;
			if (!ok)
			{
				LOG(LogWarning) << "wifi picker: could not join the saved network " << name;
				window->pushGui(new GuiMsgBox(window,
					Utils::String::format(_("COULDN'T CONNECT TO %s.").c_str(), name.c_str()) + "\n\n"
					+ _("IF ITS KEY HAS CHANGED, FORGET IT UNDER MANAGE SAVED NETWORKS AND JOIN IT AGAIN WITH THE NEW KEY."), _("OK")));
				return;
			}
			SystemConf::getInstance()->loadSystemConf();
			joined(name);
		}));
}

// Any other network: its key first -- empty for an open network -- then
// the connection wifictl connect makes, which leaves a profile behind for
// next time. On success the settings take the network and its key, as they
// did when the row edited them: the network configured last, for the paths
// that connect from the settings.
void GuiWifi::askKeyAndConnect(const std::string& name)
{
	auto onKey = [this, name](const std::string& key) { connect(name, key); };

	if (Settings::getInstance()->getBool("UseOSK"))
		mWindow->pushGui(new GuiTextEditPopupKeyboard(mWindow, _("WI-FI KEY"), "", onKey, false));
	else
		mWindow->pushGui(new GuiTextEditPopup(mWindow, _("WI-FI KEY"), "", onKey, false));
}

void GuiWifi::connect(const std::string& name, const std::string& key)
{
	Window* window = mWindow;
	mWaitingLoad = true;
	LOG(LogInfo) << "wifi picker: connecting to " << name;
#if !WIN32
	const std::string country = SystemConf::getInstance()->get("wifi.country");
	auto apply = [name, key, country](IGuiLoadingHandler*) { return ApiSystem::getInstance()->enableWifi(name, key, country); };
#else
	auto apply = [name, key](IGuiLoadingHandler*) { return ApiSystem::getInstance()->enableWifi(name, key); };
#endif
	window->pushGui(new GuiLoading<bool>(window, _("CONNECTING TO WI-FI"), apply,
		[this, window, name, key](bool ok)
		{
			mWaitingLoad = false;
			if (!ok)
			{
				LOG(LogWarning) << "wifi picker: could not connect to " << name;
				window->pushGui(new GuiMsgBox(window,
					Utils::String::format(_("COULDN'T CONNECT TO %s.").c_str(), name.c_str()) + "\n\n" + _("CHECK THE KEY AND TRY AGAIN."), _("OK")));
				return;
			}
			SystemConf::getInstance()->set("wifi.ssid", name);
			SystemConf::getInstance()->set("wifi.key", key);
			SystemConf::getInstance()->saveSystemConf();
			joined(name);
		}));
}

// The picker's job is done: the toast says so over whatever the page
// returns to, and the page that opened the picker rebuilds to read the
// connection back. The callback is copied out first -- it belongs to that
// page, and this component is gone before it runs.
void GuiWifi::joined(const std::string& name)
{
	Window* window = mWindow;
	auto onJoined = mOnJoined;
	window->displayNotificationMessage(_U("\uF058  ") + _("CONNECTED TO") + " " + name);
	delete this;
	if (onJoined != nullptr)
		onJoined();
}

bool GuiWifi::input(InputConfig* config, Input input)
{
	if (GuiComponent::input(config, input))
		return true;

	if (input.value != 0 && config->isMappedTo(BUTTON_BACK, input))
	{
		if (!mWaitingLoad)
			delete this;

		return true;
	}

	return false;
}

std::vector<HelpPrompt> GuiWifi::getHelpPrompts()
{
	std::vector<HelpPrompt> prompts = mMenu.getHelpPrompts();
	prompts.push_back(HelpPrompt(BUTTON_BACK, _("BACK")));
	return prompts;
}

// rescan: ask NetworkManager for a fresh scan first (REFRESH, and an empty
// first list); false lists what it already knows, which is what the page
// opens with. The saved profiles and the network joined now come with the
// same wait, behind the spinner, each time-boxed in ApiSystem.
void GuiWifi::onRefresh(bool rescan)
{
	Window* window = mWindow;
	mWaitingLoad = true;

	window->pushGui(new GuiLoading<Answer>(window, _("SEARCHING WI-FI NETWORKS"),
		[rescan](IGuiLoadingHandler*)
		{
			Answer answer;
			answer.inRange = ApiSystem::getInstance()->getWifiNetworks(rescan);
			ApiSystem::getInstance()->getSavedWifiNetworks(answer.saved);
			ApiSystem::getInstance()->getCurrentWifiSsid(answer.current);
			return answer;
		},
		[this, rescan](Answer answer)
		{
			mWaitingLoad = false;
			if (answer.inRange.empty() && !rescan)
			{
				onRefresh(true);
				return;
			}
			mSaved = answer.saved;
			mCurrent = answer.current;
			load(WifiText::pickerRows(answer.inRange, answer.saved, answer.current));
		}));
}
