/*
 * Part of VCMI / New Horizons.
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "NewHorizonsAerialSupremacy.h"
#include "NewHorizonsParalyzingGaze.h"
#include "Unit.h"
#include "BattleAttackInfo.h"
#include "../mapObjects/CGHeroInstance.h"
#include "NewHorizonsCombatSkills.h"
#include "CBattleInfoCallback.h"
#include "../CCreatureHandler.h"
#include "../GameLibrary.h"
#include "BattleInfo.h"
#include "../CStack.h"
#include "../networkPacks/SetStackEffect.h"
#include "../entities/hero/NewHorizonsParalyzingGaze.h"
#include "../bonuses/BonusList.h"
#include "../bonuses/BonusSelector.h"
namespace newHorizonsParalyzingGaze
{
bool isMarker(const Bonus & bonus) noexcept
{
	return bonus.statusIdentity == "paralyzingGaze";
}
bool validateMarker(const Bonus & bonus)
{
	if(!isMarker(bonus))
		return false;
	if(bonus.type != BonusType::NONE || bonus.source != BonusSource::HERO_SPECIAL
		|| !bonus.sid.as<HeroTypeID>().hasValue() || !bonus.subtype.as<CreatureID>().hasValue()
		|| bonus.duration != BonusDuration::ONE_BATTLE || bonus.val != 0 || bonus.turnsRemain != 0
		|| !bonus.stacking.empty() || bonus.effectRange != BonusLimitEffect::NO_LIMIT
		|| bonus.valType != BonusValueType::ADDITIVE_VALUE || bonus.propagator || bonus.updater
		|| bonus.propagationUpdater || bonus.limiter || bonus.parameters || !bonus.appliedByEnemy
		|| bonus.statusTags != std::vector<BonusStatusTag>{BonusStatusTag::DEBUFF, BonusStatusTag::NON_TRANSFERABLE})
		throw std::runtime_error("Invalid pending Paralyzing Gaze token");
	return true;
}
void validateLoadedMarker(const Bonus & bonus)
{
	if(validateMarker(bonus) && (!LIBRARY
		|| bonus.sid != BonusSourceID(HeroTypeID(HeroTypeID::decode("core:ajit")))
		|| bonus.subtype != BonusSubtypeID(CreatureID(CreatureID::decode("core:beholder")))))
		throw std::runtime_error("Pending Gaze requires canonical specialty provenance");
}
bool captured(const IBattleInfo & battle)
{
	return newHorizonsHeroes::paralyzingGazeEnabled(battle.getSideHero(BattleSide::ATTACKER))
		|| newHorizonsHeroes::paralyzingGazeEnabled(battle.getSideHero(BattleSide::DEFENDER));
}
void validateApplication(const IBattleInfo & battle, uint32_t target, const Bonus & bonus)
{
	if(!validateMarker(bonus))
		return;
	validateLoadedMarker(bonus);
	const auto units = battle.getUnitsIf([target](const battle::Unit * unit)
		{ return unit && unit->unitId() == target; });
	if(!captured(battle) || units.size() != 1 || !units.front()->alive()
		|| units.front()->isGhost() || units.front()->isTimeStopped())
		throw std::runtime_error("Gaze application requires supported captured battle and available recipient");
}
Bonus marker()
{
	Bonus result(BonusDuration::ONE_BATTLE, BonusType::NONE, BonusSource::HERO_SPECIAL, 0,
		BonusSourceID(HeroTypeID(HeroTypeID::decode("core:ajit"))),
		BonusSubtypeID(CreatureID(CreatureID::decode("core:beholder"))));
	result.statusIdentity = "paralyzingGaze";
	result.statusTags = {BonusStatusTag::DEBUFF, BonusStatusTag::NON_TRANSFERABLE};
	result.appliedByEnemy = true;
	result.description = MetaString::createFromRawString("Paralyzing Gaze: the next otherwise eligible ordinary retaliation is suppressed.");
	return result;
}
std::optional<Bonus> pending(const battle::Unit & target)
{
	// Query only inert tokens; unrelated numeric combat bonuses stay untouched.
	const auto tokens = target.getBonuses(Selector::type()(BonusType::NONE), "paralyzingGazeTokens");
	std::optional<Bonus> result;
	for(const auto & token : *tokens)
		if(token && validateMarker(*token))
		{
			if(result)
				throw std::runtime_error("Duplicate pending Paralyzing Gaze tokens");
			validateLoadedMarker(*token);
			result = *token;
		}
	return result;
}
bool qualifiesAttack(const CBattleInfoCallback & battle, const battle::Unit * source,
	const battle::Unit * primary, bool ranged, bool physical, bool secondary)
{
	if(!source || !primary || !ranged || !physical || secondary || !source->creatureId().hasValue())
		return false;
	const auto key = source->unitType()->getJsonKey();
	if((key != "core:beholder" && key != "core:evilEye")
		|| !source->alive() || !primary->alive() || source->isGhost() || primary->isGhost()
		|| source->isTimeStopped() || primary->isTimeStopped()
		|| battle.battleGetOwner(source) == battle.battleGetOwner(primary)
		|| !battle.battleMatchActionController(source, primary))
		return false;
	const auto side = battle.playerToSide(battle.battleGetOwner(source));
	return (side == BattleSide::ATTACKER || side == BattleSide::DEFENDER)
		&& newHorizonsHeroes::paralyzingGazeEnabled(battle.battleGetFightingHero(side));
}
bool canConsume(const battle::Unit & retaliator)
{
	return retaliator.alive() && !retaliator.isGhost() && !retaliator.isTimeStopped()
		&& pending(retaliator).has_value() && retaliator.ableToRetaliate();
}
}

void BattleInfo::validateLoadedGazeTokens() const
{
	bool found = false;
	for(const auto & stack : stacks)
		if(stack)
		{
			unsigned count = 0;
			for(const auto & bonus : stack->getExportedBonusList())
				if(bonus && newHorizonsParalyzingGaze::validateMarker(*bonus))
				{
					newHorizonsParalyzingGaze::validateLoadedMarker(*bonus);
					if(++count > 1) throw std::runtime_error("Duplicate saved pending Gaze token");
					found = true;
				}
		}
	if(found && !newHorizonsParalyzingGaze::captured(*this))
		throw std::runtime_error("Pending Gaze requires its captured specialty policy");
}

void SetStackEffect::validateGazeApplication(const IBattleInfo & battle) const
{
	for(const auto * changes : {&toAdd, &toUpdate})
		for(const auto & [target, bonuses] : *changes)
			for(const auto & bonus : bonuses)
				newHorizonsParalyzingGaze::validateApplication(battle, target, bonus);
	for(const auto & [target, bonuses] : toRemove)
		for(const auto & bonus : bonuses)
			newHorizonsParalyzingGaze::validateLoadedMarker(bonus);
}

bool CBattleInfoCallback::battleCanUseGazeDefiant(const battle::Unit * opponent,
	const battle::Unit * retaliator, bool ranged,
	const newHorizonsAerialSupremacy::Excursion * aerial) const
{
	const auto * current = getBattle();
	if(!current || !opponent || !retaliator || !opponent->alive()
		|| !newHorizonsParalyzingGaze::canConsume(*retaliator)
		|| !heroCommands::isCanonicalRules(current->getHeroCommandRules())
		|| !newHorizonsCombatSkills::isOrdinaryCreatureAttacker(retaliator))
		return false;
	const auto side = playerToSide(battleGetActionController(retaliator));
	const auto enemy = playerToSide(battleGetActionController(opponent));
	if(side == BattleSide::NONE || enemy == BattleSide::NONE || side == enemy
		|| !current->getArmorerDefiantState(side).availableAt(current->getRound()))
		return false;
	const auto * hero = battleGetFightingHero(side);
	if(!hero || !hero->hasActivePerk("new-horizons:armorer", "new-horizons:armorer.defiant"))
		return false;
	if(aerial)
	{
		if(ranged || aerial->actor != retaliator->unitId() || aerial->opponent != opponent->unitId())
			return false;
		return newHorizonsAerialSupremacy::matches(*this, *aerial);
	}
	if(ranged)
		return retaliator->hasBonusOfType(BonusType::RANGED_RETALIATION)
			&& !opponent->hasBonusOfType(BonusType::BLOCKS_RANGED_RETALIATION)
			&& battleCanShoot(retaliator, opponent->getPosition());
	if(opponent->isInvincible() || opponent->hasBonusOfType(BonusType::BLOCKS_RETALIATION)
		|| !isMeleeAttackPossible(opponent, retaliator) || isLongWeaponAttack(opponent, retaliator))
		return false;
	static const auto firstStrike = Selector::typeSubtype(BonusType::FIRST_STRIKE, BonusCustomSubtype::damageTypeAll)
		.Or(Selector::typeSubtype(BonusType::FIRST_STRIKE, BonusCustomSubtype::damageTypeMelee));
	// Geometry adapter only: neither incoming damage class nor historical hit
	// properties are asserted or used to classify this already physical token.
	const BattleAttackInfo geometry(opponent, retaliator, 0, false);
	return !battleShroudDeniesRetaliation(geometry) || retaliator->hasBonus(firstStrike);
}
