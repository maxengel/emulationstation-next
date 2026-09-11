// The pure cloud text, checked without a device.
//
// Everything here runs against es-app/src/CloudText.cpp alone -- no window,
// no locale, no /storage. What it is worth is what it covers: the shapes a
// script can print and a stamp can hold, including the ones nobody meant to
// write. A parser is judged on its junk, so most of these cases are junk.
//
// #120 box 4. Run: see README.md beside this file.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include "CloudText.h"

#include <string>
#include <vector>

using namespace CloudText;

// ---------------------------------------------------------------- hostname

TEST_CASE("cleanHostname keeps letters and digits and nothing else")
{
	// The case the row exists for (#106): a name with a space in it, said
	// back the way a router will show it.
	CHECK(cleanHostname("RG SP") == "RG-SP");
	CHECK(cleanHostname("Max's RG35XX SP") == "Max-s-RG35XX-SP");

	// A run of anything is one hyphen, however long the run.
	CHECK(cleanHostname("RG    SP") == "RG-SP");
	CHECK(cleanHostname("RG _-. SP") == "RG-SP");

	// Never at either end.
	CHECK(cleanHostname("-RG-SP-") == "RG-SP");
	CHECK(cleanHostname("   RG SP   ") == "RG-SP");
	CHECK(cleanHostname("---") == "");
	CHECK(cleanHostname("") == "");

	// Unicode is not "anything else" that survives: the bytes are dropped
	// like any other run, and a name of nothing but them cleans to nothing.
	CHECK(cleanHostname("Max\xe2\x80\x99s RG SP") == "Max-s-RG-SP");
	CHECK(cleanHostname("\xf0\x9f\x8e\xae RG SP") == "RG-SP");
	CHECK(cleanHostname("\xd0\x9f\xd1\x80\xd0\xb8\xd0\xb2\xd0\xb5\xd1\x82") == "");

	// 63 bytes is the limit the network takes.
	CHECK(cleanHostname(std::string(70, 'a')) == std::string(63, 'a'));
	CHECK(cleanHostname(std::string(63, 'a')) == std::string(63, 'a'));

	// And the truncation cannot leave a trailing hyphen behind.
	const std::string cut = cleanHostname(std::string(62, 'a') + " " + std::string(10, 'b'));
	CHECK(cut == std::string(62, 'a'));
}

// ---------------------------------------------------------------- provider

TEST_CASE("providerLabel says the words the player chose the service by")
{
	CHECK(providerLabel("dropbox") == "DROPBOX");
	CHECK(providerLabel("drive") == "GOOGLE DRIVE");
	CHECK(providerLabel("webdav") == "WEBDAV");
	CHECK(providerLabel("onedrive") == "MICROSOFT ONEDRIVE");

	// A provider set up outside the list falls back to rclone's own word.
	CHECK(providerLabel("nextcloud") == "NEXTCLOUD");
	CHECK(providerLabel("yandex") == "YANDEX");

	// Empty in, empty out: the caller decides what to say when nothing is
	// connected.
	CHECK(providerLabel("") == "");

	// The table is the one both sides read.
	CHECK(recommendedProviders().size() == 14);
	CHECK(recommendedProviders().front().first == "dropbox");
}

// ------------------------------------------------------------------- stamp

TEST_CASE("parseLastRun reads a stamp EmulationStation wrote")
{
	const LastRun r = parseLastRun("1789074505 0 completed");
	CHECK(r.ran);
	CHECK(r.when == 1789074505);
	CHECK(r.code == 0);
	CHECK(r.token == "completed");
	CHECK(r.knownToken);
	CHECK(r.outcome == Outcome::Completed);
	CHECK(r.finished);
	CHECK(r.why == "");
}

TEST_CASE("parseLastRun reads a failure with the scripts' own sentence")
{
	const LastRun r = parseLastRun("1789075371 5 cloud-stopped YOUR CLOUD STOPPED ANSWERING");
	CHECK(r.ran);
	CHECK(r.when == 1789075371);
	CHECK(r.code == 5);
	CHECK(r.token == "cloud-stopped");
	CHECK(r.knownToken);
	CHECK(r.outcome == Outcome::Failed);
	CHECK_FALSE(r.finished);
	CHECK(r.why == "YOUR CLOUD STOPPED ANSWERING");
}

