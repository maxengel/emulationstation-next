#include "CaptureRotationText.h"

#include <cstdlib>

namespace
{
	// The value of `key = "value"` in a RetroArch config, or "" when absent.
	std::string configValue(const std::string& config, const std::string& key)
	{
		size_t pos = 0;
		while ((pos = config.find(key, pos)) != std::string::npos)
		{
			const bool atLineStart = pos == 0 || config[pos - 1] == '\n' || config[pos - 1] == '\r';
			const size_t after = pos + key.size();
			pos = after;
			if (!atLineStart)
				continue;
			size_t p = after;
			while (p < config.size() && (config[p] == ' ' || config[p] == '\t'))
				p++;
			if (p >= config.size() || config[p] != '=')
				continue;
			p++;
			while (p < config.size() && (config[p] == ' ' || config[p] == '\t'))
				p++;
			if (p < config.size() && config[p] == '"')
			{
				const size_t end = config.find('"', p + 1);
				return end == std::string::npos ? "" : config.substr(p + 1, end - p - 1);
			}
			const size_t end = config.find_first_of("\r\n", p);
			return config.substr(p, end == std::string::npos ? std::string::npos : end - p);
		}
		return "";
	}
}

namespace CaptureRotationText
{
	int turnsFromLog(const std::string& launchLog)
	{
		const std::string tag = "SET_ROTATION: \"";
		size_t pos = launchLog.rfind(tag);
		if (pos == std::string::npos)
			return -1;
		pos += tag.size();
		if (pos >= launchLog.size() || launchLog[pos] < '0' || launchLog[pos] > '9')
			return -1;
		return (launchLog[pos] - '0') % 4;
	}

	int fold(int coreTurns, const std::string& retroarchConfig)
	{
		int turns = coreTurns < 0 ? 0 : coreTurns % 4;
		const std::string allow = configValue(retroarchConfig, "video_allow_rotate");
		if (allow == "false")
			turns = 0;
		const std::string own = configValue(retroarchConfig, "video_rotation");
		if (!own.empty() && own[0] >= '0' && own[0] <= '9')
			turns = (turns + (own[0] - '0')) % 4;
		return turns;
	}

	int turnsFromTable(const std::string& table, const std::string& romName)
	{
		if (romName.empty())
			return 0;
		size_t pos = 0;
		while (pos < table.size())
		{
			size_t end = table.find('\n', pos);
			if (end == std::string::npos)
				end = table.size();
			const std::string line = table.substr(pos, end - pos);
			pos = end + 1;
			if (line.size() < romName.size() + 2 || line.compare(0, romName.size(), romName) != 0 || line[romName.size()] != ' ')
				continue;
			const char c = line[romName.size() + 1];
			if (c < '0' || c > '3')
				return 0;
			return c - '0';
		}
		return 0;
	}

	std::string recordText(int turns)
	{
		turns = ((turns % 4) + 4) % 4;
		return "turns=" + std::string(1, (char)('0' + turns)) + "\n";
	}

	int parseRecord(const std::string& text)
	{
		size_t p = 0;
		while (p < text.size() && (text[p] == ' ' || text[p] == '\t' || text[p] == '\r' || text[p] == '\n'))
			p++;
		const std::string key = "turns=";
		if (text.compare(p, key.size(), key) == 0)
			p += key.size();
		if (p >= text.size() || text[p] < '0' || text[p] > '3')
			return 0;
		return text[p] - '0';
	}
}
