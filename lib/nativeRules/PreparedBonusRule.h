/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once

#include "../bonuses/Bonus.h"
#include <optional>
#include <span>
#include <vector>

namespace nativeRules
{
enum class SourceKind : uint8_t { SkillRank, HeroSpecialty, SpellAddition };
enum class TargetDomain : uint8_t { Army, CreatureFamily, AdjacentArmy, SpellRecipient };
enum class EvaluationStage : uint8_t { Attachment, SpatialQuery, CastApplication };
enum class LifetimePolicy : uint8_t { Permanent, Rounds, ParentStatus };

/// Source adapters select this contract; authors cannot spoof another controller.
struct DLL_LINKAGE BonusRuleContract
{
	uint16_t version = 1;
	SourceKind source = SourceKind::SkillRank;
	TargetDomain target = TargetDomain::Army;
	EvaluationStage stage = EvaluationStage::Attachment;
	LifetimePolicy lifetime = LifetimePolicy::Permanent;
	std::optional<BonusType> parentStatus;
};

/// Owned immutable payload, with existing immutable limiter/updater objects.
/// No live battle pointers, evaluation cache, event dispatch or executable expressions.
class DLL_LINKAGE PreparedBonusRule
{
	BonusRuleContract contract;
	Bonus prototype;
public:
	PreparedBonusRule(BonusRuleContract contract, const Bonus & prototype);
	const BonusRuleContract & policy() const { return contract; }
	const Bonus & preparedBonus() const { return prototype; }
	std::shared_ptr<Bonus> attach() const;
	Bonus bindSpell(SpellID source, int32_t rounds = 0) const;
};

/// Composition retains authored operation order; inactive bundles are never attached.
class DLL_LINKAGE PreparedBonusBundle
{
	std::vector<PreparedBonusRule> rules;
public:
	explicit PreparedBonusBundle(std::vector<PreparedBonusRule> rules);
	std::span<const PreparedBonusRule> operations() const { return rules; }
	std::vector<Bonus> bindSpell(SpellID source, int32_t rounds = 0) const;
};
}
