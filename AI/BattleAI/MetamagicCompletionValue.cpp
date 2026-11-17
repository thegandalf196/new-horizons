/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../../lib/nativeRules/CompiledMetamagicRules.h"
#include "MetamagicCompletionValue.h"
#include "MetamagicExpectedValue.h"
#include "FixedActionValue.h"
#include "HypotheticBattle.h"
#include "SpellTargetsEvaluator.h"
#include "../../lib/spells/ISpellMechanics.h"
#include "../../lib/spells/CSpell.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/entities/hero/CHero.h"
#include "../../lib/battle/NewHorizonsPassiveSpecialties.h"
#include "../../lib/battle/NewHorizonsCommandResources.h"
#include <array>
#include <cmath>
#include <set>
#include <limits>
namespace metamagicAI
{
Observation observeBefore(const HypotheticBattle & state, const spells::Mechanics & mechanics)
{
	Observation result;
	const auto * hero = mechanics.getHeroCaster();
	if(!hero || !nativeRules::metamagicRules(hero) || !nativeRules::metamagicRules(hero)->policy()) return result;
	result.before = newHorizonsMetamagic::captureRecipients(state, mechanics);
	for(const auto & [id, snapshot] : result.before)
	{
		result.applicationChance[id] = SpellTargetEvaluator::applicationChance(&mechanics, state.battleGetUnitByID(id));
		result.originalOwners[id] = snapshot.originalOwner;
	}
	return result;
}
void observeAfter(Observation & observation, const spells::Mechanics & mechanics)
{
	if(observation.before.empty()) return;
	observation.resolved.assign(mechanics.getResolvedHeroRecipients().begin(), mechanics.getResolvedHeroRecipients().end());
	observation.injuries.assign(mechanics.getResolvedHeroInjuries().begin(), mechanics.getResolvedHeroInjuries().end());
}
namespace
{
bool contains(const battle::BattleEffectSnapshot & snapshot, const newHorizonsMetamagic::RemovalCandidate & candidate)
{
	return std::all_of(candidate.bonuses.begin(), candidate.bonuses.end(), [&](const Bonus & bonus)
	{
		return std::any_of(snapshot.effects.begin(), snapshot.effects.end(), [&](const Bonus & installed)
		{ return battle::exactBattleEffectEqual(bonus, installed); });
	});
}
}
std::optional<float> completionValue(const Environment * environment, const std::shared_ptr<HypotheticBattle> & state,
	const CGHeroInstance * hero, const CSpell * spell, const Observation & observation, const DamageCache & damage,
	float positive, float negative)
{
	if(!hero || !spell || observation.before.empty()) return 0;
	const auto * compiled = nativeRules::metamagicRules(hero);
	const auto * policy = compiled ? compiled->policy() : nullptr;
	if(!policy) return 0;
	const int rank = nativeRules::metamagicSkillRank(hero);
	const bool halon = compiled->isHalon(hero->getHeroTypeID());
	const double removalChance = compiled->removalChance(rank, hero->getHeroTypeID()) / 100.0;
	if(removalChance == 0) return 0;
	const auto player = hero->getCasterOwner();
	const auto has = [&](nativeRules::MetamagicPerk perk)
	{ return nativeRules::hasMetamagicPerk(hero, perk); };
	const auto energy = nativeRules::captureMetamagicEnergy(*state);
	const auto resolver = [&](SpellID id) -> std::optional<newHorizonsMetamagic::EnergyDefinition>
	{
		const auto * definition = energy->fuelDefinition(id);
		return definition ? std::optional<newHorizonsMetamagic::EnergyDefinition>(*definition) : std::nullopt;
	};
	std::optional<fixedActionValue::Reference> reference;
	DamageCache cache(&damage);
	std::set<uint32_t> seen;
	double value = 0;
	size_t work = 0;
	for(const auto id : observation.resolved)
	{
		const auto prior = observation.before.find(id);
		const auto * target = state->battleGetUnitByID(id);
		if(prior == observation.before.end() || !target || !target->alive() || target->isGhost() || !seen.insert(id).second) continue;
		const auto originalOwner = prior->second.originalOwner;
		if(originalOwner == PlayerColor::CANNOT_DETERMINE || player == PlayerColor::CANNOT_DETERMINE) continue;
		const bool friendly = originalOwner == player;
		const bool restoration = friendly && spell->isPositive();
		const bool hit = std::any_of(observation.injuries.begin(), observation.injuries.end(),
			[id](const BattleStackAttacked & injury) { return injury.stackAttacked == id; });
		if(!restoration && (friendly || !(spell->isDamage() ? hit : spell->isNegative() && has(nativeRules::MetamagicPerk::HexWeaving)))) continue;
		const auto kind = restoration ? newHorizonsMetamagic::ResolutionKind::RESTORATION : newHorizonsMetamagic::ResolutionKind::OFFENSE;
		auto effects = newHorizonsMetamagic::collectRemovalCandidates(id, originalOwner, prior->second.effects, kind, resolver);
		const auto current = state->captureBattleEffects(id);
		std::erase_if(effects, [&](const auto & effect) { return !contains(current, effect); });
		if(effects.empty()) continue;
		work += effects.size();
		if(work > 64) return std::nullopt;
		if(!reference) reference.emplace(state, player, damage, positive, negative);
		for(const auto & effect : effects)
		{
			auto removed = std::make_shared<HypotheticBattle>(environment, state);
			removed->removeUnitBonus(id, effect.bonuses);
			const auto after = removed->captureBattleEffects(id);
			if(std::any_of(after.effects.begin(), after.effects.end(), [&](const Bonus & installed)
			{ return std::any_of(effect.bonuses.begin(), effect.bonuses.end(), [&](const Bonus & bonus)
				{ return battle::exactBattleEffectEqual(installed, bonus); }); })) return std::nullopt;
			const auto marginal = reference->marginal(removed, std::array{id});
			if(!marginal) return std::nullopt;
			value += removalChance * *marginal * observation.applicationChance.at(id);
		}
		const auto adjust = [&](int64_t pool)
		{
			if(restoration) return target->healingCapacity(pool, EHealLevel::RESURRECT,
				target->isSummoned() ? EHealPower::ONE_BATTLE : EHealPower::PERMANENT);
			if(has(nativeRules::MetamagicPerk::ArcaneRupture)) return pool;
			if(newHorizonsPassiveSpecialties::magicalDamageImmune(*target)) return int64_t{0};
			pool = spell->adjustRawDamage(nullptr, target, pool, 0, 0, 100, true, true, false);
			return newHorizonsCommand::mitigateIncomingDamage(*state, target, pool);
		};
		const auto pool = expectedPool(effects, *policy, rank, halon, kind,
			restoration ? std::numeric_limits<int64_t>::max() : target->getAvailableHealth(), adjust);
		if(!pool) return std::nullopt;
		if(pool->hitPoints > 0)
		{
			const auto rounded = static_cast<uint64_t>(std::ceil(pool->hitPoints));
			const auto utility = AttackPossibility::calculateDamageReduce(nullptr, target, rounded, cache, state);
			// Eligibility/removal above use originalOwner even after control flips.
			// Utility prices HP gained/lost for the current projected controller.
			const bool currentlyOurs = state->battleGetActionController(target) == player;
			const float sign = restoration == currentlyOurs ? 1 : -1;
			value += sign * utility * (pool->hitPoints / rounded) * (sign > 0 ? positive : negative)
				* observation.applicationChance.at(id);
		}
	}
	return std::isfinite(value) ? std::optional<float>(static_cast<float>(value)) : std::nullopt;
}
}
