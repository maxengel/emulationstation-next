#include "DisplayAspect.h"

#include "DisplayAspectText.h"
#include "FileData.h"
#include "SystemData.h"
#include "PlatformId.h"
#include "Log.h"
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

	// The content name RetroArch wrote into the file name is the ROM's
	// file name without its extension. The game is looked for once per
	// screenshot across the game systems -- a list of a few hundred games
	// is a few hundred string compares, and the answer is kept, so moving
	// the cursor back over a screenshot costs nothing. A screenshot whose
	// game is in no list (a ROM since removed, a name RetroArch shortened)
	// keeps the file's own proportions.
	float forScreenshot(FileData* screenshot)
	{
		if (screenshot == nullptr)
			return 0.0f;
		static std::mutex mutex;
		static std::map<std::string, float> known;
		const std::string path = screenshot->getPath();
		{
			std::unique_lock<std::mutex> lock(mutex);
			auto it = known.find(path);
			if (it != known.cend())
				return it->second;
		}
		const std::string content = DisplayAspectText::screenshotContent(Utils::FileSystem::getFileName(path));
		float aspect = 0.0f;
		std::string matched;
		int considered = 0, gamesSeen = 0;
		if (!content.empty())
		{
			for (SystemData* system : SystemData::sSystemVector)
			{
				if (system == nullptr || system->isCollection() || !system->isGameSystem() || system->getRootFolder() == nullptr)
					continue;
				if (system->hasPlatformId(PlatformIds::IMAGEVIEWER) || DisplayAspectText::forSystem(system->getThemeFolder()) == 0.0f)
					continue;
				considered++;
				for (FileData* game : system->getRootFolder()->getFilesRecursive(GAME))
				{
					gamesSeen++;
					if (Utils::FileSystem::getStem(game->getPath()) == content)
					{
						aspect = DisplayAspectText::forSystem(system->getThemeFolder());
						matched = system->getName() + "/" + system->getThemeFolder();
						break;
					}
				}
				if (aspect > 0.0f)
					break;
			}
		}
		LOG(LogInfo) << "DisplayAspect: screenshot " << Utils::FileSystem::getFileName(path) << " content '" << content
			<< "' systems considered " << considered << " games seen " << gamesSeen << " matched '" << matched << "' aspect " << aspect;
		std::unique_lock<std::mutex> lock(mutex);
		known[path] = aspect;
		return aspect;
	}
}
