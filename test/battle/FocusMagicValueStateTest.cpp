/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "mock/mock_battle_Unit.h"
#include "../../lib/battle/FocusMagicValueState.h"
#include "../../lib/bonuses/BonusList.h"
#include "../../lib/bonuses/BonusParameters.h"
#include "../../lib/json/JsonNode.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include "../../lib/CRandomGenerator.h"
#include <algorithm>
#include <memory>
#include <stdexcept>

namespace
{
using namespace newHorizonsFocusMagic;
class ValueUnit final : public testing::NiceMock<UnitMock>
{
public:
	std::vector<std::shared_ptr<Bonus>> rows;
	mutable int reads = 0;
	bool forbidReads = false;
	TConstBonusListPtr getAllBonuses(const CSelector & selector, const std::string &) const override
	{
		if(forbidReads) throw std::logic_error("Forecast queried native Unit");
		++reads;
		auto result = std::make_shared<BonusList>();
		for(const auto & row : rows)
			if(!selector || selector(row.get())) result->push_back(row);
		return result;
	}
	void grant(const std::vector<Bonus> & records)
	{
		for(const auto & record : records) rows.push_back(std::make_shared<Bonus>(record));
	}
	void apply(const Consumption & receipt)
	{
		for(const auto & removed : receipt.remove)
			std::erase_if(rows, [&](const auto & row)
			{
				return row->source == removed.source && row->sid == removed.sid && row->type == removed.type
					&& row->subtype == removed.subtype && row->stacking == removed.stacking && row->val == removed.val;
			});
		grant(receipt.add);
	}
};
SpellID focus() { return SpellID(SpellID::decode(SPELL)); }
void sameRecords(const std::vector<Bonus> & expected, const std::vector<Bonus> & actual)
{
	ASSERT_EQ(expected.size(), actual.size());
	for(size_t index = 0; index < expected.size(); ++index)
	{
		EXPECT_EQ(expected[index].toJsonNode(), actual[index].toJsonNode());
		EXPECT_EQ(expected[index].spellCasterOwner, actual[index].spellCasterOwner);
		EXPECT_EQ(expected[index].bonusOwner, actual[index].bonusOwner);
		EXPECT_EQ(expected[index].appliedByEnemy, actual[index].appliedByEnemy);
		EXPECT_EQ(expected[index].hidden, actual[index].hidden);
		EXPECT_EQ(expected[index].statusTags, actual[index].statusTags);
		EXPECT_EQ(expected[index].statusIdentity, actual[index].statusIdentity);
		EXPECT_EQ(expected[index].turnsRemain, actual[index].turnsRemain);
	}
}
std::vector<std::byte> rngState()
{
	CMemorySerializer memory;
	CRandomGenerator::getDefault().serialize(memory.oser);
	return memory.extractBuffer();
}
}

TEST(FocusMagicValueStateTest, ThreeConvertedAcceptedStrikesMatchNativeIncludingFinalShot)
{
	ValueUnit unit;
	unit.grant(enchantment(unit, focus()));
	auto captured = captureValueState(unit);
	ASSERT_TRUE(captured);
	auto state = *captured;
	for(int remaining = 2; remaining >= 0; --remaining)
	{
		const auto typed = prepareStrike(state, true, true); // Includes miss/zero damage: no HP input.
		const auto native = consume(unit);
		sameRecords(native.remove, typed.effects.remove);
		sameRecords(native.add, typed.effects.add);
		EXPECT_TRUE(typed.focusRanged);
		EXPECT_TRUE(typed.convertedRanged); // Still ranged on the last charge.
		EXPECT_FALSE(typed.spendNativeAmmo);
		EXPECT_EQ(typed.after.charges, remaining);
		unit.apply(native);
		EXPECT_EQ(charges(unit), remaining);
		sameRecords(nativeRecords(unit), typed.after.records);
		state = typed.after;
	}
	EXPECT_FALSE(state.converted);
	EXPECT_TRUE(state.records.empty());
}

