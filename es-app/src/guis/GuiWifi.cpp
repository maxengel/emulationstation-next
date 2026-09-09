#include "guis/GuiBackup.h"
#include "guis/GuiMsgBox.h"
#include "Window.h"
#include <string>
#include "Log.h"
#include "Settings.h"
#include "ApiSystem.h"
#include "LocaleES.h"
#include "GuiWifi.h"
#include "guis/GuiTextEditPopup.h"
#include "guis/GuiTextEditPopupKeyboard.h"
#include "GuiLoading.h"

GuiWifi::GuiWifi(Window* window, const std::string title, std::string data, const std::function<void(std::string)>& onsave)
	: GuiComponent(window), mMenu(window, title.c_str())
{
	mTitle = title;
	mInitialData = data;
	mSaveFunction = onsave;
	mWaitingLoad = false;

	auto theme = ThemeData::getMenuTheme();

	addChild(&mMenu);

	// Fetched off the interface thread. wifictl list is itself a rescan --
	// it waits for the adapter, asks NetworkManager to scan, sleeps, then
	// lists -- so this constructor used to run up to twenty seconds of
	// nmcli with nothing drawn, and with NetworkManager unresponsive (fork
	// #102) nothing bounded it at all. The spinner REFRESH has always used
	// runs it now. Posted rather than pushed here, because the page has to
	// be on the stack before anything can go over it; an empty first answer
	// still turns into the full scan it always did.
	mWindow->postToUiThread([this]() { onRefresh(false); });

	mMenu.addButton(_("REFRESH"), "refresh", [&] { onRefresh(); });
	mMenu.addButton(_("INPUT MANUALLY"), "manual input", [&] { onManualInput(); });
	mMenu.addButton(_("BACK"), "back", [&] { delete this; });

	if (Renderer::ScreenSettings::fullScreenMenus())
		mMenu.setPosition((Renderer::getScreenWidth() - mMenu.getSize().x()) / 2, (Renderer::getScreenHeight() - mMenu.getSize().y()) / 2);
	else
		mMenu.setPosition((Renderer::getScreenWidth() - mMenu.getSize().x()) / 2, Renderer::getScreenHeight() * 0.15f);
}

void GuiWifi::load(std::vector<std::string> ssids)
{
	mMenu.clear();

	if (ssids.size() == 0)
		mMenu.addEntry(_("NO WI-FI NETWORKS FOUND"), false, [this] { onRefresh(); });
	else
	{
		for (auto ssid : ssids)
			mMenu.addEntry(ssid, false, [this, ssid]() { GuiWifi::onSave(ssid); });
	}

	mMenu.updateSize();

	if (Renderer::ScreenSettings::fullScreenMenus())
		mMenu.setPosition((Renderer::getScreenWidth() - mMenu.getSize().x()) / 2, (Renderer::getScreenHeight() - mMenu.getSize().y()) / 2);

	mWaitingLoad = false;
}

void GuiWifi::onManualInput()
{
	if (Settings::getInstance()->getBool("UseOSK"))
		mWindow->pushGui(new GuiTextEditPopupKeyboard(mWindow, mTitle, mInitialData, [this](const std::string& value) { onSave(value); }, false));
	else
		mWindow->pushGui(new GuiTextEditPopup(mWindow, mTitle, mInitialData, [this](const std::string& value) { onSave(value); }, false));
}

void GuiWifi::onSave(const std::string& value)
{
	if (mWaitingLoad)
		return;

	mSaveFunction(value);
	delete this;
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
// opens with. Either way the wait happens behind the spinner, time-boxed in
// ApiSystem::getWifiNetworks.
void GuiWifi::onRefresh(bool rescan)
{		
	Window* window = mWindow;

	mWindow->pushGui(new GuiLoading<std::vector<std::string>>(mWindow, _("SEARCHING WI-FI NETWORKS"), 
		[this, window, rescan](auto gui)
		{
			mWaitingLoad = true;
			return ApiSystem::getInstance()->getWifiNetworks(rescan);
		},
		[this, window, rescan](std::vector<std::string> ssids)
		{
			mWaitingLoad = false;
			if (ssids.empty() && !rescan)
			{
				onRefresh(true);
				return;
			}
			load(ssids);
		}));	
}
