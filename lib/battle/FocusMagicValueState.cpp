/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "FocusMagicValueState.h"
#include <stdexcept>

namespace newHorizonsFocusMagic
{
namespace
{
struct RecordTransition
{
	std::vector<Bonus> retained;
	Consumption effects;
};
RecordTransition transitionRecords(std::span<const Bonus> records, int remaining)
{
	RecordTransition result;
	if(remaining <= 0)
	{
		result.retained.assign(records.begin(), records.end());
		return result;
	}
	for(const auto & record : records)
	{
		if(remaining == 1 || isChargeMarker(record)) result.effects.remove.push_back(record);
		else result.retained.push_back(record);
		if(remaining > 1 && isChargeMarker(record) && result.effects.add.empty())
		{
			auto next = record;
			next.val = remaining - 1;
			result.effects.add.push_back(std::move(next));
		}
	}
	result.retained.insert(result.retained.end(), result.effects.add.begin(), result.effects.add.end());
	return result;
}
void validateDerived(const ValueState & state)
{
	const auto expected = valueState(state.records);
	if(expected.charges != state.charges || expected.converted != state.converted)
		throw std::invalid_argument("Inconsistent Focus value state");
}
}

ValueState valueState(std::vector<Bonus> records)
{
	ValueState result;
	if(records.empty()) return result;
	if(records.size() > 3) throw std::invalid_argument("Unsupported Focus record count");
	int markers = 0, shooters = 0, damage = 0, damagePercent = 0;
	for(const auto & record : records)
	{
		if(!isEnchantmentRecord(record) || record.stacking != SPELL
			|| record.duration != BonusDuration::ONE_BATTLE || record.turnsRemain != 0
			|| record.valType != BonusValueType::ADDITIVE_VALUE
			|| record.targetSourceType != BonusSource::OTHER || record.effectRange != BonusLimitEffect::NO_LIMIT
			|| record.parameters || record.limiter || record.updater || record.propagator || record.propagationUpdater
			|| !record.hasValidStatusMetadata() || !Bonus::isValidSpellCasterOwner(record.spellCasterOwner)
			|| !Bonus::isValidSpellCasterOwner(record.bonusOwner))
			throw std::invalid_argument("Unsupported Focus record shape");
		if(isChargeMarker(record) && record.subtype == BonusSubtypeID())
		{
			++markers;
			result.charges = record.val;
		}
		else if(record.type == BonusType::SHOOTER && record.val == 1 && record.subtype == BonusSubtypeID())
			++shooters;
		else if(record.type == BonusType::PERCENTAGE_DAMAGE_BOOST && record.val > 0 && record.val <= 120
			&& record.subtype == BonusSubtypeID(BonusCustomSubtype::damageTypeRanged))
		{
			++damage;
			damagePercent = record.val;
		}
		else throw std::invalid_argument("Unsupported Focus effect component");
	}
	if(markers != 1 || shooters > 1 || damage > 1
		|| (shooters == 0 && (damage != 1 || damagePercent < 20))
		|| (shooters != 0 && damagePercent > 100))
		throw std::invalid_argument("Incomplete Focus enchantment");
	result.converted = shooters != 0;
	result.records = std::move(records);
	return result;
}

std::optional<ValueState> captureValueState(const battle::Unit & unit)
{
	try { return valueState(nativeRecords(unit)); }
	catch(const std::invalid_argument &) { return std::nullopt; }
}

Consumption consumeRecords(std::span<const Bonus> records, int remaining)
{
	return transitionRecords(records, remaining).effects;
}

StrikeTransition prepareStrike(const ValueState & before, bool accepted, bool ranged)
{
	validateDerived(before);
	StrikeTransition result;
	result.after = before;
	result.focusRanged = accepted && ranged && before.charges > 0;
	result.convertedRanged = result.focusRanged && before.converted;
	result.spendNativeAmmo = spendsNativeAmmo(accepted, ranged, result.convertedRanged);
	if(!result.focusRanged) return result;
	auto changed = transitionRecords(before.records, before.charges);
	result.after = valueState(std::move(changed.retained));
	result.effects = std::move(changed.effects);
	return result;
}

std::vector<Bonus> enchantment(bool nativeShooter, SpellID spell, int32_t additionalRangedPercent)
{
	if(additionalRangedPercent < 0 || additionalRangedPercent > 100)
		throw std::invalid_argument("Invalid Focus Magic ranged enhancement");
	Bonus marker(BonusDuration::ONE_BATTLE, BonusType::NONE, BonusSource::SPELL_EFFECT,
		MAX_CHARGES, BonusSourceID(spell));
	marker.stacking = SPELL;
	std::vector<Bonus> result{marker};
	if(!nativeShooter)
	{
		Bonus shooter(BonusDuration::ONE_BATTLE, BonusType::SHOOTER,
			BonusSource::SPELL_EFFECT, 1, BonusSourceID(spell));
		shooter.stacking = SPELL;
		result.push_back(std::move(shooter));
	}
	const int enhancement = (nativeShooter ? 20 : 0) + additionalRangedPercent;
	if(enhancement > 0)
	{
		Bonus damage(BonusDuration::ONE_BATTLE, BonusType::PERCENTAGE_DAMAGE_BOOST,
			BonusSource::SPELL_EFFECT, enhancement, BonusSourceID(spell));
		damage.stacking = SPELL;
		damage.subtype = BonusSubtypeID(BonusCustomSubtype::damageTypeRanged);
		result.push_back(std::move(damage));
	}
	return result;
}

EnchantmentTransition prepareEnchantment(const ValueState & before, bool nativeShooter,
	SpellID spell, int32_t additionalRangedPercent)
{
	validateDerived(before);
	EnchantmentTransition result;
	result.effects.remove = before.records;
	result.effects.add = enchantment(nativeShooter, spell, additionalRangedPercent);
	result.after = valueState(result.effects.add);
	return result;
}
}