TEST(FocusMagicValueStateTest, NativeShootersSpendOrdinaryAmmoWithoutFocusRefillOnRecast)
{
	ValueUnit unit;
	unit.rows.push_back(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::SHOOTER,
		BonusSource::CREATURE_ABILITY, 1, BonusSourceID(CreatureID(0))));
	unit.grant(enchantment(unit, focus(), 10));
	auto state = *captureValueState(unit);
	int usedOrdinaryAmmo = 7;
	for(int index = 0; index < 3; ++index)
	{
		const auto next = prepareStrike(state, true, true);
		EXPECT_FALSE(next.convertedRanged);
		EXPECT_TRUE(next.spendNativeAmmo);
		usedOrdinaryAmmo += next.spendNativeAmmo ? 1 : 0;
		unit.apply(consume(unit));
		sameRecords(nativeRecords(unit), next.after.records);
		state = next.after;
	}
	EXPECT_EQ(usedOrdinaryAmmo, 10);
	const auto recast = prepareEnchantment(state, true, focus(), 10);
	EXPECT_EQ(recast.after.charges, 3);
	EXPECT_FALSE(recast.after.converted);
	EXPECT_EQ(usedOrdinaryAmmo, 10); // Effect-only receipt has no ammo transaction.
	sameRecords(enchantment(unit, focus(), 10), recast.effects.add);
}

TEST(FocusMagicValueStateTest, RejectedOrMeleeStrikeConsumesNothingAndRecastRestoresOnlyThree)
{
	auto state = valueState(enchantment(false, focus(), 20));
	for(const auto [accepted, ranged] : {std::pair{false, true}, std::pair{true, false}})
	{
		const auto next = prepareStrike(state, accepted, ranged);
		EXPECT_EQ(next.after.charges, 3);
		EXPECT_TRUE(next.effects.remove.empty());
		EXPECT_TRUE(next.effects.add.empty());
		EXPECT_FALSE(next.spendNativeAmmo);
	}
	state = prepareStrike(state, true, true).after;
	const auto recast = prepareEnchantment(state, false, focus());
	EXPECT_EQ(recast.after.charges, 3);
	EXPECT_TRUE(recast.after.converted);
	sameRecords(recast.effects.remove, state.records);
	EXPECT_EQ(recast.effects.add.size(), 2u); // Marker + shooter; no inherited melee abilities or ammo.
}

TEST(FocusMagicValueStateTest, OwnedBranchesOutliveNativeUnitAndDoNotUseRng)
{
	const auto randomBefore = rngState();
	auto unit = std::make_unique<ValueUnit>();
	unit->grant(enchantment(false, focus(), 10));
	const auto parent = *captureValueState(*unit);
	const auto before = parent.records;
	unit->forbidReads = true;
	auto child = prepareStrike(parent, true, true).after;
	unit.reset();
	const auto grandchild = prepareStrike(child, true, true).after;
	EXPECT_EQ(parent.charges, 3);
	EXPECT_EQ(child.charges, 2);
	EXPECT_EQ(grandchild.charges, 1);
	sameRecords(parent.records, before);
	EXPECT_EQ(rngState(), randomBefore);
}

TEST(FocusMagicValueStateTest, AbsentIsValidButMalformedOrBorrowedPayloadRequiresFallback)
{
	ValueUnit unit;
	ASSERT_TRUE(captureValueState(unit));
	EXPECT_EQ(captureValueState(unit)->charges, 0);
	EXPECT_TRUE(prepareStrike(*captureValueState(unit), true, true).spendNativeAmmo);
	unit.grant(enchantment(false, focus()));
	unit.rows.front()->parameters = std::make_shared<BonusParameters>(std::vector<int32_t>{1});
	EXPECT_FALSE(captureValueState(unit));
	const auto native = consume(unit); // Native compatibility remains on the same record algorithm.
	ASSERT_EQ(native.add.size(), 1u);
	EXPECT_EQ(native.add.front().val, 2);
	EXPECT_EQ(native.add.front().parameters, unit.rows.front()->parameters);
	auto malformed = enchantment(false, focus());
	malformed.front().val = 4;
	EXPECT_THROW(valueState(malformed), std::invalid_argument);
	auto inconsistent = valueState(enchantment(false, focus()));
	inconsistent.charges = 1;
	EXPECT_THROW(prepareStrike(inconsistent, true, true), std::invalid_argument);
}

TEST(FocusMagicValueStateTest, MetamagicCapabilitiesExpireWithTheFinalFocusCharge)
{
	auto records = enchantment(false, focus());
	for(const auto type : {BonusType::FLYING, BonusType::FREE_SHOOTING})
	{
		auto capability = records.front();
		capability.type = type;
		capability.val = 1;
		records.push_back(std::move(capability));
	}
	auto state = valueState(records);
	ASSERT_EQ(state.records.size(), 4u);
	for(int shot = 0; shot < 2; ++shot)
	{
		state = prepareStrike(state, true, true).after;
		EXPECT_EQ(state.records.size(), 4u);
	}
	const auto last = prepareStrike(state, true, true);
	EXPECT_EQ(last.effects.remove.size(), 4u);
	EXPECT_TRUE(last.after.records.empty());
	EXPECT_EQ(last.after.charges, 0);
}
