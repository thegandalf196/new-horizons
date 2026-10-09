/*
 * NewHorizonsRealityWarpTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "NewHorizonsElementalTerrainFixture.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/NewHorizonsRealityWarp.h"
#include "../../../lib/battle/NewHorizonsConfusionControl.h"
#include "../../../lib/battle/BattleEffectExchange.h"
#include "../../../lib/spells/BattleSpellMechanics.h"
#include "../../../lib/networkPacks/SetStackEffect.h"
#include "../../mock/mock_ServerCallback.h"
#include "../../mock/mock_vstd_RNG.h"

namespace
{
class NewHorizonsRealityWarpTest : public NewHorizonsElementalTerrainFixture
{
protected:
	CStack * friendly = nullptr;
	CStack * enemy = nullptr;
	CStack * other = nullptr;
	bool useLegacyMagicRules = false;
	static SpellID spell() { return SpellID(SpellID::decode("new-horizons:realityWarp")); }
	void mapLoaded(CMap * loaded) override
	{
		NewHorizonsElementalTerrainFixture::mapLoaded(loaded);
		if(useLegacyMagicRules)
			loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, JsonNode());
	}
	void prepare(bool breaker = false, bool legacyBattleRules = false)
	{
		useLegacyMagicRules = legacyBattleRules;
		startGame();
		ASSERT_TRUE(spell().hasValue());
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		attackerSideHero->addSpellToSpellbook(spell());
		const SecondarySkill chaos(SecondarySkill::decode("new-horizons:chaosMagic"));
		attackerSideHero->setSecSkillLevel(chaos, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
		if(breaker)
		{
			attackerSideHero->applyPerkSelection({"new-horizons:chaosMagic", "new-horizons:chaosMagic.misfortuneWeaver"});
			attackerSideHero->applyPerkSelection({"new-horizons:chaosMagic", "new-horizons:chaosMagic.realityBreaker"});
			ASSERT_TRUE(attackerSideHero->hasActivePerk("new-horizons:chaosMagic", "new-horizons:chaosMagic.realityBreaker"));
		}
		attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
		setTestSpellPointTotal(attackerSideHero, 1000);
		startTerrainBattle(TerrainId::GRASS);
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
		friendly = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(3, 5), 1000);
		enemy = addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), BattleHex(12, 5), 1000);
		other = addStack(BattleSide::ATTACKER, creatureByName("core:peasant"), BattleHex(3, 8), 1000);
		ASSERT_NE(friendly, nullptr);
		ASSERT_NE(enemy, nullptr);
		ASSERT_NE(other, nullptr);
		beginCombat();
		ASSERT_EQ(battle()->battleActiveUnit(), friendly);
	}

	Bonus effect(SpellID source, BonusType type, int value, PlayerColor caster, int rounds = 3)
	{
		Bonus result(BonusDuration::N_TURNS, type, BonusSource::SPELL_EFFECT, value, BonusSourceID(source));
		result.turnsRemain = rounds;
		result.spellCasterOwner = caster;
		return result;
	}
	bool cast(const battle::Unit * first, const battle::Unit * second)
	{
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = BattleSide::ATTACKER;
		action.spell = spell();
		action.aimToUnit(first);
		action.aimToUnit(second);
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action);
	}
	void expectCommitted(const battle::BattleEffectExchange & planned, CStack * first, CStack * second)
	{
		const auto after = newHorizonsRealityWarp::prepareExchange(*battle(), first->unitId(), second->unitId());
		ASSERT_TRUE(after.exchange);
		for(size_t index = 0; index < 2; ++index)
			EXPECT_TRUE(battle::exactBattleEffectSnapshotEqual(planned.endpoints[index].replacement,
				after.exchange->endpoints[index].expected));
	}
};
}

TEST_F(NewHorizonsRealityWarpTest, PaidRegisteredReciprocalBundlesPreserveStrengthTimersProvenanceAndHealth)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	friendly->addNewBonus(std::make_shared<Bonus>(effect(SpellID::HASTE, BonusType::STACKS_SPEED, 7, PlayerColor(0), 4)));
	friendly->addNewBonus(std::make_shared<Bonus>(effect(SpellID::BLESS, BonusType::ALWAYS_MAXIMUM_DAMAGE, 1, PlayerColor(0), 2)));
	enemy->addNewBonus(std::make_shared<Bonus>(effect(SpellID::SLOW, BonusType::STACKS_INITIATIVE, -37, PlayerColor(1), 5)));
	const auto planned = newHorizonsRealityWarp::prepareExchange(*battle(), friendly->unitId(), enemy->unitId());
	ASSERT_TRUE(planned.exchange);
	ASSERT_EQ(planned.previews.size(), 3u);
	for(const auto & item : planned.previews)
		ASSERT_EQ(item.reason, newHorizonsRealityWarp::StayReason::MOVED);
	const auto mana = attackerSideHero->getManaAvailable();
	EXPECT_EQ(spell().toSpell()->getLevel(), 4);
	EXPECT_EQ(battle()->battleGetSpellCost(spell().toSpell(), attackerSideHero), 15);
	ASSERT_TRUE(cast(friendly, enemy));
	EXPECT_EQ(mana - attackerSideHero->getManaAvailable(), 15);
	ASSERT_NO_FATAL_FAILURE(expectCommitted(*planned.exchange, friendly, enemy));
	EXPECT_FALSE(cast(friendly, enemy)) << "Accepted cast spends the hero action";
}

TEST_F(NewHorizonsRealityWarpTest, IllegalRecipientHoldsWholeSourceBundleWhileLegalReciprocalEffectMoves)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	friendly->addNewBonus(std::make_shared<Bonus>(effect(SpellID::HASTE, BonusType::STACKS_SPEED, 7, PlayerColor(0))));
	enemy->addNewBonus(std::make_shared<Bonus>(effect(SpellID::SLOW, BonusType::STACKS_INITIATIVE, -25, PlayerColor(1))));
	enemy->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::SPELL_IMMUNITY,
		BonusSource::OTHER, 1, BonusSourceID(), BonusSubtypeID(SpellID(SpellID::HASTE))));
	const auto planned = newHorizonsRealityWarp::prepareExchange(*battle(), friendly->unitId(), enemy->unitId());
	ASSERT_TRUE(planned.exchange);
	ASSERT_EQ(planned.previews.size(), 2u);
	EXPECT_EQ(planned.previews[0].reason, newHorizonsRealityWarp::StayReason::ILLEGAL_RECIPIENT);
	EXPECT_EQ(planned.previews[1].reason, newHorizonsRealityWarp::StayReason::MOVED);
	ASSERT_TRUE(cast(friendly, enemy));
	ASSERT_NO_FATAL_FAILURE(expectCommitted(*planned.exchange, friendly, enemy));
}

TEST_F(NewHorizonsRealityWarpTest, DuplicateSameSideAndRemovedUnitRequestsRejectWithoutChargeOrMutation)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto mana = attackerSideHero->getManaAvailable();
	const auto first = friendly->save();
	const auto second = enemy->save();
	EXPECT_FALSE(cast(friendly, friendly));
	EXPECT_FALSE(cast(friendly, other));
	BattleAction stale;
	stale.actionType = EActionType::HERO_SPELL;
	stale.side = BattleSide::ATTACKER;
	stale.spell = spell();
	stale.aimToUnit(friendly);
	stale.aimToUnit(other);
	BattleUnitsChanged remove;
	remove.battleID = BattleID(0);
	remove.changedStacks.emplace_back(other->unitId(), UnitChanges::EOperation::REMOVE);
	gameHandler->sendAndApply(remove);
	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), stale));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana);
	EXPECT_EQ(friendly->save(), first);
	EXPECT_EQ(enemy->save(), second);
	EXPECT_TRUE(cast(friendly, enemy)) << "Rejected requests must not spend hero action";
}

TEST_F(NewHorizonsRealityWarpTest, RealityBreakerAllowsDistinctFriendlyPairAndStillRejectsDuplicate)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true));
	friendly->addNewBonus(std::make_shared<Bonus>(effect(SpellID::HASTE, BonusType::STACKS_SPEED, 9, PlayerColor(0))));
	const auto planned = newHorizonsRealityWarp::prepareExchange(*battle(), friendly->unitId(), other->unitId());
	ASSERT_TRUE(planned.exchange);
	const auto mana = attackerSideHero->getManaAvailable();
	EXPECT_FALSE(cast(friendly, friendly));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana);
	ASSERT_TRUE(cast(friendly, other));
	EXPECT_EQ(mana - attackerSideHero->getManaAvailable(), 15);
	ASSERT_NO_FATAL_FAILURE(expectCommitted(*planned.exchange, friendly, other));
}

TEST_F(NewHorizonsRealityWarpTest, LockedAndTimeStoppedEndpointsRejectWithoutCharge)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto mana = attackerSideHero->getManaAvailable();
	const SpellID lock(SpellID::decode("new-horizons:spellLock"));
	enemy->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::MAGIC_RESISTANCE,
		BonusSource::SPELL_EFFECT, 100, BonusSourceID(lock)));
	EXPECT_FALSE(cast(friendly, enemy));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana);
	enemy->removeBonuses(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(lock)));
	enemy->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::TIME_STOP,
		BonusSource::OTHER, 1, BonusSourceID()));
	EXPECT_FALSE(cast(friendly, enemy));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana);
}

TEST_F(NewHorizonsRealityWarpTest, PaidGuardianPoolsExchangeWithoutSwappingHealth)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const SpellID guardian(SpellID::decode("new-horizons:guardianSpirit"));
	friendly->addNewBonus(std::make_shared<Bonus>(effect(guardian, BonusType::GUARDIAN_SPIRIT, 1, PlayerColor(0))));
	enemy->addNewBonus(std::make_shared<Bonus>(effect(guardian, BonusType::GUARDIAN_SPIRIT, 1, PlayerColor(1))));
	friendly->guardianSpiritHitPoints = 17;
	friendly->guardianSpiritRoundsRemaining = 3;
	enemy->guardianSpiritHitPoints = 29;
	enemy->guardianSpiritRoundsRemaining = 2;
	const auto hp = std::array<int64_t, 2>{friendly->getAvailableHealth(), enemy->getAvailableHealth()};
	ASSERT_TRUE(cast(friendly, enemy));
	EXPECT_EQ(friendly->guardianSpiritHitPoints, 29);
	EXPECT_EQ(friendly->guardianSpiritRoundsRemaining, 2);
	EXPECT_EQ(enemy->guardianSpiritHitPoints, 17);
	EXPECT_EQ(enemy->guardianSpiritRoundsRemaining, 3);
	EXPECT_EQ(friendly->getAvailableHealth(), hp[0]);
	EXPECT_EQ(enemy->getAvailableHealth(), hp[1]);
}

TEST_F(NewHorizonsRealityWarpTest, PaidConfusionPendingSidecarMovesWithMarkerNotHistory)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const SpellID confusion(SpellID::decode("new-horizons:confusion"));
	friendly->addNewBonus(std::make_shared<Bonus>(newHorizonsConfusionControl::pendingMarker(confusion, PlayerColor(1), true)));
	friendly->confusionState.applyPending(PlayerColor(1), true);
	const auto planned = newHorizonsRealityWarp::prepareExchange(*battle(), friendly->unitId(), enemy->unitId());
	ASSERT_TRUE(planned.exchange);
	ASSERT_TRUE(planned.exchange->endpoints[1].replacement.sidecars.confusionPending);
	ASSERT_TRUE(cast(friendly, enemy));
	ASSERT_NO_FATAL_FAILURE(expectCommitted(*planned.exchange, friendly, enemy));
}

TEST_F(NewHorizonsRealityWarpTest, PaidRegenerationCarriesFractionalProgressWithoutHealing)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const SpellID regeneration(SpellID::decode("new-horizons:regeneration"));
	friendly->addNewBonus(std::make_shared<Bonus>(effect(regeneration, BonusType::HP_REGENERATION, 10, PlayerColor(0))));
	friendly->regenerationRateMillionths = 12345;
	friendly->regenerationPendingMicroHealth = 54321;
	const auto planned = newHorizonsRealityWarp::prepareExchange(*battle(), friendly->unitId(), enemy->unitId());
	ASSERT_TRUE(planned.exchange);
	ASSERT_EQ(planned.previews.size(), 1u);
	ASSERT_EQ(planned.previews.front().reason, newHorizonsRealityWarp::StayReason::MOVED);
	ASSERT_TRUE(cast(friendly, enemy));
	ASSERT_NO_FATAL_FAILURE(expectCommitted(*planned.exchange, friendly, enemy));
	EXPECT_EQ(friendly->regenerationRateMillionths, 0);
	EXPECT_EQ(friendly->regenerationPendingMicroHealth, 0);
	EXPECT_EQ(enemy->regenerationRateMillionths, 12345);
	EXPECT_EQ(enemy->regenerationPendingMicroHealth, 54321);
}

TEST_F(NewHorizonsRealityWarpTest, HostileSelectedResistanceRollsOnceAndAbortsBothDirections)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	friendly->addNewBonus(std::make_shared<Bonus>(effect(SpellID::HASTE, BonusType::STACKS_SPEED, 7, PlayerColor(0))));
	enemy->addNewBonus(std::make_shared<Bonus>(effect(SpellID::SLOW, BonusType::STACKS_INITIATIVE, -25, PlayerColor(1))));
	for(auto * unit : {friendly, enemy, other})
		unit->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::MAGIC_RESISTANCE,
			BonusSource::OTHER, 100, BonusSourceID()));
	const auto before = newHorizonsRealityWarp::prepareExchange(*battle(), friendly->unitId(), enemy->unitId());
	ASSERT_TRUE(before.exchange);
	spells::BattleCast event(battle(), attackerSideHero, spells::Mode::HERO, spell().toSpell());
	const auto mechanics = spell().toSpell()->battleMechanics(&event);
	ASSERT_TRUE(mechanics->canBeCastAt({spells::Destination(friendly), spells::Destination(enemy)}));
	testing::StrictMock<vstd::RNGMock> random;
	testing::NiceMock<ServerCallbackMock> server;
	ON_CALL(server, getRNG()).WillByDefault(testing::Return(&random));
	EXPECT_CALL(random, nextInt(0, 99)).Times(1).WillOnce(testing::Return(0));
	ON_CALL(server, apply(testing::Matcher<CPackForClient &>(testing::_)))
		.WillByDefault([&](CPackForClient & packet) { gameHandler->sendAndApply(packet); });
	EXPECT_CALL(server, apply(testing::Matcher<SetStackEffect &>(testing::_))).Times(0);
	mechanics->cast(&server, {spells::Destination(friendly), spells::Destination(enemy)});
	const auto after = newHorizonsRealityWarp::prepareExchange(*battle(), friendly->unitId(), enemy->unitId());
	ASSERT_TRUE(after.exchange);
	for(size_t index = 0; index < 2; ++index)
		EXPECT_TRUE(battle::exactBattleEffectSnapshotEqual(before.exchange->endpoints[index].expected,
			after.exchange->endpoints[index].expected));
}

TEST_F(NewHorizonsRealityWarpTest, RealityBreakerFriendlyPairNeverRollsResistance)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true));
	friendly->addNewBonus(std::make_shared<Bonus>(effect(SpellID::HASTE, BonusType::STACKS_SPEED, 7, PlayerColor(0))));
	for(auto * unit : {friendly, other})
		unit->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::MAGIC_RESISTANCE,
			BonusSource::OTHER, 100, BonusSourceID()));
	const auto planned = newHorizonsRealityWarp::prepareExchange(*battle(), friendly->unitId(), other->unitId());
	ASSERT_TRUE(planned.exchange);
	spells::BattleCast event(battle(), attackerSideHero, spells::Mode::HERO, spell().toSpell());
	const auto mechanics = spell().toSpell()->battleMechanics(&event);
	ASSERT_TRUE(mechanics->canBeCastAt({spells::Destination(friendly), spells::Destination(other)}));
	testing::StrictMock<vstd::RNGMock> random;
	testing::NiceMock<ServerCallbackMock> server;
	ON_CALL(server, getRNG()).WillByDefault(testing::Return(&random));
	EXPECT_CALL(random, nextInt(0, 99)).Times(0);
	ON_CALL(server, apply(testing::Matcher<CPackForClient &>(testing::_)))
		.WillByDefault([&](CPackForClient & packet) { gameHandler->sendAndApply(packet); });
	EXPECT_CALL(server, apply(testing::Matcher<SetStackEffect &>(testing::_)))
		.WillOnce([&](SetStackEffect & packet) { gameHandler->sendAndApply(packet); });
	mechanics->cast(&server, {spells::Destination(friendly), spells::Destination(other)});
	ASSERT_NO_FATAL_FAILURE(expectCommitted(*planned.exchange, friendly, other));
}

TEST_F(NewHorizonsRealityWarpTest, MissingSavedV3RulesRejectWithoutChargeOrExchange)
{
	ASSERT_NO_FATAL_FAILURE(prepare(false, true));
	ASSERT_TRUE(gameState()->getMagicRules().isNull());
	ASSERT_TRUE(battle()->getMagicRules().isNull());
	friendly->addNewBonus(std::make_shared<Bonus>(effect(SpellID::HASTE, BonusType::STACKS_SPEED, 7, PlayerColor(0))));
	const auto first = friendly->save();
	const auto second = enemy->save();
	const auto mana = attackerSideHero->getManaAvailable();
	spells::BattleCast event(battle(), attackerSideHero, spells::Mode::HERO, spell().toSpell());
	const auto mechanics = spell().toSpell()->battleMechanics(&event);
	ASSERT_FALSE(mechanics->usesNewHorizonsMagicV3());
	EXPECT_FALSE(cast(friendly, enemy));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana);
	EXPECT_EQ(friendly->save(), first);
	EXPECT_EQ(enemy->save(), second);
}
