#include "doctest/doctest.h"

#include "CaptureRotationText.h"

// What RetroArch logged and configured, as the quarter turns the display
// gave a game's frame (fork #245, D-UI-081).
TEST_CASE("the last SET_ROTATION line of a launch log gives the core's turns")
{
	CHECK(CaptureRotationText::turnsFromLog("[INFO] [Environ] SET_ROTATION: \"1\" (90 deg).\n") == 1);
	CHECK(CaptureRotationText::turnsFromLog("[INFO] [Environ] SET_ROTATION: \"3\" (270 deg).\n") == 3);
	CHECK(CaptureRotationText::turnsFromLog("[INFO] [Environ] SET_ROTATION: \"0\" (0 deg).\n") == 0);
	// the last one counts: a core that changed its mind
	CHECK(CaptureRotationText::turnsFromLog("SET_ROTATION: \"1\" (90 deg).\nsomething\nSET_ROTATION: \"0\" (0 deg).\n") == 0);
	// a log with no request is not a request for 0
	CHECK(CaptureRotationText::turnsFromLog("[INFO] [Core]: Content loaded.\n") == -1);
	CHECK(CaptureRotationText::turnsFromLog("") == -1);
	CHECK(CaptureRotationText::turnsFromLog("SET_ROTATION: \"x\"") == -1);
}

TEST_CASE("the display's turn is the core's, unless rotation is forbidden, plus the player's own")
{
	CHECK(CaptureRotationText::fold(1, "video_allow_rotate = \"true\"\nvideo_rotation = \"0\"\n") == 1);
	CHECK(CaptureRotationText::fold(1, "video_allow_rotate = \"false\"\n") == 0);
	CHECK(CaptureRotationText::fold(1, "video_rotation = \"1\"\n") == 2);
	CHECK(CaptureRotationText::fold(3, "video_rotation = \"2\"\n") == 1);
	CHECK(CaptureRotationText::fold(0, "video_rotation = \"1\"\n") == 1);
	CHECK(CaptureRotationText::fold(-1, "") == 0);
	CHECK(CaptureRotationText::fold(1, "") == 1);
	// a key that merely contains the name does not count
	CHECK(CaptureRotationText::fold(1, "menu_video_allow_rotate = \"false\"\n") == 1);
	CHECK(CaptureRotationText::fold(1, "# video_allow_rotate = \"false\"\n") == 1);
}

TEST_CASE("the record is one digit and reads back, and anything else reads as none")
{
	CHECK(CaptureRotationText::recordText(1) == "turns=1\n");
	CHECK(CaptureRotationText::recordText(5) == "turns=1\n");
	CHECK(CaptureRotationText::recordText(-1) == "turns=3\n");
	// longer than the three bytes readAllText's byte-order-mark check reads
	CHECK(CaptureRotationText::recordText(0).size() > 3);
	CHECK(CaptureRotationText::parseRecord("turns=1\n") == 1);
	CHECK(CaptureRotationText::parseRecord("turns=3") == 3);
	CHECK(CaptureRotationText::parseRecord("1\n") == 1);
	CHECK(CaptureRotationText::parseRecord("  3") == 3);
	CHECK(CaptureRotationText::parseRecord("turns=x") == 0);
	CHECK(CaptureRotationText::parseRecord("") == 0);
	CHECK(CaptureRotationText::parseRecord("9") == 0);
	CHECK(CaptureRotationText::parseRecord("north") == 0);
}

TEST_CASE("the core's table gives a game its turn by ROM name, and nothing to the rest")
{
	const std::string table = "1942 1\ndkong 3\nmspacman 3\npacman 3\nsome-flipped 2\n";
	CHECK(CaptureRotationText::turnsFromTable(table, "mspacman") == 3);
	CHECK(CaptureRotationText::turnsFromTable(table, "1942") == 1);
	CHECK(CaptureRotationText::turnsFromTable(table, "some-flipped") == 2);
	CHECK(CaptureRotationText::turnsFromTable(table, "dkong") == 3);
	// a prefix is not a match, nor is a name the table lacks
	CHECK(CaptureRotationText::turnsFromTable(table, "mspac") == 0);
	CHECK(CaptureRotationText::turnsFromTable(table, "pac") == 0);
	CHECK(CaptureRotationText::turnsFromTable(table, "sf2") == 0);
	CHECK(CaptureRotationText::turnsFromTable(table, "") == 0);
	CHECK(CaptureRotationText::turnsFromTable("", "mspacman") == 0);
	CHECK(CaptureRotationText::turnsFromTable("mspacman x\n", "mspacman") == 0);
	// the last line needs no newline
	CHECK(CaptureRotationText::turnsFromTable("galaga 3", "galaga") == 3);
}
