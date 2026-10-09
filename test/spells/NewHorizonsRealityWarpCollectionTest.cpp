/*
 * NewHorizonsRealityWarpCollectionTest.cpp, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "../server/battles/HeroCommandFixture.h"
#include "../../lib/CStack.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/bonuses/BonusParameters.h"
#include "../../lib/bonuses/Propagators.h"
#include "../../lib/battle/NewHorizonsConfusionControl.h"
#include "../../lib/modding/CModHandler.h"
#include "../../lib/spells/NewHorizonsRealityWarp.h"

class NewHorizonsRealityWarpCollectionTest : public HeroCommandFixture
{
protected:
	CStack * first = nullptr;
	CStack * second = nullptr;
	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the curated New Horizons preset";
	}
	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
	}
	void prepare(bool shooters = false)
	{
		startGame();
		startBattle();
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
		const auto creature = creatureByName(shooters ? "core:powerLich" : "core:pikeman");
		first = addStack(BattleSide::ATTACKER, creature, BattleHex(4, 5), 20);
		second = addStack(BattleSide::DEFENDER, creature, BattleHex(13, 5), 20);
		ASSERT_NE(first, nullptr);
		ASSERT_NE(second, nullptr);
	}
	Bonus effect(SpellID spell, BonusType type, int value, PlayerColor caster) const
	{
		Bonus bonus(BonusDuration::N_TURNS, type, BonusSource::SPELL_EFFECT, value, BonusSourceID(spell));
		bonus.turnsRemain = 3;
		bonus.spellCasterOwner = caster;
		return bonus;
	}
	newHorizonsRealityWarp::PreparedExchange preview() const
	{
		return newHorizonsRealityWarp::prepareExchange(*battle(), first->unitId(), second->unitId());
	}
};

TEST_F(NewHorizonsRealityWarpCollectionTest, EmptyEndpointsProduceValidNoOpWithOwnHealthGuards)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto result = preview();
	ASSERT_TRUE(result.exchange);
	EXPECT_TRUE(result.previews.empty());
	for(const auto & endpoint : result.exchange->endpoints)
		EXPECT_TRUE(battle::exactBattleEffectSnapshotEqual(endpoint.expected, endpoint.replacement));
}

TEST_F(NewHorizonsRealityWarpCollectionTest, CollectsOpaqueDuplicatesAndExchangesBothDirectionsWithoutLiveMutation)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto haste = effect(SpellID::HASTE, BonusType::STACKS_SPEED, 3, PlayerColor(0));
	const auto shield = effect(SpellID::SHIELD, BonusType::GENERAL_DAMAGE_REDUCTION, 20, PlayerColor(1));
	first->addNewBonus(std::make_shared<Bonus>(haste));
	first->addNewBonus(std::make_shared<Bonus>(haste));
	second->addNewBonus(std::make_shared<Bonus>(shield));
	const auto beforeFirst = first->save();
	const auto beforeSecond = second->save();
	const auto result = preview();
	ASSERT_TRUE(result.exchange);
	ASSERT_EQ(result.previews.size(), 2u);
	EXPECT_EQ(result.previews[0].bundle.bonuses.size(), 2u);
	for(const auto & item : result.previews)
		EXPECT_EQ(item.reason, newHorizonsRealityWarp::StayReason::MOVED);
	EXPECT_EQ(result.exchange->endpoints[0].replacement.effects.size(), 1u);
	EXPECT_EQ(result.exchange->endpoints[1].replacement.effects.size(), 2u);
	for(const auto & endpoint : result.exchange->endpoints)
	{
		EXPECT_EQ(endpoint.expected.recipientHealth, endpoint.replacement.recipientHealth);
		EXPECT_EQ(endpoint.expected.capacityHealthReferenceMax, endpoint.replacement.capacityHealthReferenceMax);
		for(const auto & bonus : endpoint.replacement.effects)
			EXPECT_TRUE(bonus.appliedByEnemy);
	}
	EXPECT_EQ(first->save(), beforeFirst);
	EXPECT_EQ(second->save(), beforeSecond);
}

TEST_F(NewHorizonsRealityWarpCollectionTest, OneNonTransferableComponentHoldsItsWholeLogicalBundle)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto haste = effect(SpellID::HASTE, BonusType::STACKS_SPEED, 3, PlayerColor(0));
	first->addNewBonus(std::make_shared<Bonus>(haste));
	haste.type = BonusType::STACKS_INITIATIVE;
	haste.statusTags = {BonusStatusTag::NON_TRANSFERABLE};
	first->addNewBonus(std::make_shared<Bonus>(haste));
	const auto result = preview();
	ASSERT_TRUE(result.exchange);
	ASSERT_EQ(result.previews.size(), 1u);
	EXPECT_EQ(result.previews.front().reason, newHorizonsRealityWarp::StayReason::EXCLUDED_EFFECT);
	EXPECT_EQ(result.previews.front().bundle.bonuses.size(), 2u);
	EXPECT_TRUE(battle::exactBattleEffectSnapshotEqual(result.exchange->endpoints[0].expected,
		result.exchange->endpoints[0].replacement));
}

TEST_F(NewHorizonsRealityWarpCollectionTest, HypnotizeRequiresCapturedCeilingEvenBeforeRecipientQuery)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto control = effect(SpellID::HYPNOTIZE, BonusType::HYPNOTIZED, 1, PlayerColor(0));
	first->addNewBonus(std::make_shared<Bonus>(control));
	auto result = preview();
	ASSERT_TRUE(result.exchange);
	ASSERT_EQ(result.previews.size(), 1u);
	EXPECT_EQ(result.previews.front().reason, newHorizonsRealityWarp::StayReason::MISSING_CAPTURE);
	first->removeBonuses(Selector::type()(BonusType::HYPNOTIZED));
	JsonNode capture;
	capture["maximumTargetHealth"].Integer() = second->getAvailableHealth() - 1;
	control.parameters = std::make_shared<BonusParameters>(capture);
	first->addNewBonus(std::make_shared<Bonus>(control));
	result = preview();
	ASSERT_TRUE(result.exchange);
	ASSERT_EQ(result.previews.size(), 1u);
	EXPECT_EQ(result.previews.front().reason, newHorizonsRealityWarp::StayReason::ILLEGAL_RECIPIENT);
}

TEST_F(NewHorizonsRealityWarpCollectionTest, ReciprocalGuardianPoolsTravelWithoutSwappingHealth)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const SpellID guardian(SpellID::decode("new-horizons:guardianSpirit"));
	first->addNewBonus(std::make_shared<Bonus>(effect(guardian, BonusType::GUARDIAN_SPIRIT, 1, PlayerColor(0))));
	second->addNewBonus(std::make_shared<Bonus>(effect(guardian, BonusType::GUARDIAN_SPIRIT, 1, PlayerColor(1))));
	first->guardianSpiritHitPoints = 17;
	first->guardianSpiritRoundsRemaining = 3;
	second->guardianSpiritHitPoints = 29;
	second->guardianSpiritRoundsRemaining = 2;
	const auto result = preview();
	ASSERT_TRUE(result.exchange);
	EXPECT_EQ(result.exchange->endpoints[0].replacement.sidecars.guardianSpiritHitPoints, 29);
	EXPECT_EQ(result.exchange->endpoints[0].replacement.sidecars.guardianSpiritRoundsRemaining, 2);
	EXPECT_EQ(result.exchange->endpoints[1].replacement.sidecars.guardianSpiritHitPoints, 17);
	EXPECT_EQ(result.exchange->endpoints[1].replacement.sidecars.guardianSpiritRoundsRemaining, 3);
	EXPECT_EQ(first->guardianSpiritHitPoints, 17);
	EXPECT_EQ(second->guardianSpiritHitPoints, 29);
}

TEST_F(NewHorizonsRealityWarpCollectionTest, SpellLockAndTimeStopRejectEndpointsRatherThanDropEffects)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const SpellID lock(SpellID::decode("new-horizons:spellLock"));
	first->addNewBonus(std::make_shared<Bonus>(effect(lock, BonusType::NONE, 1, PlayerColor(0))));
	auto result = preview();
	EXPECT_FALSE(result.exchange);
	EXPECT_EQ(result.rejection, newHorizonsRealityWarp::EndpointRejection::SPELL_LOCKED);
	first->removeBonuses(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(lock)));
	first->addNewBonus(std::make_shared<Bonus>(effect(SpellID::HASTE, BonusType::TIME_STOP, 1, PlayerColor(0))));
	ASSERT_TRUE(first->isTimeStopped());
	ASSERT_FALSE(first->isValidTarget(false));
	const auto firstHealth = first->getAvailableHealth();
	const auto secondHealth = second->getAvailableHealth();
	result = preview();
	EXPECT_FALSE(result.exchange);
	EXPECT_EQ(result.rejection, newHorizonsRealityWarp::EndpointRejection::TIME_STOPPED);
	EXPECT_TRUE(first->hasBonus(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(SpellID(SpellID::HASTE)))));
	EXPECT_TRUE(first->isTimeStopped());
	EXPECT_EQ(first->getAvailableHealth(), firstHealth);
	EXPECT_EQ(second->getAvailableHealth(), secondHealth);
}

TEST_F(NewHorizonsRealityWarpCollectionTest, FocusMagicRebindsLuaNumericBeneficiaryWithoutRecastingStrength)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true));
	const SpellID focus(SpellID::decode("new-horizons:focusMagic"));
	auto bonus = effect(focus, BonusType::COMBAT_EVENT_TRIGGER, 1345, PlayerColor(0));
	JsonNode captured;
	captured["beneficiarySide"].Float() = 0.0;
	captured["arcaneAcquisition"].Bool() = true;
	bonus.parameters = std::make_shared<BonusParameters>(captured);
	first->addNewBonus(std::make_shared<Bonus>(bonus));
	const auto result = preview();
	ASSERT_TRUE(result.exchange);
	ASSERT_EQ(result.previews.size(), 1u);
	ASSERT_EQ(result.previews.front().reason, newHorizonsRealityWarp::StayReason::MOVED);
	ASSERT_EQ(result.exchange->endpoints[1].replacement.effects.size(), 1u);
	const auto & moved = result.exchange->endpoints[1].replacement.effects.front();
	EXPECT_EQ(moved.val, 1345);
	EXPECT_EQ(moved.turnsRemain, 3);
	EXPECT_EQ(moved.spellCasterOwner, PlayerColor(0));
	ASSERT_TRUE(moved.parameters);
	const auto & parameters = moved.parameters->toCustom<JsonNode>();
	EXPECT_EQ(parameters["beneficiarySide"].Integer(), static_cast<int>(BattleSide::DEFENDER));
	EXPECT_TRUE(parameters["arcaneAcquisition"].Bool());
}

TEST_F(NewHorizonsRealityWarpCollectionTest, SoulChainCannotBecomeItsOwnPrimaryThroughExchange)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const SpellID soulChain(SpellID::decode("new-horizons:soulChain"));
	auto bonus = effect(soulChain, BonusType::COMBAT_EVENT_TRIGGER, 2345, PlayerColor(1));
	JsonNode captured;
	captured["primaryUnitId"].Float() = second->unitId();
	captured["casterSide"].Float() = 1.0;
	captured["mdrPenetration"].Float() = 1250.0;
	bonus.parameters = std::make_shared<BonusParameters>(captured);
	first->addNewBonus(std::make_shared<Bonus>(bonus));
	const auto result = preview();
	ASSERT_TRUE(result.exchange);
	ASSERT_EQ(result.previews.size(), 1u);
	EXPECT_EQ(result.previews.front().reason, newHorizonsRealityWarp::StayReason::INVALID_LINK);
	EXPECT_TRUE(battle::exactBattleEffectSnapshotEqual(result.exchange->endpoints[0].expected,
		result.exchange->endpoints[0].replacement));
}

TEST_F(NewHorizonsRealityWarpCollectionTest, NegativeOriginalSourceCannotMoveOntoInvincibleRecipient)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	first->addNewBonus(std::make_shared<Bonus>(effect(SpellID::SLOW, BonusType::STACKS_INITIATIVE, -25, PlayerColor(1))));
	second->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::INVINCIBLE, BonusSource::OTHER, 1, BonusSourceID()));
	ASSERT_TRUE(second->isInvincible());
	const auto result = preview();
	ASSERT_TRUE(result.exchange);
	ASSERT_EQ(result.previews.size(), 1u);
	EXPECT_EQ(result.previews.front().reason, newHorizonsRealityWarp::StayReason::ILLEGAL_RECIPIENT);
	EXPECT_TRUE(battle::exactBattleEffectSnapshotEqual(result.exchange->endpoints[0].expected,
		result.exchange->endpoints[0].replacement));
}

TEST_F(NewHorizonsRealityWarpCollectionTest, KnownImmunityNegationBypassesNormalMindImmunityNotLucidity)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const SpellID confusion(SpellID::decode("new-horizons:confusion"));
	first->addNewBonus(std::make_shared<Bonus>(newHorizonsConfusionControl::pendingMarker(confusion, PlayerColor(1), true)));
	first->confusionState.applyPending(PlayerColor(1), true);
	second->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::MIND_IMMUNITY, BonusSource::OTHER, 1, BonusSourceID()));
	second->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::NEGATE_ALL_NATURAL_IMMUNITIES, BonusSource::OTHER, 1, BonusSourceID(),
		BonusCustomSubtype::immunityEnemyHero));
	auto result = preview();
	ASSERT_TRUE(result.exchange);
	ASSERT_EQ(result.previews.size(), 1u);
	EXPECT_EQ(result.previews.front().reason, newHorizonsRealityWarp::StayReason::MOVED);
	EXPECT_FALSE(result.exchange->endpoints[0].replacement.sidecars.confusionPending);
	EXPECT_TRUE(result.exchange->endpoints[1].replacement.sidecars.confusionPending);
	EXPECT_EQ(result.exchange->endpoints[1].replacement.sidecars.confusionCaster, PlayerColor(1));
	EXPECT_TRUE(result.exchange->endpoints[1].replacement.sidecars.confusionConfounder);
	second->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::LUCIDITY, BonusSource::OTHER, 1, BonusSourceID()));
	result = preview();
	ASSERT_TRUE(result.exchange);
	ASSERT_EQ(result.previews.size(), 1u);
	EXPECT_EQ(result.previews.front().reason, newHorizonsRealityWarp::StayReason::ILLEGAL_RECIPIENT);
}

TEST_F(NewHorizonsRealityWarpCollectionTest, NonHealthPropagatedComponentHoldsWholeBundleWithoutThrowing)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto bonus = effect(SpellID::HASTE, BonusType::STACKS_SPEED, 3, PlayerColor(0));
	first->addNewBonus(std::make_shared<Bonus>(bonus));
	bonus.type = BonusType::STACKS_INITIATIVE;
	bonus.propagator = std::make_shared<CPropagatorNodeType>(BonusNodeType::HERO);
	first->addNewBonus(std::make_shared<Bonus>(bonus));
	const auto result = preview();
	ASSERT_TRUE(result.exchange);
	ASSERT_EQ(result.previews.size(), 1u);
	EXPECT_EQ(result.previews.front().reason, newHorizonsRealityWarp::StayReason::UNSUPPORTED_CONDITION);
	EXPECT_EQ(result.previews.front().bundle.bonuses.size(), 2u);
	EXPECT_TRUE(battle::exactBattleEffectSnapshotEqual(result.exchange->endpoints[0].expected,
		result.exchange->endpoints[0].replacement));
	EXPECT_TRUE(result.exchange->endpoints[1].replacement.effects.empty());
}
