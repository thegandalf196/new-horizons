/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/battle/BattleEffectExchange.h"
#include "../../../lib/battle/CUnitState.h"
#include "../../../lib/battle/NewHorizonsFocusMagic.h"
#include "../../../lib/bonuses/BonusList.h"
#include "../../../lib/bonuses/BonusSelector.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/networkPacks/SetStackEffect.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/ISpellMechanics.h"
#include "../../../lib/spells/NewHorizonsSorcerySpells.h"
#include "../../../server/battles/BattleFocusMagic.h"
#include "../../mock/mock_UnitEnvironment.h"

namespace
{
class NewHorizonsFocusAndRecallIntegrationTest : public HeroCommandFixture
{
protected:
	CStack * ally = nullptr;
	CStack * enemy = nullptr;
	SpellID focus = SpellID::NONE;
	SpellID recall = SpellID::NONE;

	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		useRevisedOrders = true;
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires New Horizons";
	}
	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
	}
	void prepare(bool shooter = false, bool begin = true)
	{
		startGame(); // The common fixture removes authored starting ranks/perks.
		attackerSideHero->setHeroType(HeroTypeID(HeroTypeID::decode("core:neela")));
		for(const auto * identity : {"new-horizons:sorceryMagic", "new-horizons:learning"})
			attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode(identity)), 1, ChangeValueMode::ABSOLUTE);
		ASSERT_EQ(attackerSideHero->secSkills.size(), 2);
		ASSERT_TRUE(attackerSideHero->getPerkState().getSelected().empty());
		attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 1000, ChangeValueMode::ABSOLUTE);
		setTestSpellPointTotal(attackerSideHero, 100);
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		focus = SpellID(SpellID::decode(newHorizonsFocusMagic::SPELL));
		recall = SpellID(SpellID::decode(newHorizonsSorcerySpells::FORCED_RECALL));
		ASSERT_TRUE(focus.hasValue());
		ASSERT_TRUE(recall.hasValue());
		attackerSideHero->addSpellToSpellbook(focus);
		attackerSideHero->addSpellToSpellbook(recall);
		ASSERT_TRUE(attackerSideHero->setCreature(SlotID(0), creatureByName(shooter ? "core:archer" : "core:pikeman"), 1000));
		defenderSideHero->setStackCount(SlotID(0), 1000);
		startBattle();
		for(const auto & unit : battle()->stacks)
			if(unit->unitSlot() == SlotID(0))
				(unit->unitSide() == BattleSide::ATTACKER ? ally : enemy) = unit.get();
		ASSERT_NE(ally, nullptr);
		ASSERT_NE(enemy, nullptr);
		if(begin) beginCombat();
	}
	void move(CStack & unit, BattleHex destination)
	{
		BattleStackMoved packet;
		packet.battleID = battle()->getBattleID();
		packet.stack = unit.unitId();
		packet.tilesToMove.insert(destination);
		gameHandler->sendAndApply(packet);
	}
	BattleAttack acceptedStrike(bool ranged)
	{
		BattleAttack packet;
		packet.battleID = battle()->getBattleID();
		packet.stackAttacking = ally->unitId();
		packet.tile = enemy->getPosition();
		packet.flags = ranged ? BattleAttack::SHOT : 0;
		const auto victimHealth = enemy->getAvailableHealth();
		const auto victimCount = enemy->getCount();
		const auto victimPosition = enemy->getPosition();
		const auto victimRetaliations = enemy->counterAttacks.available();
		const auto victimMoved = enemy->movedThisRound;
		const auto victimWaiting = enemy->waiting;
		BattleStackAttacked miss;
		miss.stackAttacked = enemy->unitId();
		miss.attackerID = ally->unitId();
		miss.damageAmount = 0;
		auto victim = enemy->acquireState();
		CStack::prepareAttacked(miss, *gameHandler->spellcastEnvironment()->getRNG(), victim,
			false, false, battle::DamageProvenance::PHYSICAL_CREATURE);
		packet.bsa.push_back(miss);
		battleFocusMagic::capturePresentation(packet, *ally);
		auto changed = ally->acquireState();
		changed->afterAttack(ranged, false);
		packet.attackerChanges.battleID = packet.battleID;
		UnitChanges update(ally->unitId(), UnitChanges::EOperation::UPDATE);
		update.data = changed->save();
		packet.attackerChanges.changedStacks.push_back(update);
		// An accepted miss still spends one Focus charge. The real recording server
		// applies both the attack's normal state and the following effect payment.
		gameHandler->sendAndApply(packet);
		battleFocusMagic::consumeAcceptedStrike(*gameHandler->spellcastEnvironment(), *battle(), packet);
		EXPECT_EQ(packet.bsa.front().damageAmount, 0);
		EXPECT_EQ(packet.bsa.front().killedAmount, 0);
		EXPECT_EQ(enemy->getAvailableHealth(), victimHealth);
		EXPECT_EQ(enemy->getCount(), victimCount);
		EXPECT_EQ(enemy->getPosition(), victimPosition);
		EXPECT_EQ(enemy->counterAttacks.available(), victimRetaliations);
		EXPECT_EQ(enemy->movedThisRound, victimMoved);
		EXPECT_EQ(enemy->waiting, victimWaiting);
		return packet;
	}
	void addEffect(CStack & unit, Bonus bonus)
	{
		SetStackEffect packet;
		packet.battleID = battle()->getBattleID();
		packet.toAdd.emplace_back(unit.unitId(), std::vector<Bonus>{std::move(bonus)});
		gameHandler->sendAndApply(packet);
	}
	void checkAmmoCartCannotExtend(int remaining)
	{
		testing::NiceMock<UnitEnvironmentMock> environment;
		ON_CALL(environment, unitHasAmmoCart(testing::_)).WillByDefault(testing::Return(true));
		auto view = ally->acquireState();
		view->bindStateEnvironment(&environment);
		EXPECT_EQ(view->shots.available(), remaining);
		EXPECT_EQ(view->canShoot(), remaining > 0);
	}
};
}

