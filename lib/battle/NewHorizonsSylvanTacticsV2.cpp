/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NewHorizonsSylvanTacticsV2.h"
#include "NewHorizonsSylvanTactics.h"
#include "CBattleInfoCallback.h"
#include "BattleAttackInfo.h"
#include "Unit.h"
#include "CUnitState.h"
#include "../CCreatureHandler.h"
#include "../mapObjects/CGHeroInstance.h"
#include "../combatScripts/IDamageCalculatorScript.h"
#include "../bonuses/BonusList.h"
#include "../bonuses/BonusParameters.h"
#include <array>
namespace newHorizonsSylvanTacticsV2
{
bool enabled(const CGHeroInstance * hero)
{
	return hero && newHorizonsHeroes::usesSylvanTacticsV2(hero->getPerkState().rules);
}
bool family(const battle::Unit * unit, Family line)
{
	if(!unit) return false;
	constexpr std::array<std::array<std::string_view, 2>, 7> lines{{
		{{"core:centaur", "core:centaurCaptain"}}, {{"core:dwarf", "core:battleDwarf"}},
		{{"core:woodElf", "core:grandElf"}}, {{"core:pegasus", "core:silverPegasus"}},
		{{"core:dendroidGuard", "core:dendroidSoldier"}}, {{"core:unicorn", "core:warUnicorn"}},
		{{"core:greenDragon", "core:goldDragon"}}}};
	const auto key = CreatureID::encode(unit->creatureId().getNum());
	const auto & pair = lines.at(static_cast<size_t>(line));
	return key == pair[0] || key == pair[1];
}
const CGHeroInstance * commandingHero(const CBattleInfoCallback & battle, const battle::Unit * unit)
{
	if(!unit) return nullptr;
	const auto side = battle.playerToSide(battle.battleGetOwner(unit));
	return side == BattleSide::ATTACKER || side == BattleSide::DEFENDER ? battle.battleGetFightingHero(side) : nullptr;
}
bool hasPerk(const CGHeroInstance * hero, std::string_view perk)
{
	return enabled(hero) && hero->hasActivePerk("new-horizons:sylvanLuck", "new-horizons:sylvanLuck." + std::string(perk));
}
int perkValue(const CGHeroInstance * hero, std::string_view perk, std::string_view field)
{
	if(!hasPerk(hero, perk)) return 0;
	// Captured rows were validated at acquisition/load. Do not parse or validate
	// the entire perk registry during damage, immunity or AI queries.
	const auto id = "new-horizons:sylvanLuck." + std::string(perk);
	for(const auto & row : hero->getPerkState().rules["skills"]["new-horizons:sylvanLuck"]["perks"].Vector())
		if(row["id"].String() == id) return static_cast<int>(row["effect"]["parameters"][std::string(field)].Integer());
	return 0;
}
bool huntingPackConfigured(const CBattleInfoCallback & battle, const battle::Unit * attacker)
{
	return newHorizonsSylvanTactics::ordinary(attacker) && family(attacker, Family::Centaur)
		&& hasPerk(commandingHero(battle, attacker), "elvenPrecision");
}
int huntingPackBonus(const CBattleInfoCallback & battle, const battle::Unit * attacker, const battle::Unit * target, BattleHex targetPosition)
{
	if(!huntingPackConfigured(battle, attacker) || !target || battle.battleGetOwner(attacker) == battle.battleGetOwner(target)) return 0;
	if(targetPosition == BattleHex::INVALID) targetPosition = target->getPosition();
	for(const auto & hex : target->getSurroundingHexes(targetPosition))
	{
		const auto * friendUnit = battle.battleGetUnitByPos(hex, true);
		if(friendUnit && friendUnit->unitId() != attacker->unitId() && newHorizonsSylvanTactics::ordinary(friendUnit)
			&& battle.battleGetOwner(friendUnit) == battle.battleGetOwner(attacker)
			&& friendUnit->creatureId().toCreature()->getFactionID() == FactionID::RAMPART)
			return perkValue(commandingHero(battle, attacker), "elvenPrecision", "attackDamagePercent");
	}
	return 0;
}
int briarGripReduction(const CBattleInfoCallback & battle, const battle::Unit * attacker)
{
	if(!newHorizonsSylvanTactics::ordinary(attacker)) return 0;
	for(const auto & marker : *attacker->getBonusesOfType(BonusType::BIND_EFFECT))
	{
		if(marker->source != BonusSource::SPELL_EFFECT || marker->sid.toString() != "core:bind" || !marker->parameters) continue;
		const auto * source = battle.battleGetUnitByID(marker->parameters->toNumber());
		if(!newHorizonsSylvanTactics::ordinary(source) || !family(source, Family::Dendroid)) continue;
		if(battle.battleGetOwner(source) == battle.battleGetOwner(attacker)) continue;
		const auto adjacent = battle.battleAdjacentUnits(attacker);
		if(std::ranges::find(adjacent, source) == adjacent.end()) continue;
		const int reduction = perkValue(commandingHero(battle, source), "luckyRecovery", "attackDamageReductionPercent");
		if(reduction > 0) return reduction;
	}
	return 0;
}
int disarmingShotRetaliations(const CBattleInfoCallback & battle, const battle::Unit * attacker,
	const battle::Unit * victim, bool ranged, int64_t actualDamage)
{
	if(!ranged || actualDamage <= 0 || !newHorizonsSylvanTactics::ordinary(attacker)
		|| !family(attacker, Family::Elf) || !victim || battle.battleGetOwner(attacker) == battle.battleGetOwner(victim)) return 0;
	const auto * state = dynamic_cast<const battle::CUnitState *>(victim);
	if(!state || !state->counterAttacks.isLimited()) return 0;
	return std::min(state->counterAttacks.available(),
		perkValue(commandingHero(battle, attacker), "serendipity", "retaliationsConsumed"));
}
bool spareFriendlyBreath(const CBattleInfoCallback & battle, const battle::Unit * attacker, const battle::Unit * victim)
{
	return attacker && victim && family(attacker, Family::Dragon)
		&& battle.battleGetOwner(attacker) == battle.battleGetOwner(victim)
		&& hasPerk(commandingHero(battle, attacker), "wildChance");
}
int extraDamper(const CBattleInfoCallback & battle, const battle::Unit * provider)
{
	return family(provider, Family::Pegasus) && provider->hasBonusOfType(BonusType::CHANGES_SPELL_COST_FOR_ENEMY)
		? perkValue(commandingHero(battle, provider), "natureSProvidence", "enemySpellCostBonus") : 0;
}
void finalizeDamage(const CBattleInfoCallback & battle, const BattleAttackInfo & info, DamageAttackInfo & payload)
{
	if(!newHorizonsSylvanTactics::ordinary(info.attacker)) return;
	if(const int bonus = huntingPackBonus(battle, info.attacker, info.defender, info.defenderPos)) payload.sylvanAttackMultipliers.push_back(100 + bonus);
	if(const int reduction = briarGripReduction(battle, info.attacker)) payload.sylvanAttackMultipliers.push_back(100 - reduction);
}
}
