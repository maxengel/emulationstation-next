#pragma once
#ifndef ES_APP_JOURNEY_TIERS_H
#define ES_APP_JOURNEY_TIERS_H

// What a settings-first restore carries across its restart (audit #307
// PL-029). RESTORE with SETTINGS ticked restores the settings, restarts,
// and at the next start offers the rest: the restore form's dialog says
// ANYTHING ELSE YOU TICKED IS RESTORED AFTER THE RESTART. The ticks cannot
// ride in system.cfg -- the settings restore is what replaces it -- so the
// form writes them here, beside the content picker's selection in
// /storage/.cache/cloud_sync, which the settings backup deliberately does
// not capture; the start reads them back when backuptool's journey marker
// is there, and builds the continuation from them.
//
// Pure: the record's text and the continuation's command. The file and the
// dialogs are GuiMenu's and main's.

#include <string>
#include <vector>

namespace JourneyTiers
{
	// Where the ticks wait for the restart.
	constexpr const char* PATH = "/storage/.cache/cloud_sync/journey-tiers";

	struct Tiers
	{
		bool known = false;    // a record this build's form wrote, read whole
		bool saves = false;
		bool content = false;  // ROMs and BIOS
		bool media = false;    // game content
		bool any() const { return saves || content || media; }
	};

	// The record, a line that says what it is and then one line a tier
	// (es-code-traps.md: a record the interface writes and reads back is a
	// line that says what it is, never a bare digit).
	inline std::string record(bool saves, bool content, bool media)
	{
		return std::string("journey-tiers=1\n")
			+ "saves=" + (saves ? "1" : "0") + "\n"
			+ "content=" + (content ? "1" : "0") + "\n"
			+ "media=" + (media ? "1" : "0") + "\n";
	}

	// known only when the first line says journey-tiers=1: anything else --
	// no file, an empty one, a later format -- is not this build's record,
	// and the continuation falls back to the one the marker always meant.
	// Keys this build does not know are passed over.
	inline Tiers parse(const std::string& text)
	{
		Tiers t;
		std::vector<std::string> lines;
		std::string line;
		for (char c : text)
		{
			if (c == '\n') { lines.push_back(line); line.clear(); }
			else if (c != '\r') line += c;
		}
		if (!line.empty())
			lines.push_back(line);
		if (lines.empty() || lines[0] != "journey-tiers=1")
			return t;
		t.known = true;
		for (size_t i = 1; i < lines.size(); i++)
		{
			if (lines[i] == "saves=1")   t.saves = true;
			if (lines[i] == "content=1") t.content = true;
			if (lines[i] == "media=1")   t.media = true;
		}
		return t;
	}

	// The continuation a journey marker offers: every part as its own tier,
	// each reporting itself as it ends (">>> tier <label>|<rc>", the label
	// being what the restore form calls the item) and the run's status
	// accumulated rather than taken from the last part -- the restore
	// form's composition (GuiMenu, cloudOpenTransfer), with the parts the
	// player ticked and none they did not. Saves first, then the content
	// the picker's selection names (--selected; the picker wrote it with
	// --set-systems before the restore ran, into the same directory as the
	// record, so the settings restore did not touch it either).
	//
	// A marker with no record of this build's (unknown) is one an earlier
	// build left (D-WORKFLOW-050, read both): its continuation was
	// everything, and its prompt said so, so that is what it still runs.
	inline std::string command(const Tiers& t)
	{
		std::string cmd = "rc=0";
		auto add = [&cmd](const std::string& label, const std::string& part)
		{
			cmd += " ; _t=0 ; { " + part + " ; } || _t=$? ; echo \">>> tier " + label + "|$_t\" ; [ \"$_t\" = 0 ] || rc=$_t";
		};
		if (!t.known)
		{
			add("RESTORING ROMS AND BIOS", "/usr/bin/cloud_content_restore --all");
			add("RESTORING SAVES", "/usr/bin/cloud_restore --yes");
		}
		else
		{
			if (t.saves)
				add("SAVES", "/usr/bin/cloud_restore --yes --saves-only");
			if (t.content || t.media)
				add(t.content && t.media ? "ROMS, BIOS, AND GAME CONTENT" : t.content ? "ROMS AND BIOS" : "GAME CONTENT",
					std::string("/usr/bin/cloud_content_restore --selected")
					+ (t.content && t.media ? " --with-media" : t.media ? " --media-only" : ""));
		}
		cmd += " ; exit $rc";
		return cmd;
	}
}

#endif // ES_APP_JOURNEY_TIERS_H
