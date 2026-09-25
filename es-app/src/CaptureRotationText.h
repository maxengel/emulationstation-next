#pragma once
#ifndef ES_APP_CAPTURE_ROTATION_TEXT_H
#define ES_APP_CAPTURE_ROTATION_TEXT_H

#include <string>

// How many quarter turns counter-clockwise RetroArch turned a game's frame
// for the display, read from what it logged and configured (fork #245,
// D-UI-081). The capture -- a save-state thumbnail, a screenshot -- is the
// frame as the core drew it, so the interface turns it by the same amount.
namespace CaptureRotationText
{
	// The last "[Environ] SET_ROTATION: "n" (deg)." line of a launch log
	// AFTER its last "=== Build" banner (RetroArch prints one per process),
	// as 0-3; -1 when that launch has none (a core that never asked), and
	// -1 when the log has no banner at all. Only the last launch's section
	// counts: the file held every launch since it was last removed on a
	// device whose log level was none (fork #280), and a vertical game's
	// line at its end turned every game exited after it.
	int turnsFromLog(const std::string& launchLog);

	// What the display did with the core's request: nothing when
	// video_allow_rotate is off, plus the player's own video_rotation,
	// mod 4. A core that asked for nothing counts as 0.
	int fold(int coreTurns, const std::string& retroarchConfig);

	// A core's table -- "<romname> <turns>" per line, generated from the
	// core's own driver flags at build time (fork #248) -- read for one
	// game: the turns, or 0 when the game is not in it.
	int turnsFromTable(const std::string& table, const std::string& romName);

	// The record's text, "turns=N" and a newline, and reading it back:
	// N is 0-3 and anything else reads as 0. A bare digit reads too. The
	// line is longer than three bytes on purpose -- readAllText skips a
	// UTF-8 byte-order mark by reading three bytes first, and a shorter
	// file comes back empty.
	std::string recordText(int turns);
	int parseRecord(const std::string& text);
}

#endif // ES_APP_CAPTURE_ROTATION_TEXT_H
