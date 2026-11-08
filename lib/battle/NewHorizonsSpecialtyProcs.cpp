/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NewHorizonsSpecialtyProcs.h"
#include "NewHorizonsStun.h"
#include "NewHorizonsCreatureAbilitySuppression.h"
#include "SpecialtyEventPolicy.h"
#include "BattleAttackInfo.h"
#include "CBattleInfoCallback.h"
#include "Unit.h"
#include "../CCreatureHandler.h"
#include "../bonuses/BonusList.h"
#include "../nativeRules/NativeRulePilots.h"
#include "../combatScripts/NativeJsonParameters.h"
#include "../spells/CSpell.h"
#include "../spells/ISpellMechanics.h"
#include "../spells/AbilityCaster.h"
#include "../spells/effects/native/StatusHelpers.h"
#include <vcmi/spells/Caster.h>
namespace newHorizonsSpecialtyProcs
{
namespace
{
constexpr auto VENOM = "new-horizons:broghild:wyvernVenom";
bool broghild(const CBattleInfoCallback & battle, const battle::Unit & unit)
{
	using namespace newHorizonsSpecialtyEvents::detail;
	return family(unit, Family::Wyvern) && newHorizonsCreatureAbilitySuppression::suppressionLevel(unit) == 0
		&& coefficient(battle, unit, "core:broghild", "wyvernVenomPolicyVersion") == 1;
}
const battle::Unit * castingUnit(const spells::Mechanics & mechanics)
{
	return mechanics.battle() && mechanics.caster && mechanics.caster->getCasterUnitId() >= 0
		? mechanics.battle()->battleGetUnitByID(mechanics.caster->getCasterUnitId()) : nullptr;
}
int nativePoisonChance()
{
	static const auto monarch = CreatureID(CreatureID::decode("core:wyvernMonarch"));
	return std::clamp(monarch.toCreature()->valOfBonuses(BonusType::SPELL_AFTER_ATTACK,
		BonusSubtypeID(SpellID(SpellID::POISON))), 0, 100);
}
}
std::optional<int> attackSpellChance(const CBattleInfoCallback & battle, const battle::Unit & unit, SpellID spell, bool ranged)
{
	if(spell == SpellID::POISON && broghild(battle, unit))
	{
		static const auto base = CreatureID(CreatureID::decode("core:wyvern"));
		// Monarchs already own the native ability. Preserve all of its ordinary
		// chance/level modifiers instead of overriding it with a second grant.
		if(unit.creatureId() != base) return std::nullopt;
		return std::clamp(nativePoisonChance() + unit.valOfBonuses(BonusType::SPELL_AFTER_ATTACK,
			BonusSubtypeID(SpellID(SpellID::POISON))), 0, 100);
	}
	using namespace newHorizonsSpecialtyEvents::detail;
	if(spell != SpellID::DISPEL_HELPFUL_SPELLS || !ranged || !family(unit, Family::Mage)) return std::nullopt;
	static const auto mage = CreatureID(CreatureID::decode("core:mage"));
	const char * key = unit.creatureId() == mage ? "dispellingShotMageChancePercent" : "dispellingShotArchMageChancePercent";
	const int chance = coefficient(battle, unit, "core:theodorus", key);
	return chance > 0 ? std::optional<int>(chance) : std::nullopt;
}
std::vector<SpellID> additionalAttackSpells(const CBattleInfoCallback & battle, const battle::Unit & unit, bool ranged)
{
	std::vector<SpellID> result;
	for(const auto id : {SpellID(SpellID::POISON), SpellID(SpellID::DISPEL_HELPFUL_SPELLS)})
		if(attackSpellChance(battle, unit, id, ranged)) result.push_back(id);
	std::sort(result.begin(), result.end());
	return result;
}
int poisonPotencyMultiplier(const CBattleInfoCallback & battle, const battle::Unit & unit)
{
	static const auto wyvern = CreatureID(CreatureID::decode("core:wyvern"));
	return broghild(battle, unit) && unit.creatureId() != wyvern ? 2 : 1;
}
bool isStrongWyvernPoison(const Bonus * bonus)
{
	if(!bonus || bonus->stacking != VENOM) return false;
	if(bonus->source != BonusSource::SPELL_EFFECT || bonus->sid != BonusSourceID(SpellID(SpellID::POISON))
		|| bonus->duration != BonusDuration::N_TURNS || bonus->turnsRemain < 0
		|| !((bonus->type == BonusType::POISON && bonus->val == 60 && bonus->valType == BonusValueType::INDEPENDENT_MAX)
			|| (bonus->type == BonusType::STACK_HEALTH && bonus->val <= -20 && bonus->val >= -60
				&& bonus->valType == BonusValueType::PERCENT_TO_ALL)))
		throw std::runtime_error("Invalid stronger Wyvern Poison provenance");
	return true;
}
void scaleNativePoison(JsonMap & bonuses, int multiplier)
{
	if(multiplier != 1 && multiplier != 2) throw std::invalid_argument("Invalid native Venom multiplier");
	if(multiplier == 1) return;
	for(auto & [name, bonus] : bonuses)
		if(bonus["type"].String() == "POISON" || bonus["type"].String() == "STACK_HEALTH")
		{
			bonus["val"].Float() *= multiplier;
			bonus["stacking"].String() = VENOM;
		}
}
void scaleNativePoisonForCast(const spells::Mechanics & mechanics, JsonMap & bonuses)
{
	const auto * caster = castingUnit(mechanics);
	if(mechanics.getSpellId() == SpellID::POISON && caster && dynamic_cast<const spells::AbilityCaster *>(mechanics.caster))
		scaleNativePoison(bonuses, poisonPotencyMultiplier(*mechanics.battle(), *caster));
}
bool isStrongWyvernPoisonCast(const spells::Mechanics & mechanics)
{
	const auto * caster = castingUnit(mechanics);
	return mechanics.getSpellId() == SpellID::POISON && caster
		&& dynamic_cast<const spells::AbilityCaster *>(mechanics.caster)
		&& poisonPotencyMultiplier(*mechanics.battle(), *caster) == 2;
}
int poisonRampValue(const battle::Unit & unit, const Bonus & current)
{
	const int step = isStrongWyvernPoison(&current) ? 20 : 10;
	return std::max(current.val - step, -unit.valOfBonuses(BonusType::POISON));
}
std::vector<Bonus> poisonBonusPlan(const CBattleInfoCallback & battle, const battle::Unit & caster, int duration)
{
	const auto * spell = SpellID(SpellID::POISON).toSpell();
	auto descriptors = spell->getLevelInfo(0).battleEffects["poisoning"]["bonus"].Struct();
	scaleNativePoison(descriptors, poisonPotencyMultiplier(battle, caster));
	std::vector<Bonus> result;
	for(auto & [name, descriptor] : descriptors)
	{
		descriptor["turns"].Integer() = duration;
		descriptor["sourceType"].String() = "SPELL_EFFECT";
		descriptor["sourceID"].String() = "core:poison";
		result.push_back(nativeRules::bonusFromDescriptor(descriptor));
	}
	return result;
}
bool isPositiveTemporaryMagic(const Bonus & bonus)
{
	if(bonus.source != BonusSource::SPELL_EFFECT || !bonus.sid.as<SpellID>().hasValue()) return false;
	const auto * spell = bonus.sid.as<SpellID>().toSpell();
	return spell && spell->isPositive() && spell->isMagical() && !spell->isAdventure()
		&& !spell->isPersistent() && spells::effects::native::status::isTemporary(bonus);
}
std::vector<Bonus> dispelRemovalPlan(const battle::Unit & unit)
{
	std::vector<Bonus> result;
	const auto bonuses = unit.getBonuses(Selector::all, "all");
	for(const auto & bonus : *bonuses)
		if(bonus && isPositiveTemporaryMagic(*bonus)) result.push_back(*bonus);
	return result;
}
bool isNullifyingShotCast(const spells::Mechanics & mechanics)
{
	const auto * caster = castingUnit(mechanics);
	return mechanics.getSpellId() == SpellID::DISPEL_HELPFUL_SPELLS && caster
		&& attackSpellChance(*mechanics.battle(), *caster, SpellID(SpellID::DISPEL_HELPFUL_SPELLS), true).has_value();
}
int stunChance(const CBattleInfoCallback & battle, const BattleAttackInfo & attack)
{
	if(!attack.attacker || !attack.defender || !attack.attacker->alive() || !attack.defender->alive()
		|| attack.attacker->isGhost() || attack.defender->isGhost() || attack.defender->isTimeStopped()
		|| attack.defender->isInvincible() || attack.defender->isTurret() || newHorizonsStun::isStunned(*attack.defender)
		|| attack.secondaryAttack || attack.bracePreemptive || attack.preemptiveDamagePercent > 0
		|| attack.cleaveDamagePercent > 0 || battle.battleGetOwner(attack.attacker) == battle.battleGetOwner(attack.defender)) return 0;
	using namespace newHorizonsSpecialtyEvents::detail;
	return family(*attack.attacker, Family::Cyclops)
		? coefficient(battle, *attack.attacker, "core:yog", "cyclopeanStunChancePercent") : 0;
}
}
