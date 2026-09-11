#pragma once
#ifndef ES_APP_CLOUD_TEXT_H
#define ES_APP_CLOUD_TEXT_H

// The cloud surfaces' pure text: what a stamp line says, what a script's
// protocol line means, which words a provider is known by, and how a
// composed line is shortened when the panel is too narrow for all of it.
//
// Nothing here reads a file, asks Settings or SystemConf anything, touches
// a Window, or measures a font, so all of it can be checked by a test
// binary that links no more of EmulationStation than this file and
// StringUtil (es-app/tests/unit). The callers keep the parts that cannot
// be: the file read, the translation of an outcome into the player's
// language, and the side effects a protocol line asks for.
//
// Translation stays outside on purpose. _() at this level would put the
// player's language inside the thing under test, so the enums below name
// an outcome and the caller says it in words.

#include <ctime>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace CloudText
{
	// The providers we put in front of people, most-likely-to-be-owned
	// first: rclone's word for each service, and the words the player
	// chose it by. One table, read from both sides -- the wizard's
	// recommendation list and providerLabel below.
	//
	// This was split into two groups by how the sign-in works -- one you
	// approve through a provider page, one you type a key into. That is our
	// distinction, not the player's: they are choosing where their saves
	// live, and the group headers made an implementation detail look like
	// the question being asked. It also read as a hierarchy it is not,
	// since the second group was a handful of picks rather than the rest of
	// the world.
	//
	// One list of recommendations, and a complete list behind it. Which
	// kind of sign-in a provider needs is settled after it is chosen, by
	// its own tier. The labels are translated at the point of use: _() at
	// static-init time would run before the locale is loaded.
	const std::vector<std::pair<std::string, std::string>>& recommendedProviders();

	// rclone's word for a service ("drive") in the words the player chose
	// it by ("GOOGLE DRIVE"). A provider set up outside the list above
	// falls back to rclone's own word, uppercased, which is at least the
	// name on the tin. Empty in, empty out -- the caller decides what to
	// say when nothing is connected.
	std::string providerLabel(const std::string& type);

	// rclone's name for a provider option ("bearer_token") in the words a
	// player connecting a NAS or a bucket would use ("ACCESS TOKEN"), for
	// the fields the recommended providers ask for; anything unmapped is
	// the name itself with its underscores as spaces, upper case ("SOME
	// OPTION", never "SOME_OPTION"). The rclone name still goes in the
	// config -- only the row's label changes (#123). Every label fits a
	// 640x480 row beside its value; the longest is SECRET ACCESS KEY.
	std::string fieldLabel(const std::string& rcloneName);

	// The line under a provider form's title: the provider as rclone
	// describes it, unless that description is a paragraph -- s3's names
	// sixty compatible services -- in which case the words the player chose
	// it by (providerLabel). A subtitle is one short line on a 3.5" panel,
	// never a paragraph in small text (#128, D-UI-023).
	std::string providerSubtitle(const std::string& type, const std::string& label);

	// The device name as the network takes it: ASCII letters and digits,
	// any run of anything else as one hyphen, none at either end, at most
	// 63. The same rule as the scripts' clean_hostname (001-functions),
	// which network-base-setup applies at boot. The player's own name is
	// left exactly as they typed it -- this is only what the network shows.
	std::string cleanHostname(const std::string& in);

	// How a run ended, in the four words every cloud surface uses
	// (D-UI-028). Gaps and Failed both read COULDN'T FINISH to the player
	// and differ only in where their why comes from: a composed run whose
	// parts disagreed has the failing part's, and nothing to fall back on
	// from an exit code that describes the whole run.
	enum class Outcome
	{
		Completed,
		Gaps,
		SkippedLockHeld,
		SkippedNoNetwork,
		SkippedGameStarted,
		Failed
	};

	// What a stamp line says. The file read and the fallback whys stay with
	// the caller; ran is false for anything this cannot make sense of, and
	// a row then reads NOT DONE ON THIS DEVICE YET rather than inventing a
	// date.
	struct LastRun
	{
		bool ran = false;
		time_t when = 0;
		int code = 0;
		std::string token;
		std::string why;      // upper case, no trailing period; empty when there was none
		Outcome outcome = Outcome::Completed;
		bool finished = false;
		bool knownToken = false;  // one of ThreadedCloudSync's tokens, not a scripts' why
	};

	// Whether a stamp's third field is one of the tokens EmulationStation
	// writes (rather than the scripts' own sentence, underscored).
	bool isOutcomeToken(const std::string& token);

	// "<epoch> <rc>[ <token>[ <why...>]]" (D-UI-028), the whole file's text.
	LastRun parseLastRun(const std::string& text);

	// Which run a stamp describes. EmulationStation stamps last-sync-exit
	// and last-sync-startup as each automatic run ends, within a second or
	// two of the script writing its own last-backup or last-restore, so a
	// matching time says which it was. Pass 0 for a stamp that does not
	// exist; the exit stamp wins a tie.
	enum class RunOrigin { None, AfterLastGame, AtStartup };
	RunOrigin runOrigin(time_t when, time_t exitWhen, time_t startupWhen);

	// A shorter form of a why sentence, for a panel the whole one does not
	// fit on (#115). Two shapes appear in the sentences the scripts emit,
	// and both put the part that can go at the end: a trailing clause after
	// a dash (COULDN'T REACH YOUR CLOUD - CHECK YOUR SIGN-IN) and a
	// trailing sentence after a full stop. Anything else has no short form
	// and comes back empty; the caller falls back to the outcome word on
	// its own, which fits any panel and is still true.
	std::string shortenWhy(const std::string& why);

	// The forms of an outcome line, longest first (D-UI-035): the whole
	// thing, then the why with its trailing clause dropped, then the
	// outcome word alone. A line with no " - " has no split to make and
	// gets the single candidate it has today.
	std::vector<std::string> outcomeCandidates(const std::string& outcome);

	// The ">>> " lines are the scripts talking to the interface, not to the
	// player. Classification only: what the line is and what it carries.
	// Acting on it -- the pid to signal, the card's waiting text, the why
	// to keep, the offer to put to the player, the tier to record -- stays
	// with the caller.
	enum class ProtocolKind { NotProtocol, Pid, Doing, Why, Offer, Tier, Unknown };

	struct ProtocolLine
	{
		ProtocolKind kind = ProtocolKind::NotProtocol;
		// doing: what is being waited on; why: the sentence, upper case and
		// without its full stop; offer: the question's name; tier: the
		// part's label, upper case.
		std::string text;
		// offer: what follows the question's name, '|'-separated -- for
		// create-saves-folder the folder that is missing, then a folder
		// beside it whose name is close to it, when there is one (#127).
		std::vector<std::string> args;
		// pid: the process group; tier: that part's exit code, -1 when the
		// line carried none.
		int number = 0;
	};

	ProtocolLine classifyProtocolLine(const std::string& clean);

	// Which way the saves moved, read from the command: the in-place clause
	// is one per verb (D-CLOUD-077).
	enum class Verb { Sync, Backup, Restore, Other };
	Verb verbOf(const std::string& cmd);

	// The first candidate that fits the width, else the last one offered.
	// measure is the row's own font, handed in because a font is a GL
	// resource and this has to stay free of one; an empty measure or a
	// width of zero means nothing is known yet, and the full form is the
	// right answer then.
	std::string chooseThatFits(const std::vector<std::string>& candidates, float width,
		const std::function<float(const std::string&)>& measure);
}

#endif // ES_APP_CLOUD_TEXT_H