TEST_F(NewHorizonsFocusAndRecallIntegrationTest, ConvertedFocusHasThreeHardShotsAndRetainsThirdStrikePresentation)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_FALSE(ally->isShooter());
	const auto ammo = ally->acquireState()->save()["state"]["shots"];
	ASSERT_TRUE(castOn(attackerSideHero, focus, ally));
	ASSERT_TRUE(newHorizonsFocusMagic::converted(*ally));
	ASSERT_EQ(newHorizonsFocusMagic::charges(*ally), 3);
	checkAmmoCartCannotExtend(3);
	const auto melee = acceptedStrike(false);
	EXPECT_FALSE(melee.focusMagicRanged);
	EXPECT_EQ(newHorizonsFocusMagic::charges(*ally), 3);
	for(int remaining = 2; remaining >= 0; --remaining)
	{
		const auto strike = acceptedStrike(true);
		EXPECT_TRUE(strike.focusMagicRanged);
		EXPECT_TRUE(strike.focusMagicConverted);
		EXPECT_EQ(newHorizonsFocusMagic::charges(*ally), remaining);
		EXPECT_EQ(ally->acquireState()->save()["state"]["shots"], ammo);
		checkAmmoCartCannotExtend(remaining);
	}
	EXPECT_FALSE(newHorizonsFocusMagic::active(*ally));
	EXPECT_FALSE(ally->isShooter());
	ASSERT_GE(server.attacks.size(), 4);
	EXPECT_TRUE(server.attacks.back().focusMagicRanged);
	EXPECT_TRUE(server.attacks.back().focusMagicConverted);
}

TEST_F(NewHorizonsFocusAndRecallIntegrationTest, ConvertedRecastRefreshesThreeChargesWithoutDuplicatingGrantOrOtherEffects)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	Bonus blessing(BonusDuration::ONE_BATTLE, BonusType::PRIMARY_SKILL, BonusSource::SPELL_EFFECT,
		2, BonusSourceID(SpellID(SpellID::BLESS)), BonusSubtypeID(PrimarySkill::ATTACK));
	addEffect(*ally, blessing);
	ASSERT_TRUE(castOn(attackerSideHero, focus, ally));
	acceptedStrike(true);
	ASSERT_EQ(newHorizonsFocusMagic::charges(*ally), 2);
	advanceRound();
	ASSERT_TRUE(castOn(attackerSideHero, focus, ally));
	EXPECT_EQ(newHorizonsFocusMagic::charges(*ally), 3);
	EXPECT_TRUE(newHorizonsFocusMagic::converted(*ally));
	int markers = 0;
	const auto chargeMarkers = ally->getUnstackedBonuses(Selector::type()(BonusType::NONE));
	for(const auto & bonus : *chargeMarkers)
		if(newHorizonsFocusMagic::isChargeMarker(*bonus)) ++markers;
	EXPECT_EQ(markers, 1);
	for(int strike = 0; strike < 3; ++strike) acceptedStrike(true);
	EXPECT_FALSE(newHorizonsFocusMagic::active(*ally));
	EXPECT_TRUE(ally->hasBonus(CSelector([&](const Bonus * bonus)
		{ return battle::exactBattleEffectEqual(*bonus, blessing); })));
}

