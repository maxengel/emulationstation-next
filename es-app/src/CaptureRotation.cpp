#include "CaptureRotation.h"

#include "CaptureRotationText.h"
#include "FileData.h"
#include "Log.h"
#include "SaveStateConfigFile.h"
#include "SystemData.h"
#include "utils/FileSystemUtil.h"

#include <map>
#include <mutex>

namespace
{
	const char* LAUNCH_LOG = "/var/log/exec.log";
	const char* RETROARCH_CONFIG = "/storage/.config/retroarch/retroarch.cfg";

	std::mutex sLock;
	std::map<std::string, int> sKnown;   // record path -> turns
}

namespace
{
	// The core's own table (fork #248): <romname> <turns>, one line per
	// game with a turn, installed with the core. Read once per core.
	const char* TABLE_DIR = "/usr/config/emulationstation/rotation";
	std::map<std::string, std::string> sTables;   // core -> table text

	int fromTable(FileData* game)
	{
		FileData* source = game->getSourceFileData();
		if (source == nullptr)
			return 0;
		const std::string core = source->getCore(true);
		if (core.empty())
			return 0;
		std::string table;
		{
			std::unique_lock<std::mutex> lock(sLock);
			auto it = sTables.find(core);
			if (it != sTables.cend())
				table = it->second;
			else
			{
				const std::string path = std::string(TABLE_DIR) + "/" + core + ".txt";
				if (Utils::FileSystem::exists(path))
					table = Utils::FileSystem::readAllText(path);
				sTables[core] = table;
			}
		}
		if (table.empty())
			return 0;
		return CaptureRotationText::turnsFromTable(table, Utils::FileSystem::getStem(source->getPath()));
	}
}

namespace CaptureRotation
{
	std::string recordPath(FileData* game)
	{
		if (game == nullptr)
			return "";
		FileData* source = game->getSourceFileData();
		if (source == nullptr || source->getSystem() == nullptr)
			return "";
		for (auto rs : SaveStateConfigFile::getSaveStateConfigs(source->getSystem()))
		{
			const std::string dir = rs->getDirectory(source->getSystem());
			if (dir.empty())
				continue;
			return dir + "/" + Utils::FileSystem::getStem(source->getPath()) + ".rotation";
		}
		return "";
	}

	int read(FileData* game)
	{
		const std::string path = recordPath(game);
		if (path.empty())
			return 0;
		{
			std::unique_lock<std::mutex> lock(sLock);
			auto it = sKnown.find(path);
			if (it != sKnown.cend())
				return it->second;
		}
		// Uncached exists: the record is written while the interface runs.
		int turns = 0;
		if (Utils::FileSystem::exists(path, false))
			turns = CaptureRotationText::parseRecord(Utils::FileSystem::readAllText(path));
		else
			turns = fromTable(game);
		std::unique_lock<std::mutex> lock(sLock);
		sKnown[path] = turns;
		return turns;
	}

	void recordAfterSession(FileData* game, const std::string& emulator)
	{
		if (emulator != "retroarch")
			return;
		const std::string path = recordPath(game);
		if (path.empty())
			return;
		if (!Utils::FileSystem::exists(LAUNCH_LOG, false))
			return;

		const int coreTurns = CaptureRotationText::turnsFromLog(Utils::FileSystem::readAllText(LAUNCH_LOG));
		std::string config;
		if (Utils::FileSystem::exists(RETROARCH_CONFIG, false))
			config = Utils::FileSystem::readAllText(RETROARCH_CONFIG);
		const int turns = CaptureRotationText::fold(coreTurns, config);

		const bool had = Utils::FileSystem::exists(path, false);
		if (!had && turns == 0)
			return;
		if (had && CaptureRotationText::parseRecord(Utils::FileSystem::readAllText(path)) == turns)
			return;

		Utils::FileSystem::createDirectory(Utils::FileSystem::getParent(path));
		Utils::FileSystem::writeAllText(path, CaptureRotationText::recordText(turns));
		LOG(LogInfo) << "CaptureRotation: " << game->getName() << " turns " << turns << " (core " << coreTurns << ") -> " << path;

		std::unique_lock<std::mutex> lock(sLock);
		sKnown[path] = turns;
	}
}
