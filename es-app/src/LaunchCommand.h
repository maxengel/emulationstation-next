#pragma once
#ifndef ES_APP_LAUNCH_COMMAND_H
#define ES_APP_LAUNCH_COMMAND_H

#include <string>

// Readers for a finished launch command (fork #21 R5). The save manifest
// records exactly what the emulator was handed, so these read a token the
// way runemu.sh:23-26 reads it -- from its LAST occurrence to the next
// space -- and never re-resolve anything from SystemConf. Shared by
// FileData::getlaunchCommand (the ordinary launch) and launchStartupGame in
// main.cpp (the boot game, whose command is stored and replayed before any
// system is loaded, so the stored string is the only source).

// The value after the last " <prefix>" (" -P" -> the system name), or the
// value at the very start of the command when it begins with the prefix;
// "" when the command carries none.
inline std::string launchToken(const std::string& command, const std::string& prefix)
{
	const std::string needle = " " + prefix;
	size_t pos = command.rfind(needle);
	size_t start = (pos == std::string::npos)
		? (command.compare(0, prefix.size(), prefix) == 0 ? prefix.size() : std::string::npos)
		: pos + needle.size();
	if (start == std::string::npos)
		return "";
	size_t end = command.find(' ', start);
	return command.substr(start, end == std::string::npos ? std::string::npos : end - start);
}

// The value of a " --key=" token, with the leading space so a ROM path or a
// --controllers= blob containing the literal cannot win the search.
// `fallback` covers a per-system customCommandLine with no such token
// (config/emulators/tools.conf is "%RUNCOMMAND%").
inline std::string launchArgument(const std::string& command, const std::string& key, const std::string& fallback)
{
	std::string value = launchToken(command, key + "=");
	return value.empty() ? fallback : value;
}

#endif // ES_APP_LAUNCH_COMMAND_H
