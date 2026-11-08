/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../../Global.h"
#include "BattleSide.h"
#include <cstdint>
#include <stdexcept>
class JsonNode;
class CGHeroInstance;
class CBattleInfoCallback;
namespace battle { class Unit; }
namespace newHorizonsCommand
{
struct DLL_LINKAGE ResourceState
{
	bool initialized = false;
	int32_t maximum = 0;
	int32_t remaining = 0;
	bool operator==(const ResourceState &) const = default;
	void validate() const;
	bool spend();
	template<typename Handler> void serialize(Handler & h)
	{
		if(h.saving) validate();
		h & initialized; h & maximum; h & remaining;
		if(!h.saving) validate();
	}
};
DLL_LINKAGE bool usesRules(const JsonNode & rules);
DLL_LINKAGE bool usesPerkRules(const JsonNode & rules);
DLL_LINKAGE bool usesPerkRules(const CGHeroInstance * hero);
DLL_LINKAGE void validateRulesSerialization(const JsonNode & rules, bool supported);
DLL_LINKAGE int maximum(int level, int rank, bool battlePlan, int equipmentBonus);
DLL_LINKAGE ResourceState initialState(const CGHeroInstance * hero);
DLL_LINKAGE bool available(const CBattleInfoCallback & battle, BattleSide side);
DLL_LINKAGE int heroicSpiritEfficiencyForMorale(int morale);
DLL_LINKAGE int heroicSpiritEfficiency(const CGHeroInstance * hero);
DLL_LINKAGE int64_t effectiveLeadership(const CGHeroInstance & hero, int64_t ordinary);
DLL_LINKAGE bool beneficiary(const CBattleInfoCallback & battle, const battle::Unit * unit);
DLL_LINKAGE bool unbrokenRanks(const CBattleInfoCallback & battle, const battle::Unit * unit);
DLL_LINKAGE int64_t mitigateIncomingDamage(const CBattleInfoCallback & battle, const battle::Unit * unit, int64_t ordinary);
}
