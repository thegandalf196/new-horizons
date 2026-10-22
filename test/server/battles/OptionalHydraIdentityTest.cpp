/*
 * OptionalHydraIdentityTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "BattleTestFixture.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/battle/NewHorizonsPlague.h"
#include "../../../lib/battle/BattleEffectExchange.h"
#include "../../../lib/constants/StringConstants.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/networkPacks/SetStackEffect.h"
#include "../../../lib/spells/HydrasVitalityIdentity.h"
#include "../../../server/CGameHandler.h"
#include <stdexcept>

namespace
{
bool hydraModuleLoaded()
{
	return vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE);
}
}

TEST(OptionalHydraIdentity, LegacyAbsenceMatchesNothingAndRequiredCaptureThrows)
{
	if(hydraModuleLoaded())
		GTEST_SKIP() << "Requires the core-only test profile";
	const Bonus effect(BonusDuration::PERMANENT, BonusType::HP_REGENERATION,
		BonusSource::SPELL_EFFECT, 10, BonusSourceID(SpellID(SpellID::HASTE)));
	EXPECT_EQ(newHorizonsMagic::hydrasVitalitySourceId(JsonNode()), std::nullopt);
	EXPECT_FALSE(newHorizonsMagic::hydrasVitalitySource(JsonNode())(&effect));
	JsonNode empty;
	empty.Struct();
	EXPECT_EQ(newHorizonsMagic::hydrasVitalitySourceId(empty), std::nullopt);
	JsonNode required;
	required["rulesetVersion"].Integer() = 3;
	EXPECT_THROW(newHorizonsMagic::hydrasVitalitySourceId(required), std::runtime_error);
	EXPECT_THROW(newHorizonsMagic::hydrasVitalitySourceId(JsonNode(false)), std::runtime_error);
}

TEST(OptionalHydraIdentity, InstalledHistoricalIdentityPreservesIdAndOrdinarySourcePredicates)
{
	if(!hydraModuleLoaded())
		GTEST_SKIP() << "Requires New Horizons content";
	const SpellID hydra(SpellID::decode("new-horizons:hydrasVitality"));
	const BonusSourceID expected(hydra);
	JsonNode inactive;
	inactive["spells"]["new-horizons:hydrasVitality"]["active"].Bool() = false;
	for(const auto & rules : {JsonNode(), inactive})
	{
		const auto actual = newHorizonsMagic::hydrasVitalitySourceId(rules);
		ASSERT_TRUE(actual);
		EXPECT_EQ(*actual, expected);
		const auto selector = newHorizonsMagic::hydrasVitalitySource(rules);
		const auto oldSelector = Selector::source(BonusSource::SPELL_EFFECT, expected);
		for(const auto source : {BonusSource::SPELL_EFFECT, BonusSource::OTHER, BonusSource::CREATURE_ABILITY})
		{
			for(const auto & sid : {expected, BonusSourceID(SpellID(SpellID::HASTE)), BonusSourceID(CreatureID(hydra.getNum()))})
			{
				for(const auto type : {BonusType::STACK_HEALTH, BonusType::HP_REGENERATION})
				{
					const Bonus effect(BonusDuration::PERMANENT, type, source, 10, sid);
					EXPECT_EQ(selector(&effect), oldSelector(&effect));
					// Exchange historically compares SID without filtering source.
					EXPECT_EQ(effect.sid == *actual, effect.sid == expected);
				}
			}
		}
	}
}

class OptionalHydraBattleTest : public BattleTestFixture
{
protected:
	CStack * subject = nullptr;
	void SetUp() override
	{
		BattleTestFixture::SetUp();
		if(hydraModuleLoaded())
			GTEST_SKIP() << "Requires the core-only test profile";
		startGame();
		startBattle();
		subject = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(leftHex), 10);
		addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(rightHex), 10);
	}
};

TEST_F(OptionalHydraBattleTest, FirstRoundAndActivationRequireNoOptionalHydra)
{
	ASSERT_NO_THROW(beginCombat());
	ASSERT_NE(battle()->battleActiveUnit(), nullptr);
	EXPECT_FALSE(server.stackActivations.empty());
}

TEST_F(OptionalHydraBattleTest, OrdinaryEffectAddUpdateAndRemoveRequireNoOptionalHydra)
{
	Bonus effect(BonusDuration::N_TURNS, BonusType::STACKS_SPEED,
		BonusSource::SPELL_EFFECT, 2, BonusSourceID(SpellID(SpellID::HASTE)));
	effect.turnsRemain = 2;
	SetStackEffect add;
	add.battleID = BattleID(0);
	add.toAdd.emplace_back(subject->unitId(), std::vector<Bonus>{effect});
	ASSERT_NO_THROW(gameHandler->sendAndApply(add));
	const auto selector = Selector::source(BonusSource::SPELL_EFFECT, effect.sid)
		.And(Selector::type()(effect.type));
	ASSERT_TRUE(subject->hasBonus(selector));
	effect.turnsRemain = 3;
	SetStackEffect update;
	update.battleID = BattleID(0);
	update.toUpdate.emplace_back(subject->unitId(), std::vector<Bonus>{effect});
	ASSERT_NO_THROW(gameHandler->sendAndApply(update));
	const auto updated = subject->getBonuses(selector);
	ASSERT_EQ(updated->size(), 1u);
	EXPECT_EQ(updated->front()->turnsRemain, 3);
	SetStackEffect remove;
	remove.battleID = BattleID(0);
	remove.toRemove.emplace_back(subject->unitId(), std::vector<Bonus>{effect});
	ASSERT_NO_THROW(gameHandler->sendAndApply(remove));
	EXPECT_FALSE(subject->hasBonus(selector));
}

TEST_F(OptionalHydraBattleTest, DetachedHealthPreparationNeedsNoOptionalHydraAndPreservesLiveUnit)
{
	const auto snapshot = battle()->captureBattleEffects(subject->unitId());
	const auto before = subject->save();
	battle::PreparedBattleEffectHealth prepared;
	ASSERT_NO_THROW(prepared = battle::prepareBattleEffectHealth(*subject, snapshot, snapshot, battle()->getMagicRules()));
	ASSERT_TRUE(prepared.unit);
	EXPECT_EQ(prepared.unit->save(), before);
	EXPECT_EQ(subject->save(), before);
}

TEST(OptionalPlagueIdentity, LegacyAbsenceMatchesNothingAndRequiredCaptureThrows)
{
	if(vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
		GTEST_SKIP() << "Requires the core-only test profile";
	const Bonus unrelated(BonusDuration::PERMANENT, BonusType::COMBAT_EVENT_TRIGGER, BonusSource::OTHER, 1, BonusSourceID());
	EXPECT_FALSE(newHorizonsPlague::markerSelector(JsonNode())(&unrelated));
	JsonNode empty;
	empty.Struct();
	EXPECT_FALSE(newHorizonsPlague::markerSelector(empty)(&unrelated));
	JsonNode required;
	required["rulesetVersion"].Integer() = 3;
	EXPECT_THROW(newHorizonsPlague::markerSelector(required), std::runtime_error);
	JsonNode malformed(false);
	EXPECT_THROW(newHorizonsPlague::markerSelector(malformed), std::runtime_error);
}

TEST(OptionalPlagueIdentity, InstalledHistoricalIdentityMatchesOriginalPredicate)
{
	if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
		GTEST_SKIP() << "Requires the New Horizons content module";
	const auto plague = SpellID(SpellID::decode(std::string(newHorizonsPlague::SPELL_ID)));
	const auto old = Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(plague))
		.And(Selector::type()(BonusType::COMBAT_EVENT_TRIGGER));
	JsonNode inactive;
	inactive["spells"][std::string(newHorizonsPlague::SPELL_ID)]["active"].Bool() = false;
	for(const auto & rules : {JsonNode(), inactive})
		for(const auto source : {BonusSource::SPELL_EFFECT, BonusSource::OTHER, BonusSource::CREATURE_ABILITY})
			for(const auto & sid : {BonusSourceID(plague), BonusSourceID(SpellID(SpellID::HASTE))})
				for(const auto type : {BonusType::COMBAT_EVENT_TRIGGER, BonusType::HP_REGENERATION})
				{
					const Bonus bonus(BonusDuration::PERMANENT, type, source, 1, sid);
					EXPECT_EQ(newHorizonsPlague::markerSelector(rules)(&bonus), old(&bonus));
				}
}
