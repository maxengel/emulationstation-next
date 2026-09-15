#pragma once
#ifndef ES_APP_WIFI_TEXT_H
#define ES_APP_WIFI_TEXT_H

// The pure text of the Wi-Fi rows (fork #191): what wifictl prints for the
// interface, read into values the pages can show, with nothing from the
// window, the settings or a script behind it, so es-unit-tests can hold the
// rules (es-app/tests/unit). The thin shells that run wifictl are in
// ApiSystem; the rows themselves are in GuiMenu.
//
// The setting wifi.ssid is the network the player configured last.
// NetworkManager keeps a profile for every network the device has joined and
// autoconnects to whichever remembered one is in range, so what the device is
// on and what the setting says are two different facts; these are the shapes
// wifictl reports the first in.

#include <string>
#include <vector>

namespace WifiText
{
	// A network NetworkManager remembers, as one line of `wifictl saved`
	// reads: <name><TAB>active or <name><TAB>saved.
	struct SavedNetwork
	{
		std::string name;
		bool inUse = false;
	};

	// One line of `wifictl saved`. False for a line that is not a network:
	// no tab, an empty name, a flag that is neither word. A trailing CR is
	// tolerated. The name keeps its case and every space in it -- a network's
	// name is case-sensitive, and is what the player recognises it by -- and
	// the flag is read after the last tab, so a tab inside a name still
	// leaves the flag whole.
	bool parseSavedLine(const std::string& line, SavedNetwork& network);

	// Every network in the lines, in the order given (nmcli puts the one in
	// use first); lines that are not networks are passed over.
	std::vector<SavedNetwork> parseSaved(const std::vector<std::string>& lines);

	// The first line of `wifictl current`, the SSID the device is joined to;
	// empty when there is none. Only a trailing CR is trimmed: a name may
	// begin or end with a space.
	std::string parseCurrent(const std::vector<std::string>& lines);

	// What `wifictl forget` printed: "forgotten" when the profile went, then
	// "disconnected" on a second line when it was the one in use. A
	// "disconnected" with no "forgotten" before it is not a forget that
	// happened; nothing printed is not one either -- a caller reads the
	// word, not the absence of an error.
	struct ForgetOutcome
	{
		bool forgotten = false;
		bool disconnected = false;
	};
	ForgetOutcome parseForget(const std::vector<std::string>& lines);
}

#endif // ES_APP_WIFI_TEXT_H
