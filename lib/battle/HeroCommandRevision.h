/*
 * HeroCommandRevision.h, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once
#include "../json/JsonNode.h"
#include <algorithm>
#include <cstdint>
#include <optional>
#include <vector>

struct HeroOrderState;
struct Bonus;
class CGHeroInstance;
enum class HeroCommand : int8_t;
namespace battle { class Unit; }
namespace heroCommands
{
struct DLL_LINKAGE CrownRevisedOrderPotency
{
	int32_t feintReductionPercent = 0;
	int32_t vanguardStatBonus = 0;
	int32_t vanguardInitiativePercent = 0;
	bool operator==(const CrownRevisedOrderPotency &) const = default;
	template<typename Handler> void serialize(Handler & h)
	{
		h & feintReductionPercent;
		h & vanguardStatBonus;
		h & vanguardInitiativePercent;
	}
};
/// Frozen issuance potency and lifetime; native stack bonuses own paused timers.
struct DLL_LINKAGE RevisedOrderSnapshot
{
	std::optional<CrownRevisedOrderPotency> crown;
	bool potencyFinalized = false;
	int32_t expiresAfterRound = 0;
	std::vector<uint32_t> recipientUnitIds;
	int32_t feintCap = 60;
	int32_t vanguardStatCap = 3;
	int32_t vanguardInitiativeCap = 18;
	int32_t feintCompanionPercent = 0;
	int32_t feintReductionPercent = 0;
	int32_t vanguardStatBonus = 0;
	int32_t vanguardInitiativePercent = 0;
	bool operator==(const RevisedOrderSnapshot &) const = default;
	bool contains(uint32_t id) const
	{
		return std::binary_search(recipientUnitIds.begin(), recipientUnitIds.end(), id);
	}
	void validate(HeroCommand command, int32_t issuedRound) const;
	template<typename Handler> void serialize(Handler & h)
	{
		h & crown;
		h & potencyFinalized;
		h & expiresAfterRound;
		h & recipientUnitIds;
		h & feintCap;
		h & vanguardStatCap;
		h & vanguardInitiativeCap;
		h & feintCompanionPercent;
		h & feintReductionPercent;
		h & vanguardStatBonus;
		h & vanguardInitiativePercent;
	}
};
DLL_LINKAGE void validateRevisedRules(const JsonNode & rules);
DLL_LINKAGE void validateRevisedRulesSerialization(const JsonNode & rules, bool supported);
DLL_LINKAGE RevisedOrderSnapshot nominalRevisedOrderSnapshot(HeroCommand command,
	int32_t issuedRound, int attack, int defense, int64_t leadership, const JsonNode & definition);
struct DLL_LINKAGE ActiveVanguardBonuses
{
	int statBonus = 0;
	int initiativePercent = 0;
	int roundsRemaining = 0;
};
DLL_LINKAGE RevisedOrderSnapshot scaledRevisedOrderSnapshot(HeroCommand command,
	int32_t issuedRound, int attack, int defense, int64_t leadership, const JsonNode & definition,
	int attackEfficiencyPercent, int defenseEfficiencyPercent, int leadershipEfficiencyPercent);
/// Called once after the existing Luck receipt is assigned, before state installation.
DLL_LINKAGE int revisedPerkBonus(const JsonNode & rules, const CGHeroInstance * hero, HeroCommand command);
DLL_LINKAGE JsonNode crownRevisedOrderDefinition(const JsonNode & definition, HeroCommand command);
DLL_LINKAGE int revisedFeintReductionFor(const HeroOrderState & order, uint32_t unitId);
DLL_LINKAGE void finalizeRevisedOrderSnapshot(HeroOrderState & order, const JsonNode & magicRules);
DLL_LINKAGE bool isVanguardBonus(const Bonus * bonus);
/// Native unit timers remain authoritative when Time Stop preserves an older grant.
DLL_LINKAGE std::optional<ActiveVanguardBonuses> activeVanguardBonuses(const battle::Unit * unit);
DLL_LINKAGE bool isVanguardCreature(const battle::Unit * unit);
}