TEST_CASE("parseLastRun reads a stamp a script wrote")
{
	// The scripts' shape: the sentence itself as the third field, spaces as
	// underscores, and no token of ours in front of it.
	const LastRun r = parseLastRun("1789000000 5 YOUR_CLOUD_STOPPED_ANSWERING");
	CHECK(r.ran);
	CHECK(r.code == 5);
	CHECK(r.token == "YOUR_CLOUD_STOPPED_ANSWERING");
	CHECK_FALSE(r.knownToken);
	CHECK(r.outcome == Outcome::Failed);
	CHECK(r.why == "YOUR CLOUD STOPPED ANSWERING");

	// Lower case and a full stop, as a script may well print it.
	const LastRun quiet = parseLastRun("1789000000 5 your_cloud_stopped_answering.");
	CHECK(quiet.why == "YOUR CLOUD STOPPED ANSWERING");
}

TEST_CASE("parseLastRun on the shapes that are not a run")
{
	CHECK_FALSE(parseLastRun("").ran);
	CHECK_FALSE(parseLastRun("   \n").ran);
	CHECK_FALSE(parseLastRun("1789000000").ran);        // one field
	CHECK_FALSE(parseLastRun("not-a-stamp at all").ran);  // no epoch
	CHECK_FALSE(parseLastRun("0 0 completed").ran);     // epoch of zero
	CHECK_FALSE(parseLastRun("-5 0 completed").ran);    // epoch in the past-past
	CHECK_FALSE(parseLastRun("\x01\x02 junk").ran);
}

TEST_CASE("parseLastRun tolerates how the line is written")
{
	// The stamp is written with a trailing newline, and read after a trim.
	CHECK(parseLastRun("1789074505 0 completed\n").when == 1789074505);
	// Two spaces where there should be one: empty fields are dropped.
	CHECK(parseLastRun("1789074505  0  completed").token == "completed");
	// Two fields is the shape that predates the token, and is read from its
	// code alone.
	const LastRun old = parseLastRun("1789000000 0");
	CHECK(old.ran);
	CHECK(old.outcome == Outcome::Completed);
	CHECK(old.token == "");
	CHECK_FALSE(old.knownToken);
}

TEST_CASE("parseLastRun names each of the four outcomes")
{
	// rclone's 9 -- nothing needed moving -- is a completed run.
	CHECK(parseLastRun("1789000000 9").outcome == Outcome::Completed);
	// A token that says completed outranks a non-zero code.
	CHECK(parseLastRun("1789000000 1 completed").outcome == Outcome::Completed);

	// The two sentinels, by code (CloudExit.h).
	CHECK(parseLastRun("1789000000 75").outcome == Outcome::SkippedLockHeld);
	CHECK(parseLastRun("1789000000 69").outcome == Outcome::SkippedNoNetwork);

	// The launch cancel, by token: a 130 that was not one reads as a
	// failure instead.
	CHECK(parseLastRun("1789000000 130 cancelled").outcome == Outcome::SkippedGameStarted);
	CHECK(parseLastRun("1789000000 130 stopped").outcome == Outcome::Failed);

	// A composed run whose parts disagreed keeps its own token, and has
	// nothing to fall back on when it carried no sentence.
	const LastRun gaps = parseLastRun("1789000000 1 gaps");
	CHECK(gaps.outcome == Outcome::Gaps);
	CHECK(gaps.why == "");
	CHECK(parseLastRun("1789000000 1 gaps NES DID NOT FINISH").why == "NES DID NOT FINISH");

	// Anything else is a failure with whatever why it carried.
	const LastRun failed = parseLastRun("1789000000 7 cloud-refused");
	CHECK(failed.outcome == Outcome::Failed);
	CHECK(failed.why == "");   // the caller fills this from the token
}

// ------------------------------------------------------------------ origin

TEST_CASE("runOrigin says which automatic run a stamp belongs to")
{
	const time_t when = 1789074505;

	// Within ten seconds either way is the same run.
	CHECK(runOrigin(when, when, 0) == RunOrigin::AfterLastGame);
	CHECK(runOrigin(when, when + 10, 0) == RunOrigin::AfterLastGame);
	CHECK(runOrigin(when, when - 10, 0) == RunOrigin::AfterLastGame);

	// Eleven is a different one.
	CHECK(runOrigin(when, when + 11, 0) == RunOrigin::None);
	CHECK(runOrigin(when, when - 11, 0) == RunOrigin::None);

	// The same window on the startup stamp.
	CHECK(runOrigin(when, 0, when + 10) == RunOrigin::AtStartup);
	CHECK(runOrigin(when, 0, when + 11) == RunOrigin::None);
	CHECK(runOrigin(when, 0, when - 10) == RunOrigin::AtStartup);

	// The exit stamp is preferred where both are in range, and the startup
	// stamp answers where the exit one is out of range.
	CHECK(runOrigin(when, when, when) == RunOrigin::AfterLastGame);
	CHECK(runOrigin(when, when + 11, when) == RunOrigin::AtStartup);

	// A stamp that does not exist is a zero, and answers nothing.
	CHECK(runOrigin(when, 0, 0) == RunOrigin::None);
	CHECK(runOrigin(0, when, when) == RunOrigin::None);
	CHECK(runOrigin(0, 0, 0) == RunOrigin::None);
}

