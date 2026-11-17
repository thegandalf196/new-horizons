/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NewHorizonsMetamagicResolution.h"
#include "NewHorizonsSpellAvailability.h"
#include "CSpell.h"
#include "CSpellHandler.h"
#include "../GameLibrary.h"

namespace newHorizonsMetamagic
{
namespace
{
constexpr uint16_t temporaryDuration = BonusDuration::ONE_BATTLE | BonusDuration::N_TURNS
	| BonusDuration::UNTIL_BEING_ATTACKED | BonusDuration::UNTIL_ATTACK | BonusDuration::STACK_GETS_TURN
	| BonusDuration::COMMANDER_KILLED | BonusDuration::UNTIL_OWN_ATTACK
	| BonusDuration::UNTIL_TAKING_INDIRECT_DAMAGE | BonusDuration::UNTIL_AFTER_ATTACK_SEQUENCE
	| BonusDuration::STACK_ACTIVATION;

bool sameEffect(const RemovalCandidate & first, const RemovalCandidate & second)
{
	return first.recipient == second.recipient && first.family == second.family
		&& first.caster == second.caster && first.statusIdentity == second.statusIdentity
		&& creatureAbility::sameDescriptor(first.abilityDescriptor, second.abilityDescriptor);
}

bool belongsTo(const Bonus & bonus, const RemovalCandidate & candidate)
{
	return bonus.source == BonusSource::SPELL_EFFECT && bonus.spellCasterOwner == candidate.caster
		&& bonus.statusIdentity == candidate.statusIdentity
		&& creatureAbility::sameDescriptor(bonus.abilityDescriptor, candidate.abilityDescriptor)
		&& std::any_of(candidate.bonuses.begin(), candidate.bonuses.end(),
			[&](const Bonus & original) { return original.sid == bonus.sid; });
}

int64_t addMana(int64_t total, int32_t amount)
{
	return total > std::numeric_limits<int64_t>::max() - amount
		? std::numeric_limits<int64_t>::max() : total + amount;
}

int64_t hitPoints(int64_t mana, int multiplier)
{
	if(mana <= 0)
		return 0;
	return mana > std::numeric_limits<int64_t>::max() / multiplier
		? std::numeric_limits<int64_t>::max() : mana * multiplier;
}

int32_t refundRoom(int32_t paid, int32_t refunded, int32_t room)
{
	return std::min(std::max(0, paid) - std::min(std::max(0, paid), std::max(0, refunded)), std::max(0, room));
}
}

std::optional<ResolutionPolicy> resolutionPolicy(const JsonNode & rules)
{
	const auto & policy = rules["metamagicPolicy"];
	if(!policy.isStruct() || policy["version"].getType() != JsonNode::JsonType::DATA_INTEGER)
		return std::nullopt;
	const auto version = policy["version"].Integer();
	if((version != 4 && version != 5 && version != 6) || policy.Struct().size() != (version == 6 ? 8u : version == 5 ? 6u : 5u))
		return std::nullopt;
	const auto exactInteger = [](const JsonNode & value, int32_t expected)
		{ return value.getType() == JsonNode::JsonType::DATA_INTEGER && value.Integer() == expected; };
	const auto exactRanks = [&](const JsonNode & values, const std::array<int32_t, 4> & expected)
	{
		if(!values.isVector() || values.Vector().size() != expected.size())
			return false;
		for(size_t rank = 0; rank < expected.size(); ++rank)
			if(!exactInteger(values.Vector()[rank], expected[rank]))
				return false;
		return true;
	};
	ResolutionPolicy result{{0, 20, 30, 40}, 10, 15, 5};
	if(!exactRanks(policy["removalChancePercent"], result.chance)
		|| !exactInteger(policy["damagePerMana"], result.damagePerMana)
		|| !exactInteger(policy["restorationPerMana"], result.restorationPerMana)
		|| !exactInteger(policy["halonRemovalChanceBonusPercent"], result.halonChanceBonus))
		return std::nullopt;
	if(version >= 5)
	{
		result.overflowPercentByRank = version == 6 ? std::array<int32_t, 4>{0, 30, 45, 60}
			: std::array<int32_t, 4>{0, 25, 50, 75};
		if(!exactRanks(policy["arcaneOverflowPercentByRank"], result.overflowPercentByRank))
			return std::nullopt;
	}
	else if(policy.Struct().contains("arcaneOverflowPercentByRank"))
		return std::nullopt;
	if(version == 6)
	{
		result.optionalRemoval = true;
		result.friendlyDamageReductionPercentByRank = {0, 30, 45, 60};
		result.debuffRetryChancePercentByRank = {0, 30, 45, 60};
		if(!exactRanks(policy["friendlyDamageReductionPercentByRank"], result.friendlyDamageReductionPercentByRank)
			|| !exactRanks(policy["debuffRetryChancePercentByRank"], result.debuffRetryChancePercentByRank))
			return std::nullopt;
	}
	return result;
}

std::optional<EnergyDefinition> energyDefinition(const JsonNode & rules, SpellID spell)
{
	if(!spell.hasValue() || !LIBRARY || !LIBRARY->spellh
		|| static_cast<size_t>(spell.getNum()) >= LIBRARY->spellh->objects.size())
		return std::nullopt;
	try
	{
		const auto family = newHorizonsMagic::spellVariantBase(rules, spell);
		if(!family.hasValue() || static_cast<size_t>(family.getNum()) >= LIBRARY->spellh->objects.size())
			return std::nullopt;
		const auto * definition = family.toSpell();
		if(!definition)
			return std::nullopt;
		const auto & row = rules["spells"][definition->getJsonKey()];
		const auto & costs = row["costs"];
		if(!costs.isVector() || costs.Vector().size() != 4
			|| costs.Vector().front().getType() != JsonNode::JsonType::DATA_INTEGER
			|| costs.Vector().front().Integer() <= 0 || costs.Vector().front().Integer() > std::numeric_limits<int32_t>::max())
			return std::nullopt;
		return EnergyDefinition{family, static_cast<int32_t>(costs.Vector().front().Integer()), definition->isMagical(),
			definition->isPositive(), definition->isNegative(), definition->isCommonHeroSpell()};
	}
	catch(const std::exception &)
	{
		return std::nullopt;
	}
}

int removalChancePercent(const ResolutionPolicy & policy, int rank, bool halon)
{
	return rank >= 1 && rank <= 3 ? std::clamp(policy.chance[rank] + (halon ? policy.halonChanceBonus : 0), 0, 100) : 0;
}

int64_t energyHitPoints(int64_t removedMana, int32_t multiplier)
{
	return multiplier > 0 ? hitPoints(removedMana, multiplier) : 0;
}

std::vector<RemovalCandidate> collectRemovalCandidates(uint32_t recipient, PlayerColor recipientOwner,
	const battle::BattleEffectSnapshot & snapshot, ResolutionKind kind, const EnergyResolver & resolver)
{
	std::vector<RemovalCandidate> groups;
	if(!resolver)
		return groups;
	for(const auto & bonus : snapshot.effects)
	{
		if(bonus.source != BonusSource::SPELL_EFFECT)
			continue;
		if(bonus.abilityDescriptor && bonus.abilityDescriptor->family != creatureAbility::Family::HERO_SPELL
			&& bonus.abilityDescriptor->family != creatureAbility::Family::CREATURE_SPELL) continue;
		const auto spell = bonus.sid.as<SpellID>();
		const auto definition = resolver(spell);
		if(!definition || !definition->family.hasValue() || definition->listedMana <= 0
			|| !definition->magical || !definition->commonHeroSpell
			|| (kind == ResolutionKind::OFFENSE ? !definition->positive : !definition->negative))
			continue;
		const RemovalCandidate identity{recipient, spell, definition->family,
			bonus.spellCasterOwner, bonus.statusIdentity, definition->listedMana, {}, bonus.abilityDescriptor};
		auto found = std::find_if(groups.begin(), groups.end(), [&](const auto & group) { return sameEffect(group, identity); });
		if(found == groups.end())
		{
			groups.push_back(identity);
			found = std::prev(groups.end());
		}
		found->bonuses.push_back(bonus);
	}
	std::erase_if(groups, [&](auto & group)
	{
		if(kind == ResolutionKind::RESTORATION
			&& (recipientOwner == PlayerColor::CANNOT_DETERMINE || group.caster == PlayerColor::CANNOT_DETERMINE
				|| group.caster == recipientOwner))
			return true;
		if(std::any_of(group.bonuses.begin(), group.bonuses.end(), [](const Bonus & bonus)
		{
			return (bonus.duration & temporaryDuration) == 0 || (bonus.duration & BonusDuration::PERMANENT) != 0
				|| bonus.type == BonusType::PHYSICAL_AFFLICTION;
		}))
			return true;
		return false;
	});
	return groups;
}

int unravelChancePercent(int rank)
{
	return rank >= 1 && rank <= 3 ? 10 + 10 * rank : 0;
}

int64_t dischargeHitPoints(int64_t removedMana) { return energyHitPoints(removedMana, 10); }
int64_t restorationHitPoints(int64_t removedMana) { return energyHitPoints(removedMana, 15); }

int32_t recyclingRefund(int64_t removedMana, int32_t actualPaid, int32_t alreadyRefunded, int32_t manaRoom)
{
	const auto amount = std::min<int64_t>(std::max<int64_t>(0, removedMana) / 4, std::max(0, actualPaid) / 2);
	return static_cast<int32_t>(std::min<int64_t>(amount, refundRoom(actualPaid, alreadyRefunded, manaRoom)));
}

int32_t conservedFormulaRefund(int32_t actualPaid, int32_t alreadyRefunded, int32_t manaRoom,
	bool validSingleTarget, bool completelyResisted)
{
	return validSingleTarget && completelyResisted ? refundRoom(actualPaid, alreadyRefunded, manaRoom) : 0;
}

ResolutionLedger::ResolutionLedger(bool originalHeroCast) : enabled(originalHeroCast) {}

bool ResolutionLedger::attempt(const RemovalCandidate & candidate, int rank, const std::function<bool(int)> & roll)
{
	return attemptPercent(candidate, unravelChancePercent(rank), roll);
}

bool ResolutionLedger::attemptPercent(const RemovalCandidate & candidate, int chance, const std::function<bool(int)> & roll)
{
	if(!enabled || chance <= 0 || chance > 100 || candidate.listedMana <= 0 || candidate.bonuses.empty() || !roll
		|| std::any_of(attempts.begin(), attempts.end(), [&](const auto & entry) { return sameEffect(candidate, entry.candidate); }))
		return false;
	const bool successful = roll(chance);
	attempts.push_back({candidate, successful, false});
	return successful;
}

bool ResolutionLedger::confirmRemoval(const RemovalCandidate & candidate,
	const battle::BattleEffectSnapshot & before, const battle::BattleEffectSnapshot & after)
{
	auto found = std::find_if(attempts.begin(), attempts.end(), [&](const auto & entry) { return sameEffect(candidate, entry.candidate); });
	if(found == attempts.end() || !found->successful || found->credited)
		return false;
	std::vector<Bonus> original;
	for(const auto & bonus : before.effects)
		if(belongsTo(bonus, found->candidate))
			original.push_back(bonus);
	if(!battle::exactBattleEffectsEqual(original, found->candidate.bonuses)
		|| std::any_of(after.effects.begin(), after.effects.end(), [&](const Bonus & bonus) { return belongsTo(bonus, found->candidate); }))
		return false;
	found->credited = true;
	return true;
}

int64_t ResolutionLedger::removedMana() const
{
	int64_t result = 0;
	for(const auto & entry : attempts)
		if(entry.credited)
			result = addMana(result, entry.candidate.listedMana);
	return result;
}

int64_t ResolutionLedger::removedMana(uint32_t recipient) const
{
	int64_t result = 0;
	for(const auto & entry : attempts)
		if(entry.credited && entry.candidate.recipient == recipient)
			result = addMana(result, entry.candidate.listedMana);
	return result;
}
}