TEST_F(NewHorizonsFocusAndRecallIntegrationTest, NativeShooterRetainsOrdinaryAmmoAndLosesOnlyEnhancementAfterThreeShots)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true));
	const auto ordinaryAmmo = ally->shots.available();
	ASSERT_GT(ordinaryAmmo, 3);
	ASSERT_TRUE(castOn(attackerSideHero, focus, ally));
	EXPECT_FALSE(newHorizonsFocusMagic::converted(*ally));
	EXPECT_EQ(ally->shots.available(), ordinaryAmmo);
	EXPECT_GT(ally->valOfBonuses(BonusType::PERCENTAGE_DAMAGE_BOOST,
		BonusSubtypeID(BonusCustomSubtype::damageTypeRanged)), 0);
	for(int remaining = 2; remaining >= 0; --remaining)
	{
		const auto strike = acceptedStrike(true);
		EXPECT_TRUE(strike.focusMagicRanged);
		EXPECT_FALSE(strike.focusMagicConverted);
		EXPECT_EQ(newHorizonsFocusMagic::charges(*ally), remaining);
		EXPECT_EQ(ally->shots.available(), ordinaryAmmo - (3 - remaining));
	}
	EXPECT_TRUE(ally->isShooter());
	EXPECT_TRUE(ally->canShoot());
	EXPECT_EQ(ally->valOfBonuses(BonusType::PERCENTAGE_DAMAGE_BOOST,
		BonusSubtypeID(BonusCustomSubtype::damageTypeRanged)), 0);
}

TEST_F(NewHorizonsFocusAndRecallIntegrationTest, InvalidAndUnaffordableCastsLeaveStateManaAndHeroAllowanceUntouched)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	move(*ally, BattleHex(8, 5));
	const auto unitBefore = ally->acquireState()->save();
	const auto allowance = battle()->getHeroActionAllowances(BattleSide::ATTACKER);
	const auto casts = server.casts.size();
	const auto mana = attackerSideHero->getManaAvailable();
	EXPECT_FALSE(castOn(attackerSideHero, focus, enemy));
	EXPECT_FALSE(castOn(attackerSideHero, recall, enemy));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana);
	setTestSpellPointTotal(attackerSideHero, 0);
	EXPECT_FALSE(castOn(attackerSideHero, focus, ally));
	EXPECT_FALSE(castOn(attackerSideHero, recall, ally));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), 0);
	EXPECT_EQ(ally->acquireState()->save(), unitBefore);
	EXPECT_EQ(battle()->getHeroActionAllowances(BattleSide::ATTACKER), allowance);
	EXPECT_EQ(server.casts.size(), casts);
	EXPECT_EQ(newHorizonsFocusMagic::charges(*ally), 0);
}