// ------------------------------------------------------------ outcome line

TEST_CASE("shortenWhy drops the part that can go")
{
	// D-UI-035's worked example: a trailing clause after a dash.
	CHECK(shortenWhy("COULDN'T REACH YOUR CLOUD - CHECK YOUR SIGN-IN") == "COULDN'T REACH YOUR CLOUD");

	// A parenthetical.
	CHECK(shortenWhy("YOUR CLOUD WOULDN'T TAKE THE FILES (403)") == "YOUR CLOUD WOULDN'T TAKE THE FILES");

	// A second sentence.
	CHECK(shortenWhy("IT WAS STOPPED. TRY AGAIN IN A MOMENT") == "IT WAS STOPPED");

	// A sentence with none of the three has no short form, and the caller
	// falls back to the outcome word on its own.
	CHECK(shortenWhy("YOUR CLOUD STOPPED ANSWERING") == "");
	CHECK(shortenWhy("") == "");

	// A hyphen inside a word is not a trailing clause.
	CHECK(shortenWhy("YOUR SIGN-IN MAY HAVE EXPIRED") == "");

	// The dash wins over the others, and the parenthesis over the stop.
	CHECK(shortenWhy("A - B (C). D") == "A");
	CHECK(shortenWhy("A (B). C") == "A");
}

TEST_CASE("outcomeCandidates offers the forms of a line, longest first")
{
	// The worked example (D-UI-035): three forms, each one shorter.
	const std::vector<std::string> three =
		outcomeCandidates("COULDN'T FINISH - COULDN'T REACH YOUR CLOUD - CHECK YOUR SIGN-IN");
	REQUIRE(three.size() == 3);
	CHECK(three[0] == "COULDN'T FINISH - COULDN'T REACH YOUR CLOUD - CHECK YOUR SIGN-IN");
	CHECK(three[1] == "COULDN'T FINISH - COULDN'T REACH YOUR CLOUD");
	CHECK(three[2] == "COULDN'T FINISH");

	// A why with no short form gives two: the whole line, and the outcome
	// word, which fits any panel this runs on.
	const std::vector<std::string> two = outcomeCandidates("COULDN'T FINISH - YOUR CLOUD STOPPED ANSWERING");
	REQUIRE(two.size() == 2);
	CHECK(two[0] == "COULDN'T FINISH - YOUR CLOUD STOPPED ANSWERING");
	CHECK(two[1] == "COULDN'T FINISH");

	// A line with no " - " has no split to make.
	const std::vector<std::string> one = outcomeCandidates("COMPLETED");
	REQUIRE(one.size() == 1);
	CHECK(one[0] == "COMPLETED");

	CHECK(outcomeCandidates("").size() == 1);

	// A sentinel reads the same way.
	const std::vector<std::string> skipped = outcomeCandidates("SKIPPED - YOU'RE NOT ONLINE");
	REQUIRE(skipped.size() == 2);
	CHECK(skipped[1] == "SKIPPED");
}

// ---------------------------------------------------------------- protocol

TEST_CASE("classifyProtocolLine reads a pid line")
{
	const ProtocolLine pid = classifyProtocolLine(">>> pid 1234");
	CHECK(pid.kind == ProtocolKind::Pid);
	CHECK(pid.number == 1234);

	// A pid line with nothing after it is still one, and says zero -- which
	// the caller reads as "not known yet" and signals nothing.
	CHECK(classifyProtocolLine(">>> pid ").kind == ProtocolKind::Pid);
	CHECK(classifyProtocolLine(">>> pid ").number == 0);
	CHECK(classifyProtocolLine(">>> pid abc").number == 0);

	// Without the space it is not the pid line at all.
	CHECK(classifyProtocolLine(">>> pid").kind == ProtocolKind::Unknown);
}

