#include "SystemConf.h"
#include <iostream>
#include <fstream>
#include "Log.h"
#include "utils/StringUtil.h"
#include "utils/FileSystemUtil.h"
#include "Settings.h"
#include "Paths.h"
#include "utils/AtomicFileUtil.h"

#include <cstdio>
#include <set>
#include <regex>
#include <string>
#include <sstream>
#include <iostream>
#include <SDL_timer.h>

static std::string mapSettingsName(const std::string& name)
{
	if (name == "system.language")
		return "Language";

	return name;
}

SystemConf *SystemConf::sInstance = NULL;
bool SystemConf::sRecovered = false;

static std::set<std::string> dontRemoveValue
{
	{ "audio.device" },
	{ "updates.branch" }
};

static std::map<std::string, std::string> defaults =
{
	{ "kodi.enabled", "1" },
	{ "kodi.atstartup", "0" },
	{ "audio.bgmusic", "1" },
	{ "wifi.enabled", "0" },
	{ "system.hostname", "BATOCERA" }, // batocera
	{ "global.retroachievements", "0" },
	{ "global.retroachievements.hardcore", "0" },
	{ "global.retroachievements.leaderboards", "0" },
	{ "global.retroachievements.verbose", "0" },
	{ "global.retroachievements.screenshot", "0" },
	{ "global.retroachievements.username", "" },
	{ "global.retroachievements.password", "" },
	{ "global.netplay_public_announce", "1" },
	{ "global.ai_service_enabled", "0" },
};

SystemConf::SystemConf() 
{
	mSystemConfFile = Paths::getSystemConfFilePath();
	if (mSystemConfFile.empty())
		return;

	loadSystemConf();	
}

SystemConf *SystemConf::getInstance() 
{
    if (sInstance == NULL)
        sInstance = new SystemConf();

    return sInstance;
}

// Whether a system.cfg text is one worth reading and worth keeping as the
// last-known-good record: something in it, no NUL bytes (a file cut by a
// power failure reads as a run of them, which is what the boot check's
// "binary" test used to catch), and at least one key=value line. A file that
// is merely truncated at the tail still passes -- there is no way to tell a
// short file from a shorter configuration -- so this is the floor, not a
// proof; the shell's boot check applies its own on top (D-CLOUD-079).
static bool isUsableSystemConf(const std::string& text)
{
	if (text.empty() || text.find('\0') != std::string::npos)
		return false;
	std::istringstream in(text);
	std::string line;
	while (std::getline(in, line))
	{
		auto idx = line.find('=');
		if (idx != std::string::npos && idx > 0 && line[0] != '#' && line[0] != ';')
			return true;
	}
	return false;
}

void SystemConf::parseSystemConf(const std::string& text)
{
	std::istringstream in(text);
	std::string line;
	while (std::getline(in, line))
	{
		int idx = line.find("=");
		if (idx == std::string::npos || line.find("#") == 0 || line.find(";") == 0)
			continue;

		std::string key = line.substr(0, idx);
		std::string value = line.substr(idx + 1);
		if (!key.empty() && !value.empty())
			confMap[key] = value;
	}
}

// One record at rest, under the name the boot scripts already use
// (system.cfg.backup, D-CLOUD-079). Written whole through a temporary and a
// rename, like the live file, and only when it would change -- this runs at
// every start and after every save, and most of those change nothing.
void SystemConf::recordLastGood(const std::string& text)
{
	// No less private than the live file it copies (#308 F-ES-08): a record
	// an earlier build made 0644 beside a 0600 file is rewritten for its mode
	// even when its text is the same.
	const std::string backup = mSystemConfFile + ".backup";
	const int mode = Utils::AtomicFile::modeOf(mSystemConfFile, 0644);
	if (Utils::AtomicFile::readText(backup) == text && Utils::AtomicFile::modeOf(backup, mode) == mode)
		return;
	if (!Utils::AtomicFile::writeText(backup, text, mode))
		LOG(LogWarning) << "Unable to write the last-known-good record " << backup;
}

