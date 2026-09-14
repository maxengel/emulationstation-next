#include "CheevosIndex.h"

#include "utils/StringUtil.h"

bool CheevosIndex::hasId(const std::string& cheevosId)
{
	const std::string id = Utils::String::trim(cheevosId);
	if (id.empty() || id.size() > 12)
		return false;
	for (char c : id)
		if (c < '0' || c > '9')
			return false;
	return Utils::String::toInteger(id) > 0;
}

CheevosIndex::Take CheevosIndex::take(bool forceAllGames, const std::string& cheevosHash, const std::string& cheevosId)
{
	if (forceAllGames || Utils::String::trim(cheevosHash).empty())
		return Take::Hash;
	if (hasId(cheevosId))
		return Take::None;
	return Take::Lookup;
}
