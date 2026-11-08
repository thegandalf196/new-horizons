/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NewHorizonsCommandResources.h"
#include "CBattleInfoCallback.h"
#include "BattleInfo.h"
#include "BattleProxy.h"
#include "IBattleState.h"
#include "HeroCommandRoyalStandard.h"
#include "Unit.h"
#include "../mapObjects/CGHeroInstance.h"
#include "../json/JsonNode.h"
#include "../bonuses/BonusList.h"
namespace newHorizonsCommand
{
namespace
{
bool marker(const JsonNode & rules, const char * key)
{
	const auto & value = rules[key];
	return value.getType() == JsonNode::JsonType::DATA_INTEGER && value.Integer() == 1;
}
bool perk(const CGHeroInstance * hero, const char * id)
{
	return hero && hero->hasActivePerk("new-horizons:command", id);
}
}
void ResourceState::validate() const
{
	if(maximum < 0 || maximum > 10 || remaining < 0 || remaining > maximum
		|| (initialized && maximum < 3)
		|| (!initialized && (maximum != 0 || remaining != 0)))
		throw std::runtime_error("Invalid finite Command resource state");
}
bool ResourceState::spend()
{
	validate();
	if(!initialized || remaining == 0) return false;
	--remaining;
	return true;
}
bool usesRules(const JsonNode & rules) { return marker(rules, "commandResourcePolicyVersion"); }
bool usesPerkRules(const JsonNode & rules) { return marker(rules, "commandPolicyVersion"); }
bool usesPerkRules(const CGHeroInstance * hero)
{
	return hero && usesPerkRules(hero->getPerkState().rules);
}
void validateRulesSerialization(const JsonNode & rules, bool supported)
{
	if(!supported && (usesRules(rules) || usesPerkRules(rules)))
		throw std::runtime_error("Finite Command rules require native rules redesign format");
}
int maximum(int level, int rank, bool battlePlan, int equipmentBonus)
{
	return 3 + (level >= 10) + (level >= 20) + std::clamp(rank, 0, 3)
		+ static_cast<int>(battlePlan) + std::clamp(equipmentBonus, 0, 1);
}
ResourceState initialState(const CGHeroInstance * hero)
{
	if(!usesPerkRules(hero)) return {};
	const auto equipment = hero->getBonuses(CSelector([](const Bonus * bonus)
		{ return bonus->type == BonusType::COMMAND_CAPACITY && bonus->source == BonusSource::ARTIFACT; }));
	const int capacity = maximum(hero->level, hero->getPerkSkillRank("new-horizons:command"),
		perk(hero, "new-horizons:command.battlePlan"), equipment ? equipment->totalValue() : 0);
	return {true, capacity, capacity};
}
bool available(const CBattleInfoCallback & battle, BattleSide side)
{
	if(!battle.getBattle() || (side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)) return false;
	if(!usesRules(battle.getBattle()->getHeroCommandRules())) return false;
	if(battle.battleGetVisibleHeroPolicy(side).kind() != VisibleHeroPolicy::Knowledge::VISIBLE_HERO) return false;
	const auto & state = battle.getBattle()->getCommandResources(side);
	return state.initialized && state.remaining > 0;
}
int heroicSpiritEfficiencyForMorale(int morale)
{
	return std::clamp(morale, 0, 6) * 5;
}
int heroicSpiritEfficiency(const CGHeroInstance * hero)
{
	if(!usesPerkRules(hero) || hero->getPerkSkillRank("new-horizons:discipline") < MasteryLevel::EXPERT
		|| !hero->hasActivePerk("new-horizons:discipline", "new-horizons:discipline.heroicSpirit")) return 0;
	return heroicSpiritEfficiencyForMorale(hero->moraleVal());
}
int64_t effectiveLeadership(const CGHeroInstance & hero, int64_t ordinary)
{
	if(!usesPerkRules(&hero) || !perk(&hero, "new-horizons:command.seizeInitiative")) return ordinary;
	return ordinary + (ordinary * 20 + 50) / 100;
}

}

const newHorizonsCommand::ResourceState & BattleInfo::getCommandResources(BattleSide side) const
{ return sides.at(side).commandResources; }
void BattleInfo::setCommandResources(BattleSide side, const newHorizonsCommand::ResourceState & state)
{
	state.validate();
	const auto & previous = sides.at(side).commandResources;
	if(previous.initialized && (!state.initialized || state.maximum != previous.maximum
		|| state.remaining > previous.remaining))
		throw std::runtime_error("Finite Command capacity cannot refill or change during combat");
	sides.at(side).commandResources = state;
}
uint8_t BattleInfo::getElementalAttunementMask(BattleSide side) const
{ return sides.at(side).elementalAttunementMask; }
void BattleInfo::setElementalAttunementMask(BattleSide side, uint8_t mask)
{
	const auto previous = sides.at(side).elementalAttunementMask;
	if(mask > 15 || (previous & mask) != previous)
		throw std::runtime_error("Invalid elemental Attunement mask transition");
	if(mask == previous) return;
	sides.at(side).elementalAttunementMask = mask;
	nodeHasChanged();
}
const newHorizonsCommand::ResourceState & BattleProxy::getCommandResources(BattleSide side) const
{ return subject->getBattle()->getCommandResources(side); }
uint8_t BattleProxy::getElementalAttunementMask(BattleSide side) const
{ return subject->getBattle()->getElementalAttunementMask(side); }

const newHorizonsElementalAttunement::Profile & BattleInfo::getElementalAttunementProfile(BattleSide side) const
{ return sides.at(side).elementalAttunementProfile; }
const newHorizonsElementalAttunement::Profile & BattleProxy::getElementalAttunementProfile(BattleSide side) const
{ return subject->getBattle()->getElementalAttunementProfile(side); }
