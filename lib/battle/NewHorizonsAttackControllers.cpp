/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NewHorizonsAttackControllers.h"
#include "SpecialtyEventPolicy.h"
#include "NewHorizonsCombatSkills.h"
#include "BattleAttackInfo.h"
#include "CBattleInfoCallback.h"
#include "Unit.h"
#include "CUnitState.h"
#include "PhysicalAffliction.h"
#include "../CCreatureHandler.h"
#include "../bonuses/BonusList.h"
#include "../bonuses/BonusParameters.h"
#include "../mapObjects/CGHeroInstance.h"
#include "../spells/AbilityCaster.h"
#include "../spells/CSpell.h"
#include "../spells/ISpellMechanics.h"
namespace newHorizonsAttackControllers
{
namespace
{
constexpr auto CRIPPLE = "new-horizons:gerwulf:cripple";
enum class Family { Archer, Ogre };
bool family(const battle::Unit & unit, Family kind)
{
	static const auto archers = CreatureID(CreatureID::decode("core:archer"));
	static const auto ogres = CreatureID(CreatureID::decode("core:ogre"));
	const auto base = kind == Family::Ogre ? ogres : archers;
	return newHorizonsCombatSkills::isOrdinaryCreatureAttacker(&unit)
		&& (unit.creatureId() == base || (unit.unitType() && base.toCreature()->isMyDirectOrIndirectUpgrade(unit.unitType())));
}
int value(const CBattleInfoCallback & battle, const battle::Unit & unit, const char * hero, const char * key)
{
	return newHorizonsSpecialtyEvents::detail::coefficient(battle, unit, hero, key);
}
}
std::vector<Bonus> bloodlustBonusPlan()
{
	std::vector<Bonus> result;
	SpellID(SpellID::BLOODLUST).toSpell()->getEffects(result, BLOODLUST_LEVEL, false, BLOODLUST_DURATION);
	return result;
}
bool barrageKillQualifies(const CBattleInfoCallback & battle, const battle::Unit & source,
	const battle::Unit & victim, int64_t actualKilledCount)
{
	return actualKilledCount > 0 && newHorizonsCombatSkills::isOrdinaryCreatureAttacker(&victim)
		&& battle.battleGetOwner(&source) != battle.battleGetOwner(&victim);
}
bool barrageRepeatAllowed(const CBattleInfoCallback & battle, const battle::Unit * source,
	const battle::Unit * target, uint32_t capturedTargetId, BattleHex impact)
{
	if(!source || !target || target->unitId() != capturedTargetId || !impact.isValid()
		|| !source->alive() || !target->alive() || source->isGhost() || target->isGhost()
		|| source->isTimeStopped() || target->isTimeStopped() || !source->canMove() || !source->canShoot()
		|| !target->coversPos(impact) || battle.battleGetOwner(source) == battle.battleGetOwner(target))
		return false;
	const auto * state = dynamic_cast<const battle::CUnitState *>(source);
	return state && !state->armorerLastStandEndedActivation && battle.battleCanShoot(source, impact);
}
int barrageDamagePercent(const CBattleInfoCallback & battle, const battle::Unit & unit)
{
	return family(unit, Family::Archer) ? value(battle, unit, "core:valeska", "relentlessBarrageDamagePercent") : 0;
}
int barrageShots(const CBattleInfoCallback & battle, const battle::Unit & unit)
{
	if(!family(unit, Family::Archer)) return 0;
	int result = unit.getTotalAttacks(true);
	const auto visible = battle.battleGetVisibleHeroPolicy(battle.playerToSide(battle.battleGetOwner(&unit)));
	if(visible.kind() == VisibleHeroPolicy::Knowledge::VISIBLE_HERO)
		result += visible.legacyHero()->valOfBonuses(BonusType::HERO_GRANTS_ATTACKS, BonusSubtypeID(unit.creatureId()));
	return result;
}
int ballistaPhysicalDamagePercent(const CBattleInfoCallback & battle, const battle::Unit & unit)
{
	if(!unit.isBallista()) return 100;
	// The authored value is a bonus percentage; the complete multiplier is200.
	return 100 + value(battle, unit, "core:christian", "pointBlankShotDamageBonusPercent");
}
bool isCripple(const Bonus * bonus)
{
	if(!bonus || bonus->stacking != CRIPPLE) return false;
	const bool marker = bonus->type == BonusType::PHYSICAL_AFFLICTION;
	const auto metadata = marker ? physicalAfflictions::markerMetadata(*bonus) : std::nullopt;
	if(bonus->source != BonusSource::HERO_SPECIAL
		|| bonus->sid != BonusSourceID(HeroTypeID(HeroTypeID::decode("core:gerwulf")))
		|| (!marker && bonus->type != BonusType::STACKS_MOVEMENT_RANGE) || bonus->subtype != BonusSubtypeID()
		|| bonus->duration != BonusDuration::N_TURNS || bonus->turnsRemain < 0 || bonus->turnsRemain > 3
		|| bonus->val != (marker ? 0 : -3)
		|| bonus->valType != (marker ? BonusValueType::BASE_NUMBER : BonusValueType::ADDITIVE_VALUE)
		|| (marker && (!metadata || metadata->kind != "cripple"))
		|| bonus->statusIdentity != "cripple" || bonus->statusTags != std::vector<BonusStatusTag>{BonusStatusTag::DEBUFF, BonusStatusTag::NON_TRANSFERABLE}
		|| (!marker && bonus->parameters) || bonus->limiter || bonus->updater || bonus->propagationUpdater || bonus->propagator
		|| bonus->effectRange != BonusLimitEffect::NO_LIMIT || bonus->targetSourceType != BonusSource::OTHER
		|| bonus->appliedByEnemy || bonus->bonusOwner != PlayerColor::CANNOT_DETERMINE
		|| bonus->spellCasterOwner != PlayerColor::CANNOT_DETERMINE || !bonus->customIconPath.empty())
		throw std::runtime_error("Invalid Trapping Shot Cripple provenance");
	return true;
}
std::vector<Bonus> crippleRemoval(const battle::Unit & unit)
{
	std::vector<Bonus> result;
	const auto crippleBonuses = unit.getBonuses(CSelector(isCripple));
	for(const auto & bonus : *crippleBonuses) result.push_back(*bonus);
	return result;
}
Strike captureStrike(const CBattleInfoCallback & battle, const BattleAttackInfo & attack, int64_t damage)
{
	Strike result;
	if(!attack.attacker || !attack.defender || attack.secondaryAttack || attack.bracePreemptive
		|| attack.preemptiveDamagePercent > 0 || attack.cleaveDamagePercent > 0
		|| !attack.attacker->alive() || attack.attacker->isGhost() || attack.attacker->isTimeStopped()) return result;
	const auto & source = *attack.attacker;
	result.source = source.unitId(); result.target = attack.defender->unitId(); result.impact = attack.defenderPos;
	if(attack.shooting && attack.physicalDamage && !attack.retaliation && source.isBallista())
	{
		result.fireball = value(battle, source, "core:torosar", "incendiaryShotPolicyVersion") == 1;
		if(result.fireball && !result.impact.isValid())
			throw std::invalid_argument("Fiery Shot requires the accepted impact hex");
		if(damage > 0 && battle.battleGetOwner(&source) != battle.battleGetOwner(attack.defender)
			&& value(battle, source, "core:gerwulf", "cripplingBoltSpeedReduction") == 3)
		{
			result.cripple.target = result.target;
			result.cripple.remove = crippleRemoval(*attack.defender);
			Bonus effect(BonusDuration::N_TURNS, BonusType::STACKS_MOVEMENT_RANGE, BonusSource::HERO_SPECIAL,
				-3, BonusSourceID(HeroTypeID(HeroTypeID::decode("core:gerwulf"))), BonusSubtypeID(), BonusValueType::ADDITIVE_VALUE);
			effect.turnsRemain = value(battle, source, "core:gerwulf", "cripplingBoltDuration");
			effect.stacking = CRIPPLE; effect.statusIdentity = "cripple";
			effect.statusTags = {BonusStatusTag::DEBUFF, BonusStatusTag::NON_TRANSFERABLE};
			effect.description.appendRawString("Cripple: -3 Speed for 3 rounds.");
			result.cripple.add.push_back(effect);
			effect.type = BonusType::PHYSICAL_AFFLICTION; effect.val = 0; effect.valType = BonusValueType::BASE_NUMBER;
			JsonNode metadata; metadata["kind"].String() = "cripple"; metadata["applicationOrder"].Integer() = 0;
			for(const auto & old : result.cripple.remove)
				if(const auto previous = physicalAfflictions::markerMetadata(old))
					metadata["applicationOrder"].Integer() = previous->applicationOrder;
			effect.parameters = std::make_shared<BonusParameters>(metadata);
			result.cripple.add.push_back(std::move(effect));
		}
	}
	result.bloodlust = !attack.shooting && family(source, Family::Ogre)
		&& value(battle, source, "core:krellion", "ogreBloodlustPolicyVersion") == 1;
	return result;
}
BallistaCaster::BallistaCaster(const battle::Unit & unit, const CGHeroInstance & hero) : ProxyCaster(&hero), source(unit) {}
int32_t BallistaCaster::getCasterUnitId() const { return source.unitId(); }
PlayerColor BallistaCaster::getCasterOwner() const { return source.getCasterOwner(); }
void BallistaCaster::spendMana(ServerCallback *, int32_t) const {}
std::vector<uint32_t> bloodlustRecipients(const CBattleInfoCallback & battle, const battle::Unit & source)
{
	spells::AbilityCaster caster(&source, BLOODLUST_LEVEL);
	spells::BattleCast cast(&battle, &caster, spells::Mode::PASSIVE, SpellID(SpellID::BLOODLUST).toSpell());
	cast.setSpellLevel(BLOODLUST_LEVEL);
	cast.setEffectDuration(BLOODLUST_DURATION);
	std::vector<uint32_t> result;
	for(const auto * unit : battle.battleGetAllUnits())
	{
		if(!unit->alive() || unit->isGhost() || unit->isTimeStopped()
			|| battle.battleGetOwner(unit) != battle.battleGetOwner(&source)) continue;
		spells::Target target{spells::Destination(unit)};
		if(cast.mechanicsForTarget(target)->canBeCastAt(target)) result.push_back(unit->unitId());
	}
	std::sort(result.begin(), result.end());
	return result;
}
}
