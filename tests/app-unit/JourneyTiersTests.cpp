// The settings-first restore's continuation (audit #307 PL-029): what the
// player ticked is what runs after the restart, and nothing else.
#include "doctest/doctest.h"
#include "JourneyTiers.h"

#include <string>

namespace
{
	bool has(const std::string& s, const std::string& part) { return s.find(part) != std::string::npos; }
}

TEST_CASE("journey: settings and saves ticked restore saves and no ROMs")
{
	const JourneyTiers::Tiers t = JourneyTiers::parse(JourneyTiers::record(true, false, false));
	const std::string cmd = JourneyTiers::command(t);
	INFO(cmd);
	CHECK_FALSE(has(cmd, "cloud_content_restore"));
	CHECK(has(cmd, "/usr/bin/cloud_restore --yes --saves-only"));
	CHECK(t.known);
	CHECK(has(cmd, ">>> tier SAVES|"));
	CHECK(has(cmd, "exit $rc"));
}

TEST_CASE("journey: ROMs and BIOS ticked restore the picked systems, never --all")
{
	const JourneyTiers::Tiers t = JourneyTiers::parse(JourneyTiers::record(false, true, false));
	REQUIRE(t.known);
	const std::string cmd = JourneyTiers::command(t);
	INFO(cmd);
	CHECK(has(cmd, "/usr/bin/cloud_content_restore --selected"));
	CHECK_FALSE(has(cmd, "--all"));
	CHECK_FALSE(has(cmd, "--with-media"));
	CHECK_FALSE(has(cmd, "--media-only"));
	CHECK_FALSE(has(cmd, "cloud_restore --yes"));
	CHECK(has(cmd, ">>> tier ROMS AND BIOS|"));
}

TEST_CASE("journey: game content alone and with ROMs take the content scripts' modes")
{
	const std::string media = JourneyTiers::command(JourneyTiers::parse(JourneyTiers::record(false, false, true)));
	CHECK(has(media, "cloud_content_restore --selected --media-only"));
	CHECK(has(media, ">>> tier GAME CONTENT|"));
	const std::string both = JourneyTiers::command(JourneyTiers::parse(JourneyTiers::record(true, true, true)));
	CHECK(has(both, "cloud_content_restore --selected --with-media"));
	CHECK(has(both, ">>> tier ROMS, BIOS, AND GAME CONTENT|"));
	// Saves before content, as the restore form orders them.
	CHECK(both.find("cloud_restore --yes --saves-only") < both.find("cloud_content_restore"));
}

TEST_CASE("journey: a marker an earlier build left, with no record, keeps its old continuation")
{
	// Already written (D-WORKFLOW-050): a journey marker from before this
	// record existed promised everything, and its prompt said so.
	const JourneyTiers::Tiers t = JourneyTiers::parse("");
	CHECK_FALSE(t.known);
	const std::string cmd = JourneyTiers::command(t);
	CHECK(has(cmd, "cloud_content_restore --all"));
	CHECK(has(cmd, "/usr/bin/cloud_restore --yes"));
}

TEST_CASE("journey: a record that does not say what it is is not trusted")
{
	CHECK_FALSE(JourneyTiers::parse("saves=1\n").known);
	CHECK_FALSE(JourneyTiers::parse("1\n").known);
	CHECK_FALSE(JourneyTiers::parse("journey-tiers=2\nsaves=1\n").known);
	const JourneyTiers::Tiers t = JourneyTiers::parse("journey-tiers=1\nsaves=1\ncontent=0\nmedia=0\nfuture=1\n");
	CHECK(t.known);
	CHECK(t.saves);
	CHECK_FALSE(t.content);
	CHECK_FALSE(t.media);
	CHECK(t.any());
}

TEST_CASE("journey: settings alone leaves nothing to continue")
{
	const JourneyTiers::Tiers t = JourneyTiers::parse(JourneyTiers::record(false, false, false));
	CHECK(t.known);
	CHECK_FALSE(t.any());
}
