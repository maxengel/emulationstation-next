#include "CloudText.h"

#include "CloudExit.h"
#include "utils/StringUtil.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <set>

namespace CloudText
{

const std::vector<std::pair<std::string, std::string>>& recommendedProviders()
{
	static const std::vector<std::pair<std::string, std::string>> recommended = {
		{ "dropbox",     "DROPBOX" },
		{ "drive",       "GOOGLE DRIVE" },
		{ "onedrive",    "MICROSOFT ONEDRIVE" },
		{ "box",         "BOX" },
		{ "pcloud",      "PCLOUD" },
		{ "mega",        "MEGA" },
		{ "protondrive", "PROTON DRIVE" },
		{ "koofr",       "KOOFR" },
		{ "webdav",      "WEBDAV" },
		{ "sftp",        "SSH / SFTP" },
		{ "smb",         "WINDOWS SHARE (SMB)" },
		{ "s3",          "AMAZON S3 AND COMPATIBLE" },
		{ "b2",          "BACKBLAZE B2" },
		{ "storj",       "STORJ" },
	};
	return recommended;
}

std::string providerLabel(const std::string& type)
{
	if (type.empty())
		return "";
	for (auto& p : recommendedProviders())
		if (p.first == type)
			return p.second;
	return Utils::String::toUpper(type);
}

std::string providerSubtitle(const std::string& type, const std::string& label)
{
	const std::string text = Utils::String::trim(label);
	// One line's worth at 640 wide in the subtitle font, and no list: a
	// label that enumerates is a description, not a name.
	if (!text.empty() && text.size() <= 40 && text.find(',') == std::string::npos)
		return Utils::String::toUpper(text);
	return providerLabel(type);
}

std::string fieldLabel(const std::string& rcloneName)
{
	// The fields the recommended providers (recommendedProviders above)
	// put in front of a player, as rclone 1.75 names them. Anything a
	// provider adds later falls through to the spaced name below, which
	// is readable if not chosen.
	static const std::vector<std::pair<std::string, std::string>> words = {
		// where
		{ "url",                        "SERVER ADDRESS" },
		{ "host",                       "SERVER ADDRESS" },
		{ "endpoint",                   "ENDPOINT ADDRESS" },
		{ "port",                       "PORT" },
		{ "vendor",                     "SERVER TYPE" },
		{ "provider",                   "PROVIDER" },
		{ "region",                     "REGION" },
		{ "location_constraint",        "LOCATION" },
		{ "tenant",                     "TENANT" },
		{ "domain",                     "DOMAIN" },
		{ "spn",                        "SERVICE NAME" },
		// who
		{ "user",                       "USERNAME" },
		{ "pass",                       "PASSWORD" },
		{ "2fa",                        "TWO-FACTOR CODE" },
		{ "bearer_token",               "ACCESS TOKEN" },
		{ "access_token",               "ACCESS TOKEN" },
		{ "token",                      "SIGN-IN TOKEN" },
		{ "access_key_id",              "ACCESS KEY ID" },
		{ "secret_access_key",          "SECRET ACCESS KEY" },
		{ "env_auth",                   "KEYS FROM THE SYSTEM" },
		{ "account",                    "ACCOUNT ID" },
		{ "key",                        "APPLICATION KEY" },
		{ "client_id",                  "APP ID" },
		{ "client_secret",              "APP SECRET" },
		{ "scope",                      "ACCESS SCOPE" },
		{ "service_account_file",       "SERVICE ACCOUNT FILE" },
		{ "box_config_file",            "BOX CONFIG FILE" },
		{ "box_sub_type",               "ACCOUNT TYPE" },
		{ "drive_type",                 "DRIVE TYPE" },
		{ "use_kerberos",               "USE KERBEROS" },
		// ssh
		{ "key_pem",                    "PRIVATE KEY (PASTED)" },
		{ "key_file",                   "PRIVATE KEY FILE" },
		{ "key_file_pass",              "PRIVATE KEY PASSWORD" },
		{ "pubkey",                     "PUBLIC KEY (PASTED)" },
		{ "pubkey_file",                "PUBLIC KEY FILE" },
		{ "key_use_agent",              "USE SSH AGENT" },
		{ "use_insecure_cipher",        "ALLOW OLD CIPHERS" },
		{ "disable_hashcheck",          "SKIP CHECKSUMS" },
		{ "ssh",                        "SSH COMMAND" },
		// how
		{ "tls",                        "SECURE (IMPLICIT TLS)" },
		{ "explicit_tls",               "SECURE (EXPLICIT TLS)" },
		{ "acl",                        "ACCESS PERMISSIONS" },
		{ "storage_class",              "STORAGE CLASS" },
		{ "bucket_object_lock_enabled", "OBJECT LOCK ON BUCKET" },
	};
	const std::string name = Utils::String::toLower(Utils::String::trim(rcloneName));
	if (name.empty())
		return "";
	for (auto& w : words)
		if (w.first == name)
			return w.second;
	return Utils::String::toUpper(Utils::String::replace(name, "_", " "));
}

std::string cleanHostname(const std::string& in)
{
	std::string out;
	bool gap = false;
	for (char c : in)
	{
		const bool keep = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
		if (keep)
		{
			if (gap && !out.empty())
				out += '-';
			gap = false;
			out += c;
		}
		else
			gap = true;
	}
	if (out.size() > 63)
	{
		out.resize(63);
		while (!out.empty() && out.back() == '-')
			out.pop_back();
	}
	return out;
}

bool isOutcomeToken(const std::string& token)
{
	static const std::set<std::string> ourTokens = {
		"completed", "gaps", "no-network", "lock-held", "cancelled", "stopped",
		"folder-missing", "cloud-stopped", "cloud-refused", "unknown" };
	return ourTokens.find(token) != ourTokens.cend();
}

LastRun parseLastRun(const std::string& text)
{
	LastRun r;
	auto parts = Utils::String::split(Utils::String::trim(text), ' ', true);
	if (parts.size() < 2)
		return r;
	time_t when = (time_t) atoll(parts[0].c_str());
	if (when <= 0)
		return r;
	r.ran = true;
	r.when = when;
	// "<epoch> <rc>[ <token>[ <why...>]]" (D-UI-028). The first two fields
	// are what every stamp has always carried; the token is one word for
	// the outcome where the code alone cannot say it (a 130 that was a
	// launch cancel; a composed run whose parts disagreed), and the why
	// is the scripts' own sentence when they printed one. A stamp with two
	// fields -- one written before this reader -- is read from its code.
	//
	// The four words, with commas rather than dashes inside the outcome:
	// the line already uses a dash to separate the date from it. LockHeld
	// and NoNetwork are the scripts' "another sync holds the lock" and "no
	// network" (CloudExit.h): the scripts write no stamp for those (nothing
	// ran), but the whole-run stamps EmulationStation keeps per cause
	// (last-sync-startup, -exit, -manual; fork #94) do, because under SYNC
	// SAVES DURING STARTUP the player's question is what happened this
	// morning, and "nothing, there was no network" answers it.
	const int code = atoi(parts[1].c_str());
	r.code = code;
	//
	// Two writers, two shapes of third field. EmulationStation's stamps
	// (ThreadedCloudSync::recordOutcome) carry one of its tokens, with the
	// scripts' sentence after it when they printed one; the scripts' own
	// stamps (last-backup, last-restore, last-content-*) carry the sentence
	// itself as the third field, spaces turned into underscores
	// ("1789000000 5 YOUR_CLOUD_STOPPED_ANSWERING"), and only for a code
	// that is not 0, 9, 69 or 75. So a third field that is not one of our
	// tokens is the why, read back with its underscores as spaces.
	const std::string token = parts.size() > 2 ? parts[2] : "";
	const bool ours = isOutcomeToken(token);
	std::string why;
	for (size_t i = ours ? 3 : 2; i < parts.size(); i++)
		why += (why.empty() ? "" : " ") + parts[i];
	if (!ours)
		why = Utils::String::toUpper(Utils::String::replace(why, "_", " "));
	while (!why.empty() && why.back() == '.')
		why.pop_back();

	r.token = token;
	r.knownToken = ours;
	if (code == 0 || code == 9 || token == "completed")
	{
		r.outcome = Outcome::Completed;
		r.finished = true;
	}
	else if (token == "gaps")
	{
		// A run whose parts disagreed. The stamp keeps its own token so a log
		// can tell it from a total failure; the row says what every other
		// failure says (D-UI-030).
		r.outcome = Outcome::Gaps;
		r.why = why;
	}
	else if (code == CloudExit::LockHeld)
		r.outcome = Outcome::SkippedLockHeld;
	else if (code == CloudExit::NoNetwork)
		r.outcome = Outcome::SkippedNoNetwork;
	else if (token == "cancelled")
		r.outcome = Outcome::SkippedGameStarted;
	else
	{
		r.outcome = Outcome::Failed;
		r.why = why;
	}
	return r;
}

RunOrigin runOrigin(time_t when, time_t exitWhen, time_t startupWhen)
{
	if (when <= 0)
		return RunOrigin::None;
	if (exitWhen > 0 && std::labs((long) (exitWhen - when)) <= 10)
		return RunOrigin::AfterLastGame;
	if (startupWhen > 0 && std::labs((long) (startupWhen - when)) <= 10)
		return RunOrigin::AtStartup;
	return RunOrigin::None;
}

std::string shortenWhy(const std::string& why)
{
	const size_t dash = why.find(" - ");
	if (dash != std::string::npos)
		return Utils::String::trim(why.substr(0, dash));

	const size_t paren = why.find(" (");
	if (paren != std::string::npos)
		return Utils::String::trim(why.substr(0, paren));

	const size_t stop = why.find(". ");
	if (stop != std::string::npos)
		return Utils::String::trim(why.substr(0, stop));

	return "";
}

std::vector<std::string> outcomeCandidates(const std::string& outcome)
{
	std::vector<std::string> candidates;
	candidates.push_back(outcome);

	const size_t dash = outcome.find(" - ");
	if (dash != std::string::npos)
	{
		const std::string head = outcome.substr(0, dash);
		const std::string tail = outcome.substr(dash + 3);
		const std::string shortTail = shortenWhy(tail);
		if (!shortTail.empty() && shortTail != tail)
			candidates.push_back(head + std::string(" - ") + shortTail);
		candidates.push_back(head);
	}

	return candidates;
}

ProtocolLine classifyProtocolLine(const std::string& clean)
{
	ProtocolLine out;
	if (clean.rfind(">>> ", 0) != 0)
		return out;

	if (clean.rfind(">>> pid ", 0) == 0)
	{
		out.kind = ProtocolKind::Pid;
		out.number = atoi(clean.substr(8).c_str());
	}
	else if (clean.rfind(">>> doing ", 0) == 0)
	{
		out.kind = ProtocolKind::Doing;
		out.text = Utils::String::trim(clean.substr(10));
	}
	else if (clean.rfind(">>> why ", 0) == 0)
	{
		out.kind = ProtocolKind::Why;
		std::string why = Utils::String::toUpper(Utils::String::trim(clean.substr(8)));
		// The outcome line supplies its own end; a sentence's
		// full stop after a dash reads as a typo.
		while (!why.empty() && why.back() == '.')
			why.pop_back();
		out.text = why;
	}
	else if (clean.rfind(">>> offer ", 0) == 0)
	{
		out.kind = ProtocolKind::Offer;
		auto parts = Utils::String::split(clean.substr(10), '|', false);
		out.text = parts.size() > 0 ? Utils::String::trim(parts[0]) : "";
		for (size_t i = 1; i < parts.size(); i++)
			out.args.push_back(Utils::String::trim(parts[i]));
	}
	else if (clean.rfind(">>> tier ", 0) == 0)
	{
		out.kind = ProtocolKind::Tier;
		auto parts = Utils::String::split(clean.substr(9), '|', false);
		out.text = parts.size() > 0 ? Utils::String::toUpper(Utils::String::trim(parts[0])) : "";
		out.number = parts.size() > 1 ? atoi(Utils::String::trim(parts[1]).c_str()) : -1;
	}
	else
		out.kind = ProtocolKind::Unknown;

	return out;
}

Verb verbOf(const std::string& cmd)
{
	const bool restore = cmd.find("cloud_restore") != std::string::npos;
	const bool backup  = cmd.find("cloud_backup")  != std::string::npos || cmd.find("backuptool") != std::string::npos;
	if (restore && backup) return Verb::Sync;
	if (restore) return Verb::Restore;
	if (backup)  return Verb::Backup;
	return Verb::Other;
}

std::string chooseThatFits(const std::vector<std::string>& candidates, float width,
	const std::function<float(const std::string&)>& measure)
{
	std::string shown;

	for (auto& candidate : candidates)
	{
		shown = candidate;
		if (width <= 0.0f || !measure || measure(candidate) <= width)
			break;
	}

	return shown;
}

const std::vector<RcloneUnit>& rcloneUnits()
{
	static const std::vector<RcloneUnit> units = {
		{ "TiB", "TB",  1024.0 * 1024 * 1024 * 1024 },
		{ "GiB", "GB",  1024.0 * 1024 * 1024 },
		{ "MiB", "MB",  1024.0 * 1024 },
		{ "KiB", "KB",  1024.0 },
		{ "Ti",  " TB", 1024.0 * 1024 * 1024 * 1024 },
		{ "Gi",  " GB", 1024.0 * 1024 * 1024 },
		{ "Mi",  " MB", 1024.0 * 1024 },
		{ "Ki",  " KB", 1024.0 },
		{ "B",   "B",   1.0 },
	};
	return units;
}

long parseBytes(const std::string& field)
{
	const std::string t = Utils::String::trim(field);
	char* end = nullptr;
	const double v = strtod(t.c_str(), &end);
	if (end == t.c_str() || !std::isfinite(v) || v < 0)
		return -1;
	const std::string unit = Utils::String::trim(std::string(end));
	for (const auto& u : rcloneUnits())
		if (unit == u.rclone)
			return (long) (v * u.bytes + 0.5);
	return -1;
}

std::string sizeLabel(unsigned long bytes)
{
	char buf[32];
	const unsigned long kb = (bytes + 1023UL) / 1024UL;
	const double mb = bytes / (1024.0 * 1024.0);
	if (kb < 1024UL)
		snprintf(buf, sizeof(buf), "%lu KB", kb);
	else if (mb < 1023.95)
		snprintf(buf, sizeof(buf), "%.1f MB", mb);
	else
		snprintf(buf, sizeof(buf), "%.2f GB", bytes / (1024.0 * 1024.0 * 1024.0));
	return buf;
}

std::string roundSizes(const std::string& f)
{
	std::string out;
	size_t i = 0;
	while (i < f.size())
	{
		// a number starts at a digit that does not continue a token ("3m2s")
		const bool starts = isdigit((unsigned char) f[i]) && (i == 0 || !(isalnum((unsigned char) f[i - 1]) || f[i - 1] == '.'));
		if (!starts)
		{
			out += f[i++];
			continue;
		}
		size_t j = i;
		while (j < f.size() && (isdigit((unsigned char) f[j]) || f[j] == '.'))
			j++;
		size_t k = j;
		while (k < f.size() && f[k] == ' ')
			k++;
		const RcloneUnit* unit = nullptr;
		for (const auto& u : rcloneUnits())
		{
			const std::string spelt = u.rclone;
			if (f.compare(k, spelt.size(), spelt) == 0 && (k + spelt.size() == f.size() || !isalpha((unsigned char) f[k + spelt.size()])))
			{
				unit = &u;
				break;
			}
		}
		const size_t end = unit == nullptr ? j : k + std::string(unit->rclone).size();
		const long bytes = unit == nullptr ? -1 : parseBytes(f.substr(i, end - i));
		out += bytes < 0 ? f.substr(i, end - i) : sizeLabel((unsigned long) bytes);
		i = end;
	}
	return out;
}

namespace
{
	// "<a> / <b>[, <pct>%[, <speed>]]" -- the body of a Transferred: or
	// Checks: line once its label is gone. False when there is no pair.
	bool readPair(const std::string& body, std::string& a, std::string& b, int& percent)
	{
		const auto fields = Utils::String::split(body, ',', true);
		if (fields.empty())
			return false;
		const std::string pair = Utils::String::trim(fields[0]);
		const auto slash = pair.find(" / ");
		if (slash == std::string::npos)
			return false;
		a = Utils::String::trim(pair.substr(0, slash));
		b = Utils::String::trim(pair.substr(slash + 3));
		percent = -1;
		for (size_t i = 1; i < fields.size(); i++)
		{
			const std::string t = Utils::String::trim(fields[i]);
			if (t.empty() || t.back() != '%')
				continue;
			const std::string digits = t.substr(0, t.size() - 1);
			if (!digits.empty() && digits.find_first_not_of("0123456789") == std::string::npos)
			{
				const int value = atoi(digits.c_str());
				if (value >= 0 && value <= 100)
					percent = value;
			}
			break;
		}
		return !a.empty() && !b.empty();
	}

