#include "DisplayAspect.h"

#include "CaptureRotation.h"
#include "DisplayAspectText.h"
#include "FileData.h"
#include "SystemData.h"
#include "PlatformId.h"
#include "components/ImageComponent.h"
#include "utils/FileSystemUtil.h"
#include "utils/StringUtil.h"

#include <map>
#include <mutex>

namespace DisplayAspect
{
	float forSystem(SystemData* system)
	{
		if (system == nullptr)
			return 0.0f;
		return DisplayAspectText::forSystem(system->getThemeFolder());
	}

	Transform forGame(FileData* game)
	{
		Transform t;
		if (game == nullptr)
			return t;
		FileData* source = game->getSourceFileData();
		if (source == nullptr)
			return t;
		t.aspect = forSystem(source->getSystem());
		t.turns = CaptureRotation::read(source);
		return t;
	}

	// The content name RetroArch wrote into the file name is the ROM's
	// file name without its extension. The game is looked for once per
	// screenshot across the game systems -- a list of a few hundred games
	// is a few hundred string compares, and the answer is kept, so moving
	// the cursor back over a screenshot costs nothing. A screenshot whose
	// game is in no list (a ROM since removed, a name RetroArch shortened)
	// keeps the file's own proportions and turn.
	Transform forScreenshotPath(const std::string& path)
	{
		static std::mutex mutex;
		static std::map<std::string, Transform> known;
		{
			std::unique_lock<std::mutex> lock(mutex);
			auto it = known.find(path);
			if (it != known.cend())
				return it->second;
		}
		const std::string content = DisplayAspectText::screenshotContent(Utils::FileSystem::getFileName(path));
		Transform t;
		bool found = false;
		if (!content.empty())
		{
			for (SystemData* system : SystemData::sSystemVector)
			{
				if (system == nullptr || system->isCollection() || !system->isGameSystem() || system->getRootFolder() == nullptr)
					continue;
				if (system->hasPlatformId(PlatformIds::IMAGEVIEWER))
					continue;
				for (FileData* game : system->getRootFolder()->getFilesRecursive(GAME))
				{
					if (Utils::FileSystem::getStem(game->getPath()) == content)
					{
						t = forGame(game);
						found = true;
						break;
					}
				}
				if (found)
					break;
			}
		}
		std::unique_lock<std::mutex> lock(mutex);
		known[path] = t;
		return t;
	}

	Transform forScreenshot(FileData* screenshot)
	{
		if (screenshot == nullptr)
			return Transform();
		return forScreenshotPath(screenshot->getPath());
	}

	void applyToBoundImages(GuiComponent* root, const std::string& imagePath, const Transform& transform)
	{
		if (root == nullptr)
			return;
		if (ImageComponent* image = dynamic_cast<ImageComponent*>(root))
		{
			const bool bound = !imagePath.empty() && image->getImagePath() == imagePath;
			image->setDisplayAspect(bound ? transform.aspect : 0.0f);
			image->setDisplayRotation(bound ? transform.turns : 0);
		}
		for (unsigned int i = 0; i < root->getChildCount(); i++)
			applyToBoundImages(root->getChild(i), imagePath, transform);
	}
}
