#include "WifiText.h"

// Only a CR: the name is the rest of the line, spaces and all.
static std::string withoutCR(const std::string& line)
{
	if (!line.empty() && line.back() == '\r')
		return line.substr(0, line.size() - 1);
	return line;
}

bool WifiText::parseSavedLine(const std::string& rawLine, SavedNetwork& network)
{
	const std::string line = withoutCR(rawLine);
	const size_t tab = line.rfind('\t');
	if (tab == std::string::npos)
		return false;

	const std::string name = line.substr(0, tab);
	const std::string flag = line.substr(tab + 1);
	if (name.empty())
		return false;

	if (flag == "active")
		network.inUse = true;
	else if (flag == "saved")
		network.inUse = false;
	else
		return false;

	network.name = name;
	return true;
}

std::vector<WifiText::SavedNetwork> WifiText::parseSaved(const std::vector<std::string>& lines)
{
	std::vector<SavedNetwork> networks;
	for (const auto& line : lines)
	{
		SavedNetwork network;
		if (parseSavedLine(line, network))
			networks.push_back(network);
	}
	return networks;
}

std::string WifiText::parseCurrent(const std::vector<std::string>& lines)
{
	for (const auto& line : lines)
	{
		const std::string ssid = withoutCR(line);
		if (!ssid.empty())
			return ssid;
	}
	return "";
}

std::vector<WifiText::PickerRow> WifiText::pickerRows(const std::vector<std::string>& inRange, const std::vector<SavedNetwork>& saved, const std::string& current)
{
	auto isSaved = [&saved](const std::string& name)
	{
		for (const auto& network : saved)
			if (network.name == name)
				return true;
		return false;
	};

	std::vector<PickerRow> rows;
	auto listed = [&rows](const std::string& name)
	{
		for (const auto& row : rows)
			if (row.name == name)
				return true;
		return false;
	};

	if (!current.empty())
		rows.push_back({ current, isSaved(current), true });

	for (const auto& rawName : inRange)
	{
		const std::string name = withoutCR(rawName);
		if (name.empty() || listed(name))
			continue;
		rows.push_back({ name, isSaved(name), false });
	}
	return rows;
}

bool WifiText::parseJoin(const std::vector<std::string>& lines)
{
	for (const auto& rawLine : lines)
		if (withoutCR(rawLine) == "joined")
			return true;
	return false;
}

WifiText::ForgetOutcome WifiText::parseForget(const std::vector<std::string>& lines)
{
	ForgetOutcome outcome;
	for (const auto& rawLine : lines)
	{
		const std::string line = withoutCR(rawLine);
		if (line == "forgotten")
			outcome.forgotten = true;
		else if (line == "disconnected" && outcome.forgotten)
			outcome.disconnected = true;
	}
	return outcome;
}
