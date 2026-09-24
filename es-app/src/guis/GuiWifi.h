#pragma once

#include "GuiComponent.h"
#include "components/MenuComponent.h"
#include "WifiText.h"

#include <functional>
#include <string>
#include <vector>

// The Wi-Fi picker behind NETWORK SETTINGS' WI-FI NETWORK row (fork #191,
// renamed from WI-FI SSID by D-UI-071; the
// maintainer's paradigm of 2026-09-15: the row is the network the device is
// on, this list is what is in range). One row per network in range -- the
// one joined now first and marked CONNECTED, the ones NetworkManager holds a
// profile for marked SAVED. A press on a saved row joins it with the key
// NetworkManager has; a press on any other asks for the key and connects,
// which saves it for next time; INPUT MANUALLY takes a hidden network's
// name the same way. Once the device is on a network the page that opened
// the picker is told, so it rebuilds and reads the connection back.
class GuiWifi : public GuiComponent
{
public:
	GuiWifi(Window* window, const std::string& title, const std::function<void()>& onJoined);
	bool input(InputConfig* config, Input input) override;
	std::vector<HelpPrompt> getHelpPrompts() override;

private:
	// The three answers the list is built from, fetched together off the
	// interface thread.
	struct Answer
	{
		std::vector<std::string> inRange;
		std::vector<WifiText::SavedNetwork> saved;
		std::string current;
	};

	void load(const std::vector<WifiText::PickerRow>& rows);
	void addRow(const WifiText::PickerRow& row);
	void onSelect(const WifiText::PickerRow& row);
	void onManualInput();
	void onRefresh(bool rescan = true);
	void join(const std::string& name);
	void askKeyAndConnect(const std::string& name);
	void connect(const std::string& name, const std::string& key);
	void joined(const std::string& name);

	MenuComponent mMenu;
	std::string mTitle;
	std::function<void()> mOnJoined;
	std::vector<WifiText::SavedNetwork> mSaved;
	std::string mCurrent;
	bool mWaitingLoad;
};
