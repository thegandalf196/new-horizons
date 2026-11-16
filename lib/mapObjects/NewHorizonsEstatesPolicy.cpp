/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NewHorizonsEstatesPolicy.h"
#include "CGHeroInstance.h"
#include "../CPlayerState.h"
#include "../json/JsonNode.h"
#include "../texts/MetaString.h"
#include <limits>
#include <map>
#include <stdexcept>
#include <string_view>

namespace newHorizonsEstates
{
bool usesRevisedPolicy(const JsonNode & rules)
{
	return rules["estatesPolicyVersion"].getType() == JsonNode::JsonType::DATA_INTEGER
		&& rules["estatesPolicyVersion"].Integer() == 1;
}

void validateRevisedPolicy(const JsonNode & rules)
{
	if(!rules.Struct().contains("estatesPolicyVersion"))
		return;
	if(!usesRevisedPolicy(rules))
		throw std::runtime_error("Unsupported Estates policy version");
	static const std::map<std::string, std::string> expected = {
		{"taxCollector", "basic"}, {"supervisor", "basic"}, {"mercenaryCompany", "basic"},
		{"merchantPrince", "basic"}, {"stewardship", "advanced"}, {"conscription", "advanced"},
		{"architect", "advanced"}, {"hoarder", "advanced"}, {"decentralization", "expert"},
		{"alchemist", "expert"}};
	std::map<std::string, std::string> actual;
	for(const auto & perk : rules["skills"]["new-horizons:estates"]["perks"].Vector())
	{
		const auto & id = perk["id"].String();
		constexpr std::string_view prefix = "new-horizons:estates.";
		if(!id.starts_with(prefix) || !actual.emplace(id.substr(prefix.size()), perk["requires"].String()).second)
			throw std::runtime_error("Invalid revised Estates perk identity");
	}
	if(actual != expected)
		throw std::runtime_error("Invalid revised Estates catalogue or tier");
}

bool hasRevisedPerk(const CGHeroInstance * hero, const char * perk)
{
	return hero && hero->getOwner().isValidPlayer()
		&& usesRevisedPolicy(hero->getPerkState().rules)
		&& hero->hasActivePerk("new-horizons:estates", perk);
}

void validateRevisedSerialization(const JsonNode & rules, bool supported)
{
	if(rules.isStruct() && rules.Struct().contains("estatesPolicyVersion") && !supported)
		throw std::runtime_error("Revised Estates requires its supported save format");
}

bool anyRevisedHolder(const PlayerState & player, const char * perk)
{
	for(const auto * hero : player.getHeroes())
		if(hero->getOwner() == player.color && hasRevisedPerk(hero, perk))
			return true;
	return false;
}

ResourceSet visitReward(const ResourceSet & base, const CGHeroInstance * hero)
{
	ResourceSet result = base;
	if(!hasRevisedPerk(hero, "new-horizons:estates.taxCollector"))
		return result;
	for(size_t resource = 0; resource < base.size(); ++resource)
	{
		if(base[resource] <= 0)
			continue;
		const int64_t amount = static_cast<int64_t>(base[resource]) + base[resource] / 2;
		if(amount > std::numeric_limits<TResource>::max())
			throw std::overflow_error("Tax Collector reward overflow");
		result[resource] = static_cast<TResource>(amount);
	}
	return result;
}

void appendVisitRewardBonus(MetaString & text, const ResourceSet & base, const ResourceSet & effective)
{
	bool heading = false;
	for(size_t resource = 0; resource < base.size(); ++resource)
	{
		if(effective[resource] <= base[resource])
			continue;
		if(!heading)
		{
			text.appendEOL();
			text.appendTextID("new-horizons.economy.taxCollector.bonus");
			heading = true;
		}
		text.appendEOL();
		text.appendRawString("%d %s");
		text.replaceNumber(static_cast<int64_t>(effective[resource]) - base[resource]);
		text.replaceName(GameResID(resource));
	}
}

uint32_t mineBaseProduction(uint32_t base, GameResID resource, const PlayerState * owner)
{
	if(!owner || (resource != GameResID::WOOD && resource != GameResID::ORE)
		|| !anyRevisedHolder(*owner, "new-horizons:estates.supervisor"))
		return base;
	if(base >= static_cast<uint32_t>(std::numeric_limits<TResource>::max()))
		throw std::overflow_error("Supervisor production overflow");
	return base + 1;
}

int marketplaceBonus(const PlayerState & player)
{
	return anyRevisedHolder(player, "new-horizons:estates.merchantPrince") ? 2 : 0;
}

size_t capitolLimit(const PlayerState & player)
{
	return anyRevisedHolder(player, "new-horizons:estates.decentralization") ? 2 : 1;
}

ResourceSet weeklyAlchemistIncome(const PlayerState & player)
{
	ResourceSet result;
	if(anyRevisedHolder(player, "new-horizons:estates.alchemist"))
		for(const auto resource : {EGameResID::MERCURY, EGameResID::SULFUR, EGameResID::CRYSTAL, EGameResID::GEMS})
			result[resource] = 3;
	return result;
}
}
