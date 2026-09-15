// The Wi-Fi rows' pure text (fork #191), checked without a device.
//
// Runs against es-app/src/WifiText.cpp alone: the lines wifictl prints for
// the interface -- saved, current, forget -- including the ones nobody
// meant to write. A parser is judged on its junk, so most of these are junk.

#include "doctest/doctest.h"

#include "WifiText.h"

#include <string>
#include <vector>

using namespace WifiText;

TEST_CASE("parseSaved: two remembered networks, the one in use first, as wifictl saved prints them")
{
	auto networks = parseSaved({ "Home Wi-Fi\tactive", "Cafe: Guest\tsaved" });
	REQUIRE(networks.size() == 2);
	CHECK(networks[0].name == "Home Wi-Fi");
	CHECK(networks[0].inUse);
	CHECK(networks[1].name == "Cafe: Guest");
	CHECK_FALSE(networks[1].inUse);
}

TEST_CASE("parseSavedLine keeps a name as NetworkManager has it: case, inner spaces, a colon, an edge space")
{
	SavedNetwork network;
	CHECK(parseSavedLine("MyHome_5g\tsaved", network));
	CHECK(network.name == "MyHome_5g");            // not upper-cased: the name is case-sensitive
	CHECK(parseSavedLine("  two  spaces \tactive", network));
	CHECK(network.name == "  two  spaces ");        // every space kept, the flag read after the tab
	CHECK(network.inUse);
	CHECK(parseSavedLine("a\tb\tsaved", network));  // a tab inside a name: the flag is after the last one
	CHECK(network.name == "a\tb");
	CHECK_FALSE(network.inUse);
}

TEST_CASE("parseSavedLine tolerates a trailing CR and nothing else that is not a network")
{
	SavedNetwork network;
	CHECK(parseSavedLine("Home\tactive\r", network));
	CHECK(network.name == "Home");
	CHECK(network.inUse);

	CHECK_FALSE(parseSavedLine("", network));
	CHECK_FALSE(parseSavedLine("Home", network));               // no tab
	CHECK_FALSE(parseSavedLine("\tactive", network));           // no name
	CHECK_FALSE(parseSavedLine("Home\tyes", network));          // nmcli's own word, not wifictl's
	CHECK_FALSE(parseSavedLine("Home\tACTIVE", network));       // the flag is exact
	CHECK_FALSE(parseSavedLine("Home\tactive extra", network));
	CHECK_FALSE(parseSavedLine("Home\t", network));
}

TEST_CASE("parseSaved passes junk lines over and keeps the order of the rest")
{
	auto networks = parseSaved({ "", "Error: NetworkManager is not running.", "B\tsaved", "junk\tmaybe", "A\tactive", "\tsaved" });
	REQUIRE(networks.size() == 2);
	CHECK(networks[0].name == "B");
	CHECK(networks[1].name == "A");
	CHECK(networks[1].inUse);
	CHECK(parseSaved({}).empty());
	CHECK(parseSaved({ "", "\r" }).empty());
}

TEST_CASE("parseCurrent: the first line is the SSID, CR dropped, spaces kept; none is empty")
{
	CHECK(parseCurrent({ "Home Wi-Fi" }) == "Home Wi-Fi");
	CHECK(parseCurrent({ "Home Wi-Fi\r" }) == "Home Wi-Fi");
	CHECK(parseCurrent({ " edge " }) == " edge ");
	CHECK(parseCurrent({ "", "Late" }) == "Late");      // an empty first line is not an answer
	CHECK(parseCurrent({}) == "");
	CHECK(parseCurrent({ "", "\r" }) == "");
}

TEST_CASE("parseForget reads the word, not the absence of an error")
{
	auto both = parseForget({ "forgotten", "disconnected" });
	CHECK(both.forgotten);
	CHECK(both.disconnected);

	auto one = parseForget({ "forgotten" });
	CHECK(one.forgotten);
	CHECK_FALSE(one.disconnected);

	auto none = parseForget({});
	CHECK_FALSE(none.forgotten);
	CHECK_FALSE(none.disconnected);

	// A "disconnected" with nothing forgotten before it is not a forget.
	auto orphan = parseForget({ "disconnected" });
	CHECK_FALSE(orphan.forgotten);
	CHECK_FALSE(orphan.disconnected);

	auto junk = parseForget({ "Error: unknown connection 'x'.", "FORGOTTEN", "forgotten " });
	CHECK_FALSE(junk.forgotten);

	auto cr = parseForget({ "forgotten\r", "disconnected\r" });
	CHECK(cr.forgotten);
	CHECK(cr.disconnected);
}

TEST_CASE("ssidLine says nothing while the device is on the configured network: the value on the right already names it")
{
	CHECK(ssidLine(true, "Home Wi-Fi", "Home Wi-Fi") == SsidLine::None);
}

TEST_CASE("ssidLine names the joined network only when it is another one than the row's value (fork #191)")
{
	CHECK(ssidLine(true, "Cafe: Guest", "Home Wi-Fi") == SsidLine::ConnectedTo);
	CHECK(ssidLine(true, "home wi-fi", "Home Wi-Fi") == SsidLine::ConnectedTo);  // a name is case-sensitive
	CHECK(ssidLine(true, "Home Wi-Fi", "") == SsidLine::ConnectedTo);           // nothing configured, yet joined
}

TEST_CASE("ssidLine keeps 'joined to none' and 'no answer' apart, whatever the setting says")
{
	CHECK(ssidLine(true, "", "Home Wi-Fi") == SsidLine::NotConnected);
	CHECK(ssidLine(true, "", "") == SsidLine::NotConnected);
	CHECK(ssidLine(false, "", "Home Wi-Fi") == SsidLine::CouldNotCheck);
	CHECK(ssidLine(false, "Home Wi-Fi", "Home Wi-Fi") == SsidLine::CouldNotCheck);  // a name with no answer behind it is no answer
}
