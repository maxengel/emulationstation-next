#pragma once
#ifndef ES_APP_DISPLAY_ASPECT_H
#define ES_APP_DISPLAY_ASPECT_H

#include <string>

class FileData;
class GuiComponent;
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

	// Give every ImageComponent under root that shows imagePath the display
	// aspect, and every other one its file's; a component bound to another
	// picture is untouched. Theme extras bound with {game:image} live in the
	// list view, the md_image in the details container -- both call this.
	void applyToBoundImages(GuiComponent* root, const std::string& imagePath, float aspect);
}

#endif // ES_APP_DISPLAY_ASPECT_H
