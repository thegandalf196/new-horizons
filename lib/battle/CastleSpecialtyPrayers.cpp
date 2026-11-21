/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "CastleSpecialtyPrayers.h"
#include "BattleAction.h"
#include "CBattleInfoCallback.h"
#include "ConsecratedArms.h"
#include "ConsecratedArmsOrder.h"
#include "IBattleState.h"
#include "Unit.h"
#include "../bonuses/BonusList.h"
#include "../entities/hero/NewHorizonsCastleSpecialties.h"
#include "../mapObjects/CGHeroInstance.h"
#include "../spells/NewHorizonsMagic.h"
#include "../spells/NewHorizonsSpellAvailability.h"

namespace castleSpecialtyPrayers
{
namespace
{
bool lightSpell(const JsonNode & rules, SpellID spell)
{
	const SpellSchool light(SpellSchool::decode("new-horizons:light"));
	if(!light.hasValue() || !spell.hasValue() || !newHorizonsMagic::spellAllowedByHeroRoster(rules, spell))
		return false;
	const auto schools = newHorizonsMagic::spellSchools(rules, spell);
	return std::ranges::find(schools, light) != schools.end();
}
}

bool sermonCast(const CBattleInfoCallback & battle, const BattleAction & action)
{
	if(action.actionType != EActionType::HERO_SPELL || !battle.getBattle()
		|| (action.side != BattleSide::ATTACKER && action.side != BattleSide::DEFENDER))
		return false;
	const auto * hero = battle.battleGetFightingHero(action.side);
	if(!newHorizonsHeroes::sermonRule(hero))
		return false;
	const auto policy = consecratedArms::capturePolicy(hero);
	return policy.rank && policy.chosen && lightSpell(battle.getBattle()->getMagicRules(), action.spell);
}

std::vector<uint32_t> sermonRecipients(const CBattleInfoCallback & battle, BattleSide side)
{
	std::vector<uint32_t> candidates;
	for(const auto * unit : battle.battleGetUnitsIf([side](const battle::Unit * unit) { return unit->unitSide() == side; }))
		candidates.push_back(unit->unitId());
	return consecratedArms::prayerHolders(battle, side, candidates);
}

std::optional<Bonus> benedictionMarker(const CGHeroInstance * hero)
{
	if(!newHorizonsHeroes::benedictionOfArmsRule(hero))
		return std::nullopt;
	Bonus marker(BonusDuration::ONE_BATTLE, BonusType::NONE, BonusSource::HERO_SPECIAL, 0,
		BonusSourceID(hero->getHeroTypeID()));
	marker.stacking = BENEDICTION_STACKING;
	marker.description.appendRawString("Benediction of Arms");
	return marker;
}

bool isBenedictionMarker(const Bonus * bonus)
{
	return bonus && bonus->type == BonusType::NONE && bonus->source == BonusSource::HERO_SPECIAL
		&& bonus->stacking == BENEDICTION_STACKING;
}

std::vector<uint32_t> benedictionHolders(const CBattleInfoCallback & battle, BattleSide side)
{
	std::vector<uint32_t> result;
	for(const auto * unit : battle.battleGetUnitsIf([side](const battle::Unit * unit) { return unit->unitSide() == side; }))
		if(unit->hasBonus(CSelector([](const Bonus * bonus) { return isBenedictionMarker(bonus); })))
			result.push_back(unit->unitId());
	std::sort(result.begin(), result.end());
	return result;
}

std::optional<uint32_t> benedictionRecipient(const CBattleInfoCallback & battle, BattleSide side)
{
	const auto holders = consecratedArms::prayerHolders(battle, side, benedictionHolders(battle, side));
	return holders.empty() ? std::nullopt : std::optional<uint32_t>(holders.front());
}
}
