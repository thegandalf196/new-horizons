/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "PreparedBonusRule.h"
#include "../bonuses/Limiters.h"
#include <limits>
#include <stdexcept>

namespace nativeRules
{
namespace
{
bool permanentContract(const BonusRuleContract & contract, const Bonus & bonus)
{
	if(contract.lifetime != LifetimePolicy::Permanent || bonus.duration != BonusDuration::PERMANENT
		|| contract.parentStatus)
		return false;
	if(contract.source == SourceKind::SkillRank)
		return bonus.source == BonusSource::SECONDARY_SKILL && bonus.sid.as<SecondarySkill>().hasValue()
			&& contract.target == TargetDomain::Army
			&& contract.stage == EvaluationStage::Attachment;
	if(contract.source != SourceKind::HeroSpecialty || bonus.source != BonusSource::HERO_SPECIAL
		|| !bonus.sid.as<HeroTypeID>().hasValue() || !bonus.limiter)
		return false;
	return (contract.target == TargetDomain::CreatureFamily && contract.stage == EvaluationStage::Attachment)
		|| (contract.target == TargetDomain::AdjacentArmy && contract.stage == EvaluationStage::SpatialQuery);
}

bool castContract(const BonusRuleContract & contract, const Bonus & bonus)
{
	if(contract.source != SourceKind::SpellAddition || bonus.source != BonusSource::SPELL_EFFECT
		|| (bonus.sid != BonusSourceID(SpellID(SpellID::NONE)) && !bonus.sid.as<SpellID>().hasValue())
		|| contract.target != TargetDomain::SpellRecipient || contract.stage != EvaluationStage::CastApplication)
		return false;
	if(contract.lifetime == LifetimePolicy::Rounds)
		return bonus.duration == BonusDuration::N_TURNS && !contract.parentStatus;
	return contract.lifetime == LifetimePolicy::ParentStatus && contract.parentStatus
		&& *contract.parentStatus != BonusType::NONE && *contract.parentStatus < BonusType::BUILTIN_BONUSES_COUNT
		&& *contract.parentStatus != bonus.type && bonus.duration == BonusDuration::ONE_BATTLE;
}
}

PreparedBonusRule::PreparedBonusRule(BonusRuleContract contract, const Bonus & prototype)
	: contract(std::move(contract)), prototype(prototype)
{
	if(this->contract.version != 1 || prototype.type == BonusType::NONE
		|| prototype.type >= BonusType::BUILTIN_BONUSES_COUNT
		|| static_cast<unsigned>(prototype.valType) > static_cast<unsigned>(BonusValueType::INDEPENDENT_MIN)
		|| (!permanentContract(this->contract, prototype) && !castContract(this->contract, prototype)))
		throw std::invalid_argument("Unsupported prepared native Bonus rule contract");
}

std::shared_ptr<Bonus> PreparedBonusRule::attach() const
{
	if(contract.lifetime != LifetimePolicy::Permanent)
		throw std::invalid_argument("Cast-owned native Bonus rule cannot attach as a passive");
	return std::make_shared<Bonus>(prototype);
}

Bonus PreparedBonusRule::bindSpell(SpellID source, int32_t rounds) const
{
	if(contract.source != SourceKind::SpellAddition || !source.hasValue()
		|| rounds < 0 || rounds > std::numeric_limits<int16_t>::max()
		|| (contract.lifetime == LifetimePolicy::Rounds ? rounds == 0 : rounds != 0))
		throw std::invalid_argument("Invalid prepared native spell Bonus binding");
	Bonus result(prototype, BonusSourceID(source));
	if(contract.lifetime == LifetimePolicy::Rounds)
		result.turnsRemain = static_cast<int16_t>(rounds);
	if(contract.parentStatus)
	{
		auto parent = std::make_shared<HasAnotherBonusLimiter>(*contract.parentStatus, BonusSource::SPELL_EFFECT);
		parent->sid = BonusSourceID(source);
		parent->isSourceIDRelevant = true;
		if(result.limiter)
		{
			auto all = std::make_shared<AllOfLimiter>();
			all->add(result.limiter);
			all->add(parent);
			result.limiter = std::move(all);
		}
		else
			result.limiter = std::move(parent);
	}
	return result;
}

PreparedBonusBundle::PreparedBonusBundle(std::vector<PreparedBonusRule> rules) : rules(std::move(rules))
{
	if(this->rules.empty() || this->rules.size() > 8)
		throw std::invalid_argument("Prepared native Bonus bundle requires one to eight operations");
	const auto & source = this->rules.front().policy();
	for(const auto & rule : this->rules)
		if(rule.policy().source != source.source || rule.policy().stage != source.stage
			|| rule.policy().target != source.target || rule.policy().lifetime != source.lifetime)
			throw std::invalid_argument("Prepared native Bonus bundle mixes source controllers or lifetimes");
}

std::vector<Bonus> PreparedBonusBundle::bindSpell(SpellID source, int32_t rounds) const
{
	std::vector<Bonus> result;
	result.reserve(rules.size());
	for(const auto & rule : rules)
		result.push_back(rule.bindSpell(source, rounds));
	return result;
}
}