TEST_CASE("classifyProtocolLine reads a doing line")
{
	const ProtocolLine doing = classifyProtocolLine(">>> doing network");
	CHECK(doing.kind == ProtocolKind::Doing);
	CHECK(doing.text == "network");

	// Any other keyword is a doing line too; only "network" makes the card
	// say it is waiting, and that is the caller's decision.
	CHECK(classifyProtocolLine(">>> doing archive").text == "archive");
	CHECK(classifyProtocolLine(">>> doing ").text == "");
	CHECK(classifyProtocolLine(">>> doing   network  ").text == "network");
}

TEST_CASE("classifyProtocolLine reads a why line")
{
	const ProtocolLine why = classifyProtocolLine(">>> why your cloud stopped answering");
	CHECK(why.kind == ProtocolKind::Why);
	CHECK(why.text == "YOUR CLOUD STOPPED ANSWERING");

	// The outcome line supplies its own end, so a trailing full stop goes --
	// however many of them there are.
	CHECK(classifyProtocolLine(">>> why your cloud stopped answering.").text == "YOUR CLOUD STOPPED ANSWERING");
	CHECK(classifyProtocolLine(">>> why it was stopped...").text == "IT WAS STOPPED");
	CHECK(classifyProtocolLine(">>> why   it was stopped.  ").text == "IT WAS STOPPED");

	// A stop inside the sentence stays.
	CHECK(classifyProtocolLine(">>> why it stopped. try again").text == "IT STOPPED. TRY AGAIN");

	// An empty why is a why line with nothing in it; the caller keeps the
	// one it had.
	CHECK(classifyProtocolLine(">>> why ").kind == ProtocolKind::Why);
	CHECK(classifyProtocolLine(">>> why ").text == "");
	CHECK(classifyProtocolLine(">>> why .").text == "");
}

TEST_CASE("classifyProtocolLine reads an offer line")
{
	const ProtocolLine offer = classifyProtocolLine(">>> offer create-saves-folder");
	CHECK(offer.kind == ProtocolKind::Offer);
	CHECK(offer.text == "create-saves-folder");
	CHECK(offer.args.empty());
	CHECK(classifyProtocolLine(">>> offer ").text == "");

	// #127: the folder that is missing, then a near name beside it.
	const ProtocolLine near = classifyProtocolLine(">>> offer create-saves-folder|/ROCKNIX/Savez|/ROCKNIX/Saves");
	CHECK(near.kind == ProtocolKind::Offer);
	CHECK(near.text == "create-saves-folder");
	REQUIRE(near.args.size() == 2);
	CHECK(near.args[0] == "/ROCKNIX/Savez");
	CHECK(near.args[1] == "/ROCKNIX/Saves");
	const ProtocolLine alone = classifyProtocolLine(">>> offer create-saves-folder|/Saves");
	REQUIRE(alone.args.size() == 1);
	CHECK(alone.args[0] == "/Saves");
}

TEST_CASE("fieldLabel says the player's words for rclone's option names")
{
	// The WebDAV form, framed at 640x480 with rclone's names on it (#123).
	CHECK(fieldLabel("url") == "SERVER ADDRESS");
	CHECK(fieldLabel("vendor") == "SERVER TYPE");
	CHECK(fieldLabel("user") == "USERNAME");
	CHECK(fieldLabel("pass") == "PASSWORD");
	CHECK(fieldLabel("bearer_token") == "ACCESS TOKEN");
	// S3 and SFTP.
	CHECK(fieldLabel("access_key_id") == "ACCESS KEY ID");
	CHECK(fieldLabel("secret_access_key") == "SECRET ACCESS KEY");
	CHECK(fieldLabel("host") == "SERVER ADDRESS");
	CHECK(fieldLabel("key_file_pass") == "PRIVATE KEY PASSWORD");
	// Unmapped: the name in plain words, never SOME_OPTION.
	CHECK(fieldLabel("some_option") == "SOME OPTION");
	CHECK(fieldLabel("SOME_OPTION") == "SOME OPTION");
	CHECK(fieldLabel(" url ") == "SERVER ADDRESS");
	CHECK(fieldLabel("") == "");
	// Nothing wider than the row can take beside a value on a 640 panel.
	for (const char* f : { "url", "vendor", "user", "pass", "bearer_token", "env_auth", "access_key_id",
	                       "secret_access_key", "region", "endpoint", "location_constraint", "acl",
	                       "bucket_object_lock_enabled", "host", "port", "key_pem", "key_file",
	                       "key_file_pass", "pubkey", "pubkey_file", "key_use_agent",
	                       "use_insecure_cipher", "disable_hashcheck", "ssh" })
		CHECK(fieldLabel(f).size() <= 21);
}

