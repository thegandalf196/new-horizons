/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NativeRulePilots.h"
#include "../bonuses/Limiters.h"
#include "../bonuses/Propagators.h"
#include "../bonuses/Updaters.h"
#include <stdexcept>

namespace nativeRules
{
std::shared_ptr<Bonus> prepareSkillRankBonus(std::shared_ptr<Bonus> bonus)
{
	if(!bonus)
		throw std::invalid_argument("Skill rank preparation requires an owned Bonus");
	if(bonus->type != BonusType::NONE)
		(void)PreparedBonusRule(BonusRuleContract{}, *bonus);
	// Identifier callbacks registered by the existing loader still target this
	// object. Keep its identity until finalization; the skill already owns its
	// prepared immutable template after loading, and installation copies it.
	return bonus;
}

std::shared_ptr<Bonus> prepareFamilySpecialtyBonus(const Bonus & bonus)
{
	BonusRuleContract contract;
	contract.source = SourceKind::HeroSpecialty;
	contract.target = TargetDomain::CreatureFamily;
	return PreparedBonusRule(contract, bonus).attach();
}

std::shared_ptr<Bonus> prepareAdjacentSpecialtyBonus(const Bonus & bonus)
{
	BonusRuleContract contract;
	contract.source = SourceKind::HeroSpecialty;
	contract.target = TargetDomain::AdjacentArmy;
	contract.stage = EvaluationStage::SpatialQuery;
	return PreparedBonusRule(contract, bonus).attach();
}

void configureAdjacentSpecialtyBonus(Bonus & bonus, CreatureID family, AuraPolicy policy)
{
	if(!family.hasValue() || static_cast<unsigned>(policy) > static_cast<unsigned>(AuraPolicy::DirectMagicProtection))
		throw std::invalid_argument("Unsupported native adjacency aura policy");
	auto adjacent = std::make_shared<UnitAdjacentLimiter>();
	adjacent->targetUnit = family;
	adjacent->filter.includeUpgrades = true;
	adjacent->filter.relationship = AdjacentUnitFilter::Relationship::SAME_ARMY;
	if(policy == AuraPolicy::NativeUndeadDefense)
	{
		adjacent->filter.anyCreature = true;
		adjacent->targetUnit = CreatureID();
		adjacent->filter.nativeTrait = AdjacentUnitFilter::NativeTrait::UNDEAD;
		auto all = std::make_shared<AllOfLimiter>();
		all->add(bonus.limiter);
		all->add(adjacent);
		bonus.limiter = all;
	}
	else
		bonus.limiter = adjacent;
	if(policy == AuraPolicy::EnemyMeleeReduction)
	{
		adjacent->filter.relationship = AdjacentUnitFilter::Relationship::OPPOSING_ARMY;
		bonus.effectRange = BonusLimitEffect::ONLY_MELEE_FIGHT;
		bonus.propagator = std::make_shared<CPropagatorNodeType>(BonusNodeType::BATTLE_WIDE);
		bonus.propagationUpdater = std::make_shared<OwnerUpdater>();
		auto all = std::make_shared<AllOfLimiter>();
		all->add(adjacent);
		all->add(std::make_shared<OppositeSideLimiter>());
		bonus.limiter = all;
	}
	else if(policy == AuraPolicy::DirectMagicProtection)
	{
		bonus.subtype = BonusSubtypeID(SpellSchool::ANY);
		auto all = std::make_shared<AllOfLimiter>();
		all->add(adjacent);
		all->add(std::make_shared<CreatureLevelLimiter>(1));
		bonus.limiter = all;
	}
	else if(policy == AuraPolicy::FriendlyMeleeDamage)
	{
		bonus.subtype = BonusCustomSubtype::damageTypeMelee;
		bonus.effectRange = BonusLimitEffect::ONLY_MELEE_FIGHT;
	}
	else
		bonus.subtype = BonusSubtypeID(PrimarySkill::DEFENSE);
}

PreparedBonusRule sanctuaryKeeperTemplate()
{
	BonusRuleContract contract;
	contract.source = SourceKind::SpellAddition;
	contract.target = TargetDomain::SpellRecipient;
	contract.stage = EvaluationStage::CastApplication;
	contract.lifetime = LifetimePolicy::ParentStatus;
	contract.parentStatus = BonusType::SANCTIFIED;
	Bonus morale(BonusDuration::ONE_BATTLE, BonusType::MORALE, BonusSource::SPELL_EFFECT, 2, BonusSourceID(SpellID(SpellID::NONE)));
	return PreparedBonusRule(contract, morale);
}

PreparedBonusRule creatureSuppressionTemplate(bool intrinsicPassives)
{
	BonusRuleContract contract;
	contract.source = SourceKind::SpellAddition;
	contract.target = TargetDomain::SpellRecipient;
	contract.stage = EvaluationStage::CastApplication;
	contract.lifetime = LifetimePolicy::Rounds;
	Bonus suppression(BonusDuration::N_TURNS, BonusType::CREATURE_ABILITY_SUPPRESSION,
		BonusSource::SPELL_EFFECT, intrinsicPassives ? 2 : 1, BonusSourceID(SpellID(SpellID::NONE)));
	suppression.valType = BonusValueType::INDEPENDENT_MAX;
	suppression.statusTags.push_back(BonusStatusTag::DEBUFF);
	return PreparedBonusRule(contract, suppression);
}

Bonus sanctuaryKeeperMorale(SpellID sanctuary)
{
	static const auto prepared = sanctuaryKeeperTemplate();
	return prepared.bindSpell(sanctuary);
}

Bonus creatureSuppression(SpellID spell, std::string_view identity, int32_t rounds, bool intrinsicPassives)
{
	static const auto ordinary = creatureSuppressionTemplate(false);
	static const auto enhanced = creatureSuppressionTemplate(true);
	auto result = (intrinsicPassives ? enhanced : ordinary).bindSpell(spell, rounds);
	result.statusIdentity = identity;
	return result;
}
}
