#pragma once
#ifndef ES_APP_CAPTURE_ROTATION_H
#define ES_APP_CAPTURE_ROTATION_H

#include <string>

class FileData;

// A game's display rotation, learned at the end of each RetroArch session
// from the launch log and kept beside its save states as <rom>.rotation
// (fork #245, D-UI-081) -- so it travels with the states, and a game that
// was never played here, which has no captures either, simply has none.
namespace CaptureRotation
{
	// Quarter turns counter-clockwise, 0-3; 0 when there is no record.
	int read(FileData* game);

	// After a session: read the launch log and RetroArch's config, and
	// write the record when it is new or has changed. Only RetroArch
	// sessions write one; nothing else captures through the interface.
	void recordAfterSession(FileData* game, const std::string& emulator);

	// Where the record lives, "" when the game's system keeps no states.
	std::string recordPath(FileData* game);
}

#endif // ES_APP_CAPTURE_ROTATION_H
