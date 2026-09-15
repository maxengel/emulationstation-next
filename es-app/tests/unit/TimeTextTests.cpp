// The time half of a save state's date, checked without a device (fork
// #195, D-UI-058).
//
// Runs against es-core/src/utils/TimeText.cpp alone. The case that wrote
// it: the AUTO SAVE row on the RG SP read "09/13/2026 09:07" for a Sunday
// morning file, and on a Monday night that was read as 9:07 pm. What
// matters here is that the 12-hour clock always says which half of the day
// it is, in a locale with a marker and in one without.

#include "doctest/doctest.h"

#include "utils/TimeText.h"

using Utils::Time::clockText;

static tm at(int hour, int minute)
{
	tm t = {};
	t.tm_hour = hour;
	t.tm_min = minute;
	return t;
}

TEST_CASE("the switch off is the 24-hour clock, whatever the locale says for %p")
{
	CHECK(clockText(at(9, 7), false, "AM") == "09:07");
	CHECK(clockText(at(21, 7), false, "PM") == "21:07");
	CHECK(clockText(at(9, 7), false, "") == "09:07");
	CHECK(clockText(at(0, 5), false, "") == "00:05");
	CHECK(clockText(at(23, 59), false, "") == "23:59");
}

TEST_CASE("the switch on is %I:%M %p, the locale's marker after the hour")
{
	CHECK(clockText(at(9, 7), true, "AM") == "09:07 AM");
	CHECK(clockText(at(21, 7), true, "PM") == "09:07 PM");
	// Midnight and noon are 12, as %I writes them.
	CHECK(clockText(at(0, 5), true, "AM") == "12:05 AM");
	CHECK(clockText(at(12, 0), true, "PM") == "12:00 PM");
	CHECK(clockText(at(23, 59), true, "PM") == "11:59 PM");
}

TEST_CASE("a locale with no %p still tells morning from evening on the 12-hour clock")
{
	// French: strftime's %p is empty, so the hour alone would read "09:07"
	// twice a day -- the ambiguity the switch exists to remove.
	CHECK(clockText(at(9, 7), true, "") == "09:07 AM");
	CHECK(clockText(at(21, 7), true, "") == "09:07 PM");
	CHECK(clockText(at(0, 0), true, "") == "12:00 AM");
	CHECK(clockText(at(11, 59), true, "") == "11:59 AM");
	CHECK(clockText(at(12, 0), true, "") == "12:00 PM");
	CHECK(clockText(at(13, 0), true, "") == "01:00 PM");
}

TEST_CASE("the locale's own marker is used as it is")
{
	// A locale whose marker is not the English pair is not corrected.
	CHECK(clockText(at(9, 7), true, "a.m.") == "09:07 a.m.");
	CHECK(clockText(at(15, 30), true, "nachm.") == "03:30 nachm.");
}

TEST_CASE("the Sunday-morning file and the Monday-night reading are never the same text")
{
	// The case that filed #195: 09:07 on one half of the day read as the
	// other. With the switch on, no hour shares its text with the hour
	// twelve away from it, marker or no marker.
	for (int h = 0; h < 12; h++)
	{
		CHECK(clockText(at(h, 7), true, "") != clockText(at(h + 12, 7), true, ""));
		CHECK(clockText(at(h, 7), true, "AM") != clockText(at(h + 12, 7), true, "PM"));
	}
}

TEST_CASE("every hour writes the same number of characters, so one sample measures the widest tile")
{
	// GuiSaveState sizes the tile label from now() and trusts that text to
	// be as long as any row's. True only if every hour writes the same
	// count, marker included -- on both clocks.
	const size_t twelve = clockText(at(12, 0), true, "").size();
	const size_t twentyFour = clockText(at(12, 0), false, "").size();
	for (int h = 0; h < 24; h++)
	{
		CHECK(clockText(at(h, 0), true, "").size() == twelve);
		CHECK(clockText(at(h, 0), true, h < 12 ? "AM" : "PM").size() == twelve);
		CHECK(clockText(at(h, 0), false, "").size() == twentyFour);
	}
}