	bool allDigits(const std::string& s)
	{
		return !s.empty() && s.find_first_not_of("0123456789") == std::string::npos;
	}
}

LiveLine liveLine(const std::string& clean)
{
	LiveLine out;
	static const std::string XFER = "Transferred:";
	static const std::string CHECKS = "Checks:";

	const size_t xfer = clean.rfind(XFER);
	const size_t checks = clean.rfind(CHECKS);

	if (xfer != std::string::npos && (checks == std::string::npos || xfer > checks))
	{
		std::string body = Utils::String::trim(clean.substr(xfer + XFER.size()));
		// speed and ETA are for a terminal; the card has a bar
		const auto eta = body.find(", ETA ");
		if (eta != std::string::npos)
			body = body.substr(0, eta);
		std::string a, b;
		int percent = -1;
		if (!readPair(body, a, b, percent))
			return out;
		if (allDigits(a) && allDigits(b))
		{
			// the count line has no unit: "0 / 3, 0%"
			out.kind = LiveLine::Kind::Files;
			out.sent = atol(a.c_str());
			out.total = atol(b.c_str());
			return out;
		}
		const long sent = parseBytes(a);
		const long total = parseBytes(b);
		if (sent < 0 || total < 0)
			return out;
		out.kind = LiveLine::Kind::Bytes;
		out.sent = sent;
		out.total = total;
		out.percent = percent;
		return out;
	}

	if (checks != std::string::npos)
	{
		// rclone's check counter -- "Checks: 12 / 70, 17%, Listed 313" --
		// is a comparison, not a transfer, and its percentage is not the
		// bar's: drawn as one it reads as seventy uploads.
		std::string a, b;
		int percent = -1;
		if (!readPair(Utils::String::trim(clean.substr(checks + CHECKS.size())), a, b, percent))
			return out;
		if (!allDigits(a) || !allDigits(b))
			return out;
		out.kind = LiveLine::Kind::Checks;
		out.sent = atol(a.c_str());
		out.total = atol(b.c_str());
		return out;
	}

	// A per-file line (" * name: 32% /292.969Ki, 95.996Ki/s, 2s") is
	// written for a log; the card's title already says what is moving.
	if (clean.rfind("* ", 0) == 0)
		return out;

	// Anything else carries progress if it has a percentage or an "x / y"
	// count, and is shown as it came. rclone's headers, the elapsed time
	// and the scripts' banners have neither, and stay off the card.
	if (clean.find('%') != std::string::npos || clean.find(" / ") != std::string::npos)
	{
		out.kind = LiveLine::Kind::Other;
		out.text = clean;
	}
	return out;
}

} // namespace CloudText