// Read the live file if it is usable; else the last-known-good record, and
// put that back as the live file; else whatever is there, so a device whose
// file fails this check but was fine for years reads exactly as before, and
// nothing is written over it (D-CLOUD-078: never defaults over a file that
// failed to parse before the recovery was tried).
bool SystemConf::loadSystemConf()
{
	if (mSystemConfFile.empty())
		return true;

	changedConf.clear();

	// A temporary left by a save that never reached its rename is litter,
	// not a record: nothing reads it, and the next save replaces it. The
	// record's own temporary likewise.
	std::remove((mSystemConfFile + ".tmp").c_str());
	std::remove((mSystemConfFile + ".backup.tmp").c_str());

	bool liveOpened = false;
	const std::string live = Utils::AtomicFile::readText(mSystemConfFile, &liveOpened);
	if (liveOpened && isUsableSystemConf(live))
	{
		parseSystemConf(live);
		recordLastGood(live);
		return true;
	}

	const std::string backupPath = mSystemConfFile + ".backup";
	bool backupOpened = false;
	const std::string backup = Utils::AtomicFile::readText(backupPath, &backupOpened);
	if (backupOpened && isUsableSystemConf(backup))
	{
		LOG(LogWarning) << mSystemConfFile << " is " << (liveOpened ? (live.empty() ? "empty" : "damaged") : "missing")
			<< " -- loading the last-known-good record " << backupPath << " and writing it back";
		parseSystemConf(backup);
		if (!Utils::AtomicFile::writeText(mSystemConfFile, backup))
			LOG(LogError) << "Unable to write " << mSystemConfFile << " back from its last-known-good record";
		sRecovered = true;
		return true;
	}

	if (!liveOpened)
	{
		LOG(LogError) << "Unable to open " << mSystemConfFile;
		return false;
	}

	// Both unusable by the check above. Read the live file as it always was
	// read -- a file this check is wrong about still works -- and record
	// nothing: there is no good copy to record.
	LOG(LogError) << mSystemConfFile << " has no usable key=value line and no usable last-known-good record; reading it as it is";
	parseSystemConf(live);
	return true;
}

bool SystemConf::saveSystemConf()
{
	if (mSystemConfFile.empty())
		return Settings::getInstance()->saveFile();	

	if (changedConf.empty())
		return false;

	// The shell's settings lock (wait_lock in profile.d/001-functions), held
	// across the read-modify-write: set_setting reads the file, writes a
	// temporary and renames it under the same lock, and a save that read or
	// renamed beside it put one writer's snapshot over the other's. Five
	// seconds is the budget, on the interface thread. A live holder still on
	// the lock past it is logged and nothing is written (PL-024): this used to
	// save anyway, and lost either the script's key or the player's. The
	// changes stay in changedConf, and the next save -- any page closing,
	// any set-and-save -- writes them over whatever the script left.
	std::string out;
	const Utils::AtomicFile::LockedSave saved = Utils::AtomicFile::saveUnderLock(mSystemConfFile, "/tmp/.system.cfg.lock", 5000,
		[this](const std::string& current) { return applyChanges(current); }, &out);

	switch (saved)
	{
	case Utils::AtomicFile::LockedSave::LockBusy:
		LOG(LogWarning) << "saveSystemConf: the settings lock was not free within 5s; nothing written -- the changes are kept for the next save";
		return false;
	case Utils::AtomicFile::LockedSave::Unreadable:
		LOG(LogError) << "Unable to open for saving :  " << mSystemConfFile << " -- the changes are kept for the next save";
		return false;
	case Utils::AtomicFile::LockedSave::WriteFailed:
		// Written to a temporary of its own, synced, and renamed over the
		// live file (D-CLOUD-079, PL-063); a failure leaves the file whole.
		LOG(LogError) << "Unable to write " << mSystemConfFile << " -- the changes are kept for the next save";
		return false;
	case Utils::AtomicFile::LockedSave::Written:
		break;
	}

	changedConf.clear();

	// What was just written is, by construction, the newest good state, so
	// it becomes the record (D-CLOUD-078: a success becomes the last known
	// good). Outside the lock: the shell never takes it for the record.
	recordLastGood(out);

	return true;
}

