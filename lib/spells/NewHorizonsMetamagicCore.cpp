/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NewHorizonsMetamagicCore.h"
#include "NewHorizonsMetamagicCompletion.h"
#include "NewHorizonsMagic.h"
#include "ISpellMechanics.h"
#include "CSpell.h"
#include "ObstacleCasterProxy.h"
#include "../nativeRules/CompiledMetamagicRules.h"
#include "../nativeRules/CapturedRuleSet.h"
#include "../battle/CBattleInfoCallback.h"
#include "../battle/IBattleState.h"
#include "../battle/ConsecratedArms.h"
#include "../mapObjects/CGHeroInstance.h"
namespace newHorizonsMetamagic
{
namespace
{
const nativeRules::MetamagicSpellIdentity * identity(const spells::Mechanics & mechanics)
{
	const auto * callback = mechanics.battle();
	if(!callback || !callback->getBattle()) return nullptr;
	const auto owner = callback->getBattle()->getCapturedMagicRules();
	if(!owner || owner->kind() != nativeRules::CapturedRuleKind::WorldMagic
		|| &owner->document() != &callback->getBattle()->getMagicRules()) return nullptr;
	const auto energy = owner->metamagicEnergy();
	return energy ? energy->identity(mechanics.getSpellId()) : nullptr;
}
}

std::optional<bool> singleRecipientEffectPayload(const spells::Mechanics & mechanics)
{
	const auto * facts = identity(mechanics);
	const int level = mechanics.getEffectLevel();
	if(!facts || level < 0 || level >= 4
		|| facts->effectCardinalityByLevel[level] == nativeRules::MetamagicTargetShape::Unsupported) return {};
	return facts->effectCardinalityByLevel[level] == nativeRules::MetamagicTargetShape::Single;
}

bool singleTargetSpell(const spells::Mechanics & mechanics)
{
	if(mechanics.isMassive() || mechanics.getEffectiveChainLength(1) > 1) return false;
	const auto * facts = identity(mechanics);
	const int level = mechanics.getRangeLevel();
	return facts && level >= 0 && level < 4 && facts->singleTargetByLevel[level];
}

bool empoweredDamage(const spells::Mechanics & mechanics)
{
	if(!mechanics.isGenuineHeroCast() || mechanics.isRunicReflectionShare()
		|| !nativeRules::hasMetamagicPerk(mechanics.getHeroCaster(), nativeRules::MetamagicPerk::EmpoweredSpells)) return false;
	const auto * spell = mechanics.getSpell();
	return spell && spell->isDamage() && spell->isMagical() && singleTargetSpell(mechanics);
}

int debuffRetryChance(const spells::Mechanics & mechanics)
{
	if(!mechanics.isGenuineHeroCast() || mechanics.isRunicReflectionShare()) return 0;
	const auto * hero = mechanics.getHeroCaster();
	const auto * policy = nativeRules::metamagicRules(hero);
	const int chance = policy ? policy->debuffRetryPercent(nativeRules::metamagicSkillRank(hero)) : 0;
	if(chance <= 0) return 0;
	const auto * spell = mechanics.getSpell();
	if(!spell || !spell->isMagical() || spell->isDamage() || !singleTargetSpell(mechanics)) return 0;
	const auto * facts = identity(mechanics);
	const int level = mechanics.getEffectLevel();
	return facts && level >= 0 && level < 4 && facts->debuffByLevel[level] ? chance : 0;
}

bool retryResistance(bool resisted, int retryPercent, int resistancePercent, const std::function<int()> & sample)
{
	if(!resisted || retryPercent <= 0) return resisted;
	if(sample() >= std::clamp(retryPercent, 0, 100)) return true;
	return sample() < std::clamp(resistancePercent, 0, 100);
}

bool explicitlyIrresistible(const spells::Mechanics & mechanics)
{
	const auto descriptor = mechanics.getCreatureAbilityDescriptor();
	return descriptor && (descriptor->interactions & creatureAbility::IRRESISTIBLE);
}

bool hasResistibleCreatureAffliction(const spells::Mechanics & mechanics)
{
	const auto descriptor = mechanics.getCreatureAbilityDescriptor();
	return descriptor && descriptor->family == creatureAbility::Family::CREATURE_ABILITY
		&& descriptor->resolution == creatureAbility::Resolution::SPELL_CAST
		&& (descriptor->categories & creatureAbility::HOSTILE_AFFLICTION)
		&& !(descriptor->categories & creatureAbility::DAMAGE)
		&& (descriptor->interactions & creatureAbility::PREVENTABLE)
		&& !(descriptor->interactions & creatureAbility::IRRESISTIBLE)
		&& mechanics.getSpell() && !mechanics.getSpell()->isDamage();
}

int normalResistanceBasisPoints(const spells::Mechanics & mechanics, const battle::Unit * recipient)
{
	if(!recipient || !mechanics.battle() || explicitlyIrresistible(mechanics)) return 0;
	const auto * callback = mechanics.battle();
	const int base = std::clamp(callback->battleGetMagicResistance(recipient), 0, 100) * 100;
	const auto * spell = mechanics.getSpell();
	if(hasResistibleCreatureAffliction(mechanics)
		&& callback->battleGetOwner(recipient) != callback->sideToPlayer(mechanics.getEffectCasterSide()))
	{
		const auto descriptor = mechanics.getCreatureAbilityDescriptor();
		const int extra = creatureAbility::unshakableResistanceBP(*descriptor, *recipient);
		const int ordinary = descriptor->interactions & creatureAbility::NORMAL_RESISTIBLE ? base : 0;
		return extra > 0 && ordinary < 7500 ? std::min(7500, ordinary + extra) : ordinary;
	}
	if(!spell || !spell->isMagical() || spell->isCreatureAbility() || spell->isDamage()
		|| callback->battleGetOwner(recipient) == callback->sideToPlayer(mechanics.getEffectCasterSide())) return base;
	const auto prayer = consecratedArms::prayerValues(*recipient, consecratedArms::Prayer::STEADFAST_SOUL);
	return prayer.resistanceBP > 0 && base < 7500 ? std::min(7500, base + prayer.resistanceBP) : base;
}

int normalResistancePercent(const spells::Mechanics & mechanics, const battle::Unit * recipient)
{
	return normalResistanceBasisPoints(mechanics, recipient) / 100;
}

bool retryResistanceBasisPoints(bool resisted, int retryPercent, int resistanceBP,
	const std::function<int(int)> & sample)
{
	if(!resisted || retryPercent <= 0) return resisted;
	if(sample(99) >= std::clamp(retryPercent, 0, 100)) return true;
	const int probability = std::clamp(resistanceBP, 0, 10000);
	return probability % 100 == 0 ? sample(99) < probability / 100 : sample(9999) < probability;
}


int64_t retainedFriendlyDamage(int64_t damage, int retainedPercent)
{
	if(damage <= 0) return 0;
	const int factor = std::clamp(retainedPercent, 0, 100);
	return damage / 100 * factor + damage % 100 * factor / 100;
}

double resistanceFailureProbability(int resistancePercent, int retryPercent)
{
	const double resistance = std::clamp(resistancePercent, 0, 100) / 100.0;
	const double retry = std::clamp(retryPercent, 0, 100) / 100.0;
	return resistance * (1.0 - retry * (1.0 - resistance));
}

double resistanceFailureProbability(int resistancePercent, int retryPercent, double initialFailureProbability)
{
	const double resistance = std::clamp(resistancePercent, 0, 100) / 100.0;
	const double retry = std::clamp(retryPercent, 0, 100) / 100.0;
	return std::clamp(initialFailureProbability, 0.0, 1.0) * (1.0 - retry * (1.0 - resistance));
}

double resistanceFailureProbabilityBasisPoints(int resistanceBP, int retryPercent, double initialFailureProbability)
{
	const double resistance = std::clamp(resistanceBP, 0, 10000) / 10000.0;
	const double retry = std::clamp(retryPercent, 0, 100) / 100.0;
	return std::clamp(initialFailureProbability, 0.0, 1.0) * (1.0 - retry * (1.0 - resistance));
}

double effectiveResistanceFailureProbability(const spells::Mechanics & mechanics, const battle::Unit * recipient)
{
	if(!recipient || !mechanics.battle() || empoweredDamage(mechanics)
		|| mechanics.battle()->battleGetOwner(recipient) == mechanics.battle()->sideToPlayer(mechanics.getEffectCasterSide())) return 0;
	const int bp = normalResistanceBasisPoints(mechanics, recipient);
	return resistanceFailureProbabilityBasisPoints(bp, debuffRetryChance(mechanics), bp / 10000.0);
}

int friendlyDamagePercent(const spells::Mechanics & mechanics)
{
	const auto * spell = dynamic_cast<const CSpell *>(mechanics.getSpell());
	const auto * hero = mechanics.getHeroCaster();
	if(!spell || !spell->isMagical()) return 100;
	if(mechanics.isGenuineHeroCast() && hero && !mechanics.isRunicReflectionShare())
	{
		if(nativeRules::hasMetamagicPerk(hero, nativeRules::MetamagicPerk::SpellSculpting)) return 0;
		const auto * rules = nativeRules::metamagicRules(hero);
		const int reduction = rules ? rules->friendlyReductionPercent(nativeRules::metamagicSkillRank(hero)) : 0;
		if(reduction > 0 && ((spell->isDamage() && !singleTargetSpell(mechanics)) || spell->isPersistent()
			|| newHorizonsMagic::isLandMine(spell->getId()) || newHorizonsMagic::isFireWall(spell->getId())))
			return 100 - reduction;
	}
	if(dynamic_cast<const spells::ObstacleCasterProxy *>(mechanics.caster))
	{
		const auto captured = mechanics.getCapturedMdrPenetration();
		if(captured["spellSculpting"].isBool() && captured["spellSculpting"].Bool()) return 0;
		const auto & factor = captured["metamagicFriendlyDamagePercent"];
		if(factor.getType() == JsonNode::JsonType::DATA_INTEGER
			&& (factor.Integer() == 70 || factor.Integer() == 55 || factor.Integer() == 40))
			return static_cast<int>(factor.Integer());
	}
	return 100;
}

int recipientDamagePercent(const spells::Mechanics & mechanics, const battle::Unit * recipient)
{
	const auto * callback = mechanics.battle();
	if(!recipient || !callback || callback->battleGetOwner(recipient)
		!= callback->sideToPlayer(mechanics.getEffectCasterSide())) return 100;
	return friendlyDamagePercent(mechanics);
}
}
