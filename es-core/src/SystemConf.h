#ifndef EMULATIONSTATION_ALL_SYSTEMCONF_H
#define EMULATIONSTATION_ALL_SYSTEMCONF_H


#include <string>
#include <map>
#include <set>

class SystemConf 
{
public:
	static SystemConf* getInstance();
	
	static bool getIncrementalSaveStates();
	static bool getIncrementalSaveStatesUseCurrentSlot();

    bool loadSystemConf();
    bool saveSystemConf();

	// True when this start found system.cfg missing, empty or damaged and
	// loaded its last-known-good record (system.cfg.backup) in its place,
	// writing that back as the live file. main() tells the player once, after
	// the interface is up (D-CLOUD-079).
	static bool wasRecovered() { return sRecovered; }

    std::string get(const std::string &name);
    bool set(const std::string &name, const std::string &value);

	bool getBool(const std::string &name, bool defaultValue = false);
	bool setBool(const std::string &name, bool value);

private:
	SystemConf();
	static SystemConf* sInstance;
	static bool sRecovered;

	// Parse key=value lines into confMap. Comments and blank lines skipped,
	// as the file has always been read.
	void parseSystemConf(const std::string& text);
	// The last-known-good record beside the live file: written only from
	// text that has just been read whole and found well-formed, or that this
	// process has just written -- never from whatever happens to be on disk.
	void recordLastGood(const std::string& text);

	std::map<std::string, std::string> confMap;
	std::set<std::string> changedConf;


	std::string mSystemConfFile;
};


#endif //EMULATIONSTATION_ALL_SYSTEMCONF_H