TEST_CASE("classifyProtocolLine reads a tier line")
{
	const ProtocolLine tier = classifyProtocolLine(">>> tier saves|0");
	CHECK(tier.kind == ProtocolKind::Tier);
	CHECK(tier.text == "SAVES");
	CHECK(tier.number == 0);

	CHECK(classifyProtocolLine(">>> tier SETTINGS|5").text == "SETTINGS");
	CHECK(classifyProtocolLine(">>> tier SETTINGS|5").number == 5);
	CHECK(classifyProtocolLine(">>> tier  roms and bios | 7 ").text == "ROMS AND BIOS");
	CHECK(classifyProtocolLine(">>> tier  roms and bios | 7 ").number == 7);

	// No code: -1, which is not a success and not one of rclone's.
	CHECK(classifyProtocolLine(">>> tier saves").number == -1);
	CHECK(classifyProtocolLine(">>> tier saves|").number == 0);
	CHECK(classifyProtocolLine(">>> tier saves|nonsense").number == 0);

	// No label: the caller records nothing.
	CHECK(classifyProtocolLine(">>> tier |5").text == "");
	CHECK(classifyProtocolLine(">>> tier ").text == "");
	CHECK(classifyProtocolLine(">>> tier ").number == -1);
}

TEST_CASE("classifyProtocolLine on everything else")
{
	// A protocol line this reader does not know -- the transfer page's own
	// markers, or one added after this build -- still says the wait is over.
	CHECK(classifyProtocolLine(">>> unit nes|2|5").kind == ProtocolKind::Unknown);
	CHECK(classifyProtocolLine(">>> removed 3|1024|nes").kind == ProtocolKind::Unknown);
	CHECK(classifyProtocolLine(">>> ").kind == ProtocolKind::Unknown);

	// And a line the scripts printed for the player is not a protocol line.
	CHECK(classifyProtocolLine("Transferred: 12.3 MiB / 45.6 MiB, 27%").kind == ProtocolKind::NotProtocol);
	CHECK(classifyProtocolLine("").kind == ProtocolKind::NotProtocol);
	CHECK(classifyProtocolLine(">>>").kind == ProtocolKind::NotProtocol);
	CHECK(classifyProtocolLine(">> why something").kind == ProtocolKind::NotProtocol);
	CHECK(classifyProtocolLine("  >>> why something").kind == ProtocolKind::NotProtocol);
	CHECK(classifyProtocolLine("echo \">>> pid 1\"").kind == ProtocolKind::NotProtocol);
}

// -------------------------------------------------------------------- verb

TEST_CASE("verbOf reads the direction out of the command")
{
	CHECK(verbOf("/usr/bin/cloud_backup --saves") == Verb::Backup);
	CHECK(verbOf("/usr/bin/backuptool --backup") == Verb::Backup);
	CHECK(verbOf("/usr/bin/cloud_restore --saves") == Verb::Restore);
	CHECK(verbOf("/usr/bin/cloud_restore --saves; /usr/bin/cloud_backup --saves") == Verb::Sync);
	CHECK(verbOf("/usr/bin/cloud_migrate_layout") == Verb::Other);
	CHECK(verbOf("") == Verb::Other);
}

// ------------------------------------------------------------------ fitting

TEST_CASE("chooseThatFits takes the first candidate that fits")
{
	// One character, one unit of width, so a case reads as its lengths.
	const auto measure = [](const std::string& s) { return (float) s.size(); };

	const std::vector<std::string> candidates = { "0123456789", "01234", "01" };

	CHECK(chooseThatFits(candidates, 10.0f, measure) == "0123456789");
	CHECK(chooseThatFits(candidates, 9.0f, measure) == "01234");
	CHECK(chooseThatFits(candidates, 4.0f, measure) == "01");

	// Nothing fits: the last one offered, which is the shortest thing the
	// caller had. Better a clipped word than a blank row.
	CHECK(chooseThatFits(candidates, 1.0f, measure) == "01");

	// Nothing is known yet -- an unsized row, or a row with no font -- and
	// the full form is the right answer then.
	CHECK(chooseThatFits(candidates, 0.0f, measure) == "0123456789");
	CHECK(chooseThatFits(candidates, -1.0f, measure) == "0123456789");
	CHECK(chooseThatFits(candidates, 4.0f, nullptr) == "0123456789");

	// Nothing offered, nothing shown.
	CHECK(chooseThatFits({}, 100.0f, measure) == "");
}
