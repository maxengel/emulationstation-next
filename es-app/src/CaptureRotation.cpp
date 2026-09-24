#include "CaptureRotation.h"

#include "DisplayAspect.h"
#include <sys/stat.h>

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
}

namespace
{
	// The core's own table (fork #248): <romname> <turns>, one line per
	// game with a turn, installed with the core. Read once per core and
	// kept for the process's life on purpose: the tables are on the
	// read-only image and change only with an update, which restarts the
	// interface (audit #258 PL-019 asked of each cache when it goes stale).
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

	// The turns read per record path, with the record's modification time
	// when one existed (0 for a turn that came from the table). A record
	// written behind the interface's back -- by hand over ssh, by a restore
	// -- has a newer mtime than the one cached, and is read again; before
	// this the cache was updated by recordAfterSession alone, so a record
	// that arrived any other way was not seen until a restart (audit #258
	// PL-019). The stat is one syscall a lookup; a screenshot list of a few
	// hundred is a few hundred stats, once per rebuild of its tiles.
	struct Known { int turns; time_t mtime; };
	std::map<std::string, Known> sKnownAt;

	static time_t recordMtime(const std::string& path)
	{
		struct stat st;
		return ::stat(path.c_str(), &st) == 0 ? st.st_mtime : 0;
	}

	int read(FileData* game)
	{
		const std::string path = recordPath(game);
		if (path.empty())
			return 0;
		const time_t mtime = recordMtime(path);
		{
			std::unique_lock<std::mutex> lock(sLock);
			auto it = sKnownAt.find(path);
			if (it != sKnownAt.cend() && it->second.mtime == mtime)
				return it->second.turns;
		}
		// Uncached exists: the record is written while the interface runs.
		int turns = 0;
		if (mtime != 0 && Utils::FileSystem::exists(path, false))
			turns = CaptureRotationText::parseRecord(Utils::FileSystem::readAllText(path));
		else
			turns = fromTable(game);
		std::unique_lock<std::mutex> lock(sLock);
		sKnownAt[path] = Known{ turns, mtime };
		return turns;
	}

	void forgetAll()
	{
		std::unique_lock<std::mutex> lock(sLock);
		sKnownAt.clear();
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

		{
			std::unique_lock<std::mutex> lock(sLock);
			sKnownAt[path] = Known{ turns, recordMtime(path) };
		}
		// The screenshots of this game were resolved to a Transform before
		// the session; that cache is keyed by screenshot path and knows
		// nothing of the record, so it is emptied here and rebuilt on the
		// next look (audit #258 PL-019).
		DisplayAspect::forgetScreenshots();
	}
}
