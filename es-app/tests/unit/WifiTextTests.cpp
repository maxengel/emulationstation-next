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

TEST_CASE("pickerRows: the joined network first, the rest in the scan's order, the saved ones marked")
{
	auto rows = pickerRows({ "Cafe: Guest", "Home Wi-Fi", "Library" }, { { "Home Wi-Fi", true }, { "Cafe: Guest", false } }, "Home Wi-Fi");
	REQUIRE(rows.size() == 3);
	CHECK(rows[0].name == "Home Wi-Fi");
	CHECK(rows[0].connected);
	CHECK(rows[0].saved);
	CHECK(rows[1].name == "Cafe: Guest");
	CHECK_FALSE(rows[1].connected);
	CHECK(rows[1].saved);
	CHECK(rows[2].name == "Library");
	CHECK_FALSE(rows[2].connected);
	CHECK_FALSE(rows[2].saved);
}

TEST_CASE("pickerRows: a saved network out of range is not a row; the joined one is, even when the scan missed it")
{
	auto rows = pickerRows({ "Library" }, { { "Home Wi-Fi", true }, { "Office", false } }, "Home Wi-Fi");
	REQUIRE(rows.size() == 2);
	CHECK(rows[0].name == "Home Wi-Fi");
	CHECK(rows[0].connected);
	CHECK(rows[1].name == "Library");
	CHECK_FALSE(rows[1].saved);
}

TEST_CASE("pickerRows drops empty names and repeats, keeps a name's case, and has no connected row when the device is on none")
{
	auto rows = pickerRows({ "", "Cafe: Guest\r", "Cafe: Guest", "cafe: guest" }, {}, "");
	REQUIRE(rows.size() == 2);
	CHECK(rows[0].name == "Cafe: Guest");
	CHECK_FALSE(rows[0].connected);
	CHECK_FALSE(rows[0].saved);
	CHECK(rows[1].name == "cafe: guest");
}

TEST_CASE("parseJoin reads the word, not the absence of an error")
{
	CHECK(parseJoin({ "joined" }));
	CHECK(parseJoin({ "joined\r" }));
	CHECK_FALSE(parseJoin({}));
	CHECK_FALSE(parseJoin({ "" }));
	CHECK_FALSE(parseJoin({ "Error: Connection activation failed." }));
}

// ------------------------------------------------------ #308 F-WF-03/05/06/08

TEST_CASE("pickerRows: a saved list that could not be asked leaves every row unknown, not unsaved (#308 2 claude F-WF-03, gpt F-WF-06)")
{
	// wifictl saved exits 1 when NetworkManager cannot be asked, "so a
	// caller cannot read a silence as none"; the picker read it as none, and
	// a press on the player's own network asked for a key and rebuilt its
	// profile from what was typed.
	auto rows = pickerRows({ "Home Wi-Fi", "Library" }, {}, "Home Wi-Fi", false, true);
	REQUIRE(rows.size() == 2);
	CHECK(rows[0].connected);
	CHECK_FALSE(rows[0].savedKnown);
	CHECK_FALSE(rows[1].savedKnown);
	CHECK_FALSE(rows[1].saved);

	// And an answered list is known, as it always was.
	auto known = pickerRows({ "Library" }, {}, "", true, true);
	REQUIRE(known.size() == 1);
	CHECK(known[0].savedKnown);
}

TEST_CASE("pickerRows: a current that could not be asked takes the connected row from the saved list's active profile (#308 F-WF-03)")
{
	// wifictl current exits 2 when NetworkManager cannot be asked; the saved
	// list already says which profile is active.
	auto rows = pickerRows({ "Cafe: Guest", "Home Wi-Fi" }, { { "Home Wi-Fi", true }, { "Cafe: Guest", false } }, "", true, false);
	REQUIRE(rows.size() == 2);
	CHECK(rows[0].name == "Home Wi-Fi");
	CHECK(rows[0].connected);
	CHECK(rows[0].saved);
	CHECK(rows[1].name == "Cafe: Guest");
	CHECK_FALSE(rows[1].connected);
}

TEST_CASE("a press: the connected row is checked by a join, a saved one joins, an unknown one is asked again (#308 F-WF-03/05/06)")
{
	// The connected row closed the picker on the snapshot it was built from,
	// with no look at whether the device was still on it (gpt F-WF-05).
	// wifictl join answers "joined" at once for the active profile and brings
	// back one that has dropped, so a press on it goes that way.
	PickerRow connected{ "Home Wi-Fi", true, true };
	CHECK(pressAction(connected) == PressAction::Join);
	PickerRow saved{ "Cafe: Guest", true, false };
	CHECK(pressAction(saved) == PressAction::Join);
	PickerRow other{ "Library", false, false };
	CHECK(pressAction(other) == PressAction::AskKey);
	PickerRow unknown{ "Library", false, false, false };
	CHECK(pressAction(unknown) == PressAction::CheckAgain);
	PickerRow unknownConnected{ "Home Wi-Fi", false, true, false };
	CHECK(pressAction(unknownConnected) == PressAction::Join);   // the device is on it: it has a profile

	// INPUT MANUALLY, the same rules for a typed name.
	const std::vector<SavedNetwork> profiles = { { "Home Wi-Fi", true }, { "Hidden", false } };
	CHECK(manualAction("Home Wi-Fi", "Home Wi-Fi", profiles, true) == PressAction::Join);
	CHECK(manualAction("Hidden", "Home Wi-Fi", profiles, true) == PressAction::Join);
	CHECK(manualAction("Guest", "Home Wi-Fi", profiles, true) == PressAction::AskKey);
	CHECK(manualAction("Guest", "Home Wi-Fi", {}, false) == PressAction::CheckAgain);
	CHECK(manualAction("", "", {}, true) == PressAction::AskKey);
}

TEST_CASE("the joined toast is <subject> : <outcome>, the name as it is (#308 2 claude F-WF-08, gpt F-WF-08)")
{
	// It was "CONNECTED TO" + name: a translated fragment a translation could
	// not move, and not the toast shape the style guide sets.
	CHECK(joinedNotice("Home Wi-Fi", "CONNECTED") == "Home Wi-Fi : CONNECTED");
	CHECK(joinedNotice("cafe guest", "CONNECT\xC3\x89") == "cafe guest : CONNECT\xC3\x89");
}


TEST_CASE("a join that did not happen: NetworkManager not answering is not a key that changed (#308 2 claude F-WF-03, gpt F-WF-06)")
{
	// wifictl join: exit 2 when NetworkManager could not be asked -- nothing
	// was tried, and the key is not in question. The picker told the player
	// to forget the network and join it again with a new key.
	CHECK(joinFailure(2) == JoinFailure::ServiceNotAnswering);
	// Exit 1: the profile would not come up, or the name is not a saved
	// network -- the key may be why. 124 is the timeout's own bound on the
	// activation: the same may.
	CHECK(joinFailure(1) == JoinFailure::MayBeKey);
	CHECK(joinFailure(124) == JoinFailure::MayBeKey);
	// A run that exited 0 without printing "joined" (parseJoin) is not a
	// join; nothing in it points away from the key either.
	CHECK(joinFailure(0) == JoinFailure::MayBeKey);
}
