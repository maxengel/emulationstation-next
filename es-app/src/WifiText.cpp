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

std::vector<WifiText::PickerRow> WifiText::pickerRows(const std::vector<std::string>& inRange, const std::vector<SavedNetwork>& saved, const std::string& current,
	bool savedKnown, bool currentKnown)
{
	// The device's network when `wifictl current` could not say: the saved
	// list marks the profile that is active (#308 F-WF-03).
	std::string joinedNow = current;
	if (!currentKnown && savedKnown)
		for (const auto& network : saved)
			if (network.inUse)
			{
				joinedNow = network.name;
				break;
			}

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

	if (!joinedNow.empty())
		rows.push_back({ joinedNow, savedKnown && isSaved(joinedNow), true, savedKnown });

	for (const auto& rawName : inRange)
	{
		const std::string name = withoutCR(rawName);
		if (name.empty() || listed(name))
			continue;
		rows.push_back({ name, savedKnown && isSaved(name), false, savedKnown });
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

WifiText::JoinFailure WifiText::joinFailure(int exitCode)
{
	return exitCode == 2 ? JoinFailure::ServiceNotAnswering : JoinFailure::MayBeKey;
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

WifiText::PressAction WifiText::pressAction(const PickerRow& row)
{
	if (row.connected || row.saved)
		return PressAction::Join;
	if (!row.savedKnown)
		return PressAction::CheckAgain;
	return PressAction::AskKey;
}

WifiText::PressAction WifiText::manualAction(const std::string& name, const std::string& current, const std::vector<SavedNetwork>& saved, bool savedKnown)
{
	if (!name.empty() && name == current)
		return PressAction::Join;
	for (const auto& network : saved)
		if (network.name == name)
			return PressAction::Join;
	if (!savedKnown)
		return PressAction::CheckAgain;
	return PressAction::AskKey;
}

std::string WifiText::joinedNotice(const std::string& name, const std::string& connectedWord)
{
	return name + " : " + connectedWord;
}
