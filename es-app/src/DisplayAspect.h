#pragma once
#ifndef ES_APP_DISPLAY_ASPECT_H
#define ES_APP_DISPLAY_ASPECT_H

class FileData;
class SystemData;

// The width-to-height a picture RetroArch wrote at the core's native size
// is shown at (fork #243, D-UI-080): a save-state thumbnail is its game's
// system's, a screenshot under the SCREENSHOTS entry the system of the
// game it was taken in, found by the content name RetroArch put in the
// file name. 0 means the file's own proportions.
namespace DisplayAspect
{
	float forSystem(SystemData* system);
	float forScreenshot(FileData* screenshot);
}

#endif // ES_APP_DISPLAY_ASPECT_H