// This save's keys, applied to the file as it is on disk now (read under
// the lock): a key already there is rewritten in place, or removed when it
// went back to its default or to nothing; a new one goes at the end. Every
// other line is left exactly as the file had it -- the shell's writes
// included.
std::string SystemConf::applyChanges(const std::string& current)
{
	/* Read all lines in a vector */
	std::vector<std::string> fileLines;
	std::string line;

	std::istringstream filein(current);
	while (std::getline(filein, line))
		fileLines.push_back(line);

	static std::string removeID = "$^�(p$^mpv$�rpver$^vper$vper$^vper$vper$vper$^vperv^pervncvizn";

	int lastTime = SDL_GetTicks();

	/* Save new value if exists */
	for (auto& it : changedConf)
	{
		std::string key = it + "=";
		char key0 = key[0];

		bool lineFound = false;

		for (auto& currentLine : fileLines)
		{
			if (currentLine.size() < 3)
				continue;

			char fc = currentLine[0];
			if (fc != key0 && currentLine[1] != key0)
				continue;

			int idx = currentLine.find(key);
			if (idx == std::string::npos)
				continue;

			if (idx == 0 || (idx == 1 && (fc == ';' || fc == '#')))
			{
				std::string val = confMap[it];
				if ((!val.empty() && val != "auto" && val != "default") || dontRemoveValue.find(it) != dontRemoveValue.cend())
				{
					auto defaultValue = defaults.find(key);
					if (defaultValue != defaults.cend() && defaultValue->second == val)
						currentLine = removeID;
					else
						currentLine = key + val;
				}
				else 
					currentLine = removeID;

				lineFound = true;
			}
		}

		if (!lineFound)
		{
			std::string val = confMap[it];
			if (!val.empty() && val != "auto")
				fileLines.push_back(key + val);
		}
	}

	lastTime = SDL_GetTicks() - lastTime;

	LOG(LogDebug) << "saveSystemConf :  " << lastTime;

	std::string out;
	for (int i = 0; i < fileLines.size(); i++) 
	{
		if (fileLines[i] != removeID)
			out += fileLines[i] + "\n";
	}
	return out;
}

std::string SystemConf::get(const std::string &name) 
{
	if (mSystemConfFile.empty())
		return Settings::getInstance()->getString(mapSettingsName(name));
	
	auto it = confMap.find(name);
	if (it != confMap.cend())
		return it->second;

	auto dit = defaults.find(name);
	if (dit != defaults.cend())
		return dit->second;

    return "";
}

bool SystemConf::set(const std::string &name, const std::string &value) 
{
	if (mSystemConfFile.empty())
		return Settings::getInstance()->setString(mapSettingsName(name), value == "auto" ? "" : value);

	if (confMap.count(name) == 0 || confMap[name] != value)
	{
		confMap[name] = value;
		changedConf.insert(name);
		return true;
	}

	return false;
}

bool SystemConf::getBool(const std::string &name, bool defaultValue)
{
	if (mSystemConfFile.empty())
		return Settings::getInstance()->getBool(mapSettingsName(name));

	if (defaultValue)
		return get(name) != "0";

	return get(name) == "1";
}

bool SystemConf::setBool(const std::string &name, bool value)
{
	if (mSystemConfFile.empty())
		return Settings::getInstance()->setBool(mapSettingsName(name), value);

	return set(name, value  ? "1" : "0");
}

bool SystemConf::getIncrementalSaveStates()
{
	auto valGSS = SystemConf::getInstance()->get("global.incrementalsavestates");
	return valGSS != "0" && valGSS != "2";
}

bool SystemConf::getIncrementalSaveStatesUseCurrentSlot()
{
	return SystemConf::getInstance()->get("global.incrementalsavestates") == "2";
}
