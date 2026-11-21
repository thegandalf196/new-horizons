/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "DoubleCastFollowUp.h"
#include "AttackPossibility.h"
#include "HypotheticBattle.h"
#include "PossibleSpellcast.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/battle/MetamagicDoubleCast.h"
#include "../../lib/gameState/InfoAboutArmy.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/nativeRules/CompiledMetamagicRules.h"
#include "../../lib/spells/CSpellHandler.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace doubleCastAI
{
namespace
{
using Source = HeroActionAllowanceState::GrantSource;

/// Mana is worth a fraction of the best visible use of it: it pays a later cast only.
constexpr float MANA_DISCOUNT = 0.5f;
/// Mana the hero cannot spend soon (well above its next casts) is worth much less.
constexpr float PLENTIFUL_MANA_DISCOUNT = 0.1f;
/// A shield on a stack the enemy may not reach before it expires at round start.
constexpr float WARD_USE_CHANCE = 0.5f;

const nativeRules::CompiledMetamagicRules * doubleCastRules(const CGHeroInstance * hero)
{
	const auto * rules = hero ? nativeRules::metamagicRules(hero) : nullptr;
	return rules && rules->doubleCastPolicy() ? rules : nullptr;
}

float wardValue(const CPlayerBattleCallback & battle, const std::shared_ptr<HypotheticBattle> & board,
	BattleSide side, const CGHeroInstance & hero, const nativeRules::CompiledMetamagicRules & rules,
	nativeRules::DoubleCastPerkSet perks, DamageCache & damage)
{
	const auto & state = battle.getBattle()->getMetamagicDoubleCastState(side);
	// The completion reads Crescendo after this follow-up's use is spent.
	const int64_t power = int64_t{hero.getPrimSkillLevel(PrimarySkill::SPELL_POWER)}
		+ rules.crescendoSpellPower(state.usesSpent + 1, perks);
	const int amount = rules.arcaneWardShield(static_cast<int>(std::clamp<int64_t>(power, 0,
		std::numeric_limits<int>::max())), perks);
	if(amount <= 0)
		return 0;
	size_t pendingEnemyActions = 0;
	std::vector<float> values;
	for(const auto * unit : board->battleGetAllUnits(false))
	{
		if(!unit || !unit->alive() || unit->isGhost() || unit->isTurret())
			continue;
		if(unit->unitSide() != side)
		{
			if(unit->canMove() && !unit->moved())
				++pendingEnemyActions;
			continue;
		}
		const auto gain = std::min<int64_t>(amount - std::min<int64_t>(amount, unit->getArcaneWardHitPoints()),
			unit->getAvailableHealth());
		if(gain > 0)
			values.push_back(AttackPossibility::calculateDamageReduce(nullptr, unit,
				static_cast<uint64_t>(gain), damage, board));
	}
	// Each remaining enemy activation can strike at most one ward before round start.
	std::sort(values.begin(), values.end(), std::greater<>());
	values.resize(std::min(values.size(), pendingEnemyActions));
	float result = 0;
	for(const auto value : values)
		result += std::max(0.0f, value);
	return result * WARD_USE_CHANCE;
}

float manaValue(const CPlayerBattleCallback & battle, const CGHeroInstance & hero,
	const nativeRules::CompiledMetamagicRules & rules, nativeRules::DoubleCastPerkSet perks, float valuePerMana)
{
	if(valuePerMana <= 0)
		return 0;
	const auto siphonCap = rules.manaSiphonMana(perks);
	int64_t siphon = 0;
	if(siphonCap > 0)
	{
		const auto enemy = battle.battleGetEnemyHero();
		// Visible enemy Mana when detailed; otherwise assume half the cap is payable.
		siphon = enemy.details ? std::clamp<int64_t>(enemy.details->mana, 0, siphonCap) : siphonCap / 2;
	}
	const int64_t restored = siphon + rules.formulaReserveMana(perks);
	const int64_t room = std::max<int64_t>(0, int64_t{hero.manaLimit()} - hero.getNormalSpellPoints());
	const auto gained = std::min(restored, room);
	const bool plentiful = hero.getManaAvailable() > 3 * int64_t{hero.manaLimit()} / 4;
	const float discount = plentiful ? PLENTIFUL_MANA_DISCOUNT : MANA_DISCOUNT;
	// Gained Mana plus half credit for Mana the enemy hero loses.
	return valuePerMana * discount * (static_cast<float>(gained) + 0.5f * static_cast<float>(siphon));
}
}

FollowUpFilter::FollowUpFilter(const CBattleInfoCallback & battle, BattleSide side, const CGHeroInstance * hero)
	: battle(&battle), side(side)
{
	const auto * info = battle.getBattle();
	if(!info || !hero || (side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		|| !metamagicDoubleCast::offerGranted(info->getHeroActionAllowances(side)))
		return;
	open = true;
	firstSpell = info->getMetamagicDoubleCastState(side).firstSpell;
	const auto * rules = doubleCastRules(hero);
	mustDiffer = !rules || rules->followUpMustDiffer(); // Unknown rules: keep the stricter reading.
}

bool FollowUpFilter::paidByOffer(SpellID spell) const
{
	if(!open)
		return false;
	const auto selection = battle->battleGetSpellActionAllowance(side, spell);
	return selection && selection->source == Source::METAMAGIC_DOUBLE_CAST;
}

bool FollowUpFilter::refuses(SpellID spell) const
{
	return open && mustDiffer && spell == firstSpell && paidByOffer(spell);
}

std::vector<SpellID> FollowUpFilter::overCap(const std::vector<const CSpell *> & spells) const
{
	std::vector<std::pair<int, int32_t>> ranked; // (-level, id): highest level first.
	if(open)
		for(const auto * spell : spells)
			if(spell && paidByOffer(spell->getId()))
				ranked.emplace_back(-spell->getLevel(), spell->getId().getNum());
	std::vector<SpellID> result;
	if(ranked.size() <= MAX_FOLLOW_UP_SPELLS)
		return result;
	std::sort(ranked.begin(), ranked.end());
	for(size_t index = MAX_FOLLOW_UP_SPELLS; index < ranked.size(); ++index)
		result.emplace_back(ranked[index].second);
	return result;
}

float completionValue(const CPlayerBattleCallback & battle, const std::shared_ptr<HypotheticBattle> & board,
	BattleSide side, const CGHeroInstance & hero, DamageCache & damage, float valuePerMana, float positive)
{
	const auto * rules = doubleCastRules(&hero);
	if(!rules || !board)
		return 0;
	const auto perks = nativeRules::doubleCastPerks(&hero);
	// Ward HP is raw damage utility; Mana is already priced in scored candidate units.
	const auto value = wardValue(battle, board, side, hero, *rules, perks, damage) * positive
		+ manaValue(battle, hero, *rules, perks, valuePerMana);
	return std::isfinite(value) ? std::max(0.0f, value) : 0.0f;
}

void addCompletionValue(std::vector<PossibleSpellcast> & candidates, const CPlayerBattleCallback & battle,
	const std::shared_ptr<HypotheticBattle> & board, BattleSide side, float baseline, DamageCache & damage, float positive)
{
	const auto * hero = battle.battleGetMyHero();
	const FollowUpFilter filter(battle, side, hero);
	if(!filter.offerOpen() || !doubleCastRules(hero))
		return;
	constexpr float unevaluated = std::numeric_limits<float>::lowest() / 2;
	std::vector<size_t> paid;
	float valuePerMana = 0;
	for(size_t index = 0; index < candidates.size(); ++index)
	{
		const auto & candidate = candidates[index];
		if(candidate.command != HeroCommand::NONE || !candidate.spell || candidate.value <= unevaluated)
			continue;
		const auto cost = battle.battleGetSpellCost(candidate.spell, hero);
		if(cost > 0)
			valuePerMana = std::max(valuePerMana, (candidate.value - baseline) / static_cast<float>(cost));
		if(filter.paidByOffer(candidate.spell->getId()))
			paid.push_back(index);
	}
	if(paid.empty())
		return;
	// One completion follows whichever follow-up is cast; its perks do not depend on the spell.
	const auto bonus = completionValue(battle, board, side, *hero, damage, valuePerMana, positive);
	if(bonus > 0)
		for(const auto index : paid)
			candidates[index].value += bonus;
}
}