TEST_F(NewHorizonsFocusAndRecallIntegrationTest, RecallUsesFinalClassicPlacementAndPreservesRootsHealthAndSpentCreatureState)
{
	ASSERT_NO_FATAL_FAILURE(prepare(false, false));
	ASSERT_EQ(battle()->battleGetOwner(enemy), PlayerColor(1));
	const BattleHex home(12, 5);
	move(*enemy, home);
	beginCombat(); // Real END_TACTIC_PHASE captures placement before opening events.
	ASSERT_EQ(enemy->initialPosition, home);
	move(*enemy, BattleHex(8, 5));
	HeroOrderState hold;
	hold.command = HeroCommand::HOLD_THE_LINE;
	hold.issuedRound = battle()->getRound();
	hold.anchors.push_back({enemy->unitId(), enemy->getPosition().toInt()});
	hold.validateShape();
	battle()->setHeroOrderStates(BattleSide::DEFENDER, {hold});
	const auto entangle = SpellID(SpellID::decode("new-horizons:entangle"));
	ASSERT_TRUE(entangle.hasValue());
	Bonus root(BonusDuration::N_TURNS, BonusType::BIND_EFFECT, BonusSource::SPELL_EFFECT, 1, BonusSourceID(entangle));
	root.turnsRemain = 2;
	root.spellCasterOwner = PlayerColor(0);
	addEffect(*enemy, root);
	auto spent = enemy->acquireState();
	spent->movedThisRound = true;
	spent->waiting = true;
	spent->counterAttacks.use();
	int64_t damage = 13;
	spent->damage(damage);
	BattleUnitsChanged worn;
	worn.battleID = battle()->getBattleID();
	UnitChanges changed(enemy->unitId(), UnitChanges::EOperation::UPDATE);
	changed.data = spent->save();
	worn.changedStacks.push_back(changed);
	gameHandler->sendAndApply(worn);
	const auto retaliations = enemy->counterAttacks.available();
	const auto health = enemy->getAvailableHealth();
	const auto count = enemy->getCount();
	ASSERT_LT(health, enemy->getTotalHealth());
	ASSERT_TRUE(castOn(attackerSideHero, recall, enemy));
	EXPECT_EQ(enemy->getPosition(), home);
	EXPECT_EQ(enemy->initialPosition, home);
	EXPECT_EQ(enemy->getAvailableHealth(), health);
	EXPECT_EQ(enemy->getCount(), count);
	EXPECT_TRUE(enemy->movedThisRound);
	EXPECT_TRUE(enemy->waiting);
	EXPECT_EQ(enemy->counterAttacks.available(), retaliations);
	const auto roots = enemy->getBonusesOfType(BonusType::BIND_EFFECT);
	ASSERT_EQ(roots->size(), 1);
	EXPECT_TRUE(battle::exactBattleEffectEqual(*roots->front(), root));
	const auto order = battle()->getHeroOrderState(BattleSide::DEFENDER, HeroCommand::HOLD_THE_LINE);
	ASSERT_TRUE(order);
	EXPECT_TRUE(order->containsHoldBroken(enemy->unitId()));
	EXPECT_EQ(order->anchors, hold.anchors);
	move(*enemy, BattleHex(8, 5));
	EXPECT_EQ(enemy->initialPosition, home);
	EXPECT_TRUE(battle()->getHeroOrderState(BattleSide::DEFENDER, HeroCommand::HOLD_THE_LINE)->containsHoldBroken(enemy->unitId()));
}

TEST_F(NewHorizonsFocusAndRecallIntegrationTest, ReplicatedFinalDeploymentCapturesAnchorOnceAndRepeatedCompletionCannotRefillIt)
{
	ASSERT_NO_FATAL_FAILURE(prepare(false, false));
	BattleDeploymentState initial;
	initial.independent = true;
	initial.distances[BattleSide::ATTACKER] = 1;
	initial.completed[BattleSide::ATTACKER] = true;
	initial.finalRelocationDistances[BattleSide::ATTACKER] = 1;
	initial.validateShape();
	battle()->deploymentState = initial;
	battle()->tacticsSide = initial.activeSide();
	battle()->tacticDistance = initial.activeDistance();
	const auto spawn = ally->initialPosition;
	const BattleHex placed(4, 5);
	move(*ally, placed);
	EXPECT_EQ(ally->initialPosition, spawn);
	BattleDeploymentPhaseChanged completed;
	completed.battleID = battle()->getBattleID();
	completed.state = initial;
	completed.state.complete(BattleSide::ATTACKER);
	gameHandler->sendAndApply(completed);
	ASSERT_EQ(ally->initialPosition, placed);
	EXPECT_EQ(battle()->getTacticDist(), 0);
	move(*ally, BattleHex(8, 5));
	gameHandler->sendAndApply(completed);
	EXPECT_EQ(ally->initialPosition, placed);
	EXPECT_EQ(ally->getPosition(), BattleHex(8, 5));
}
