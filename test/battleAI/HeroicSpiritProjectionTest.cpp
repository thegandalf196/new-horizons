/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "OwnedExportMeleeFixture.h"
#include "../../lib/battle/CommandArmyBonuses.h"
#include "../../lib/battle/NewHorizonsCommandResources.h"
#include "../../lib/bonuses/BonusList.h"
#include "../../lib/mapObjects/CGTownInstance.h"
#include "../../lib/mapObjects/NewHorizonsGlyphsOfFear.h"

class HeroicSpiritProjectionTest : public OwnedExportMeleeTest
{
protected:
	bool glyphsScenario = false;
	bool friendlyGlyphs = false;
	int glyphsDistance = 8;
	CGTownInstance * glyphsTown = nullptr;

	void mapLoaded(CMap * loaded) override
	{
		OwnedExportMeleeTest::mapLoaded(loaded);
		if(glyphsScenario)
		{
			// Author the Fortress before world initialization and town bonuses.
			for(const auto & object : loaded->objects)
				if(auto * town = dynamic_cast<CGTownInstance *>(object.get()))
					town->subID = FactionID::FORTRESS.getNum();
		}
	}

	void prepareSpirit(bool owned, bool esprit = false, int ordinaryMorale = 2)
	{
		startGame(glyphsScenario);
		if(glyphsScenario)
		{
			glyphsTown = findFirst<CGTownInstance>();
			ASSERT_NE(glyphsTown, nullptr);
			ASSERT_EQ(glyphsTown->getFactionID(), FactionID::FORTRESS);
			ASSERT_TRUE(gameHandler->buildStructure(glyphsTown->id, BuildingID::SPECIAL_3, true));
			if(friendlyGlyphs) gameHandler->setOwner(glyphsTown, PlayerColor(0));
			ChangeObjPos position;
			position.objid = attackerSideHero->id;
			position.nPos = glyphsTown->visitablePos() + int3(glyphsDistance, 0, 0);
			position.initiator = PlayerColor(0);
			gameHandler->sendAndApply(position);
			ASSERT_EQ(attackerSideHero->visitablePos(), position.nPos);
		}
		const SecondarySkill leadership(SecondarySkill::decode("new-horizons:discipline"));
		const std::array<const char *, 3> perks{esprit ? "new-horizons:discipline.espritDeCorps" : "new-horizons:discipline.inspirationalLeader",
			"new-horizons:discipline.standardBearer", "new-horizons:discipline.heroicSpirit"};
		for(int rank = 1; rank <= 3; ++rank)
		{
			attackerSideHero->setSecSkillLevel(leadership, rank, ChangeValueMode::ABSOLUTE);
			attackerSideHero->applyPerkSelection({"new-horizons:discipline", perks[rank - 1]});
		}
		ASSERT_TRUE(attackerSideHero->hasActivePerk("new-horizons:discipline", perks.back()));
		attackerSideHero->level = 16;
		attackerSideHero->captureCommandProgression();
		attackerSideHero->selectCommandEnhancement(6, newHorizonsHeroes::CommandEnhancement::COURAGE);
		attackerSideHero->setPrimarySkill(PrimarySkill::ATTACK, 100, ChangeValueMode::ABSOLUTE);
		attackerSideHero->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
			BonusType::MORALE, BonusSource::OTHER, ordinaryMorale - attackerSideHero->valOfBonuses(BonusType::MORALE), BonusSourceID()));
		startBattle();
		for(const auto & stack : battle()->stacks)
			if(stack->unitSide() == BattleSide::ATTACKER && stack->unitSlot() == SlotID(0)) braced = stack.get();
		ASSERT_NE(braced, nullptr);
		incoming = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(89), 100);
		addStack(BattleSide::ATTACKER, creatureByName("core:archer"), BattleHex(70), 10);
		beginCombat();
		activate(braced->unitId());
		if(esprit)
			attackerSideHero->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
				BonusType::MORALE, BonusSource::ARMY, -2, BonusSourceID()));
		environment = std::make_unique<projectedHitTest::Environment>(gameState());
		const int expected = ordinaryMorale - (esprit ? 1 : 0)
			+ newHorizonsGlyphsOfFear::moraleBonuses(attackerSideHero)->totalValue();
		ASSERT_EQ(attackerSideHero->moraleVal(), expected);
		ASSERT_EQ(battle()->battleGetHeroMorale(BattleSide::ATTACKER), attackerSideHero->moraleVal());
		ASSERT_GT(braced->getBonuses(CSelector([](const Bonus * bonus)
			{ return commandArmyBonuses::isProjectedBonus(*bonus) && bonus->type == BonusType::MORALE; }))->totalValue(), 0);
		if(owned)
		{
			captureOwned();
			startOwned();
		}
		else refreshLegacyKnown();
	}

	Bonus heroMorale(int value)
	{
		auto bonus = aura(BonusDuration::ONE_BATTLE, BonusType::MORALE);
		bonus.val = value;
		bonus.limiter.reset(); // Real originating export reaches the hero, not only creatures.
		return bonus;
	}

	void acceptNativeFocus(int efficiency)
	{
		releaseKnown();
		seed.reset(); // Retire every projected owner before native command dispatch.
		const auto action = BattleAction::makeTargetedHeroCommand(BattleSide::ATTACKER,
			HeroCommand::FOCUS_FIRE, incoming->unitId());
		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
		const auto order = battle()->getHeroOrderState(BattleSide::ATTACKER, HeroCommand::FOCUS_FIRE);
		ASSERT_TRUE(order);
		EXPECT_EQ(order->heroicSpiritEfficiencyBonusPercent, efficiency);
	}

	void acceptFocus(HypotheticBattle & branch, int efficiency, int percent)
	{
		const auto allowance = branch.prepareHeroOrderAllowance(BattleSide::ATTACKER);
		ASSERT_TRUE(allowance);
		const auto effects = branch.beginProjectedHeroOrder(BattleSide::ATTACKER,
			HeroCommand::FOCUS_FIRE, {incoming->unitId()}, *allowance);
		ASSERT_TRUE(effects);
		ASSERT_TRUE(effects->focusFire);
		EXPECT_EQ(effects->order.heroicSpiritEfficiencyBonusPercent, efficiency);
		EXPECT_FALSE(effects->order.heroMorale);
		// A100, fixed base5 + .15*A*(1 + HeroicSpirit/100); fixed base is unscaled.
		EXPECT_EQ(effects->focusFire->rangedDamagePercent, percent);
		ASSERT_TRUE(branch.projectAcceptedHeroOrder(BattleSide::ATTACKER, *effects, *allowance));
		EXPECT_EQ(branch.getHeroOrderState(BattleSide::ATTACKER, HeroCommand::FOCUS_FIRE), effects->order);
		EXPECT_EQ(branch.getFocusFireState(BattleSide::ATTACKER), effects->focusFire);
	}
};

TEST_F(HeroicSpiritProjectionTest, NativeAndBorrowedQueriesIgnoreRealSlotCourageAndRecipientOnlyChanges)
{
	ASSERT_NO_FATAL_FAILURE(prepareSpirit(false));
	const auto native = gameState()->saveToMemory();
	auto child = std::make_shared<HypotheticBattle>(environment.get(), known);
	auto sibling = std::make_shared<HypotheticBattle>(environment.get(), known);
	for(const auto & branch : {known, child, sibling})
	{
		EXPECT_EQ(branch->battleGetHeroBonuses(BattleSide::ATTACKER, Selector::type()(BonusType::MORALE))->totalValue(), 2);
		EXPECT_EQ(branch->battleGetHeroMorale(BattleSide::ATTACKER), 2);
	}
	newHorizonsHeroes::CommandSlotModifiers courage;
	courage.enhancements.morale = 9;
	child->addUnitBonus(braced->unitId(), commandArmyBonuses::project(courage));
	EXPECT_EQ(child->battleGetHeroMorale(BattleSide::ATTACKER), 2);
	const auto prepared = battle()->battlePrepareHeroOrderState(BattleSide::ATTACKER,
		HeroCommand::FOCUS_FIRE, {incoming->unitId()});
	ASSERT_TRUE(prepared);
	EXPECT_EQ(prepared->heroicSpiritEfficiencyBonusPercent, 10);
	EXPECT_EQ(newHorizonsCommand::heroicSpiritEfficiency(attackerSideHero), 10);
	for(const auto & branch : {known, child, sibling}) ASSERT_NO_FATAL_FAILURE(acceptFocus(*branch, 10, 22));
	EXPECT_EQ(gameState()->saveToMemory(), native);
}

TEST_F(HeroicSpiritProjectionTest, OwnedChildMutationChangesFutureOrdersAndKeepsAcceptedParentSiblingReceipts)
{
	ASSERT_NO_FATAL_FAILURE(prepareSpirit(true));
	const auto native = gameState()->saveToMemory();
	const auto rng = projectedHitTest::serialized(*gameHandler->randomizer);
	auto child = std::make_shared<HypotheticBattle>(environment.get(), known);
	auto sibling = std::make_shared<HypotheticBattle>(environment.get(), known);
	for(const auto & branch : {known, child, sibling}) EXPECT_EQ(branch->battleGetHeroMorale(BattleSide::ATTACKER), 2);
	ASSERT_NO_FATAL_FAILURE(acceptFocus(*known, 10, 22));
	ASSERT_NO_FATAL_FAILURE(acceptFocus(*sibling, 10, 22));
	const auto parentOrder = known->getHeroOrderState(BattleSide::ATTACKER);
	const auto siblingOrder = sibling->getHeroOrderState(BattleSide::ATTACKER);
	const auto positive = heroMorale(7);
	const auto negative = heroMorale(-5);
	child->addUnitBonus(braced->unitId(), {positive, negative});
	EXPECT_EQ(child->battleGetHeroMorale(BattleSide::ATTACKER), 4);
	ASSERT_NO_FATAL_FAILURE(acceptFocus(*child, 20, 23));
	const auto childOrder = child->getHeroOrderState(BattleSide::ATTACKER);
	const auto childMark = child->getFocusFireState(BattleSide::ATTACKER);
	auto descendant = std::make_shared<HypotheticBattle>(environment.get(), child);
	child->removeUnitBonus(braced->unitId(), {positive, negative});
	EXPECT_EQ(child->battleGetHeroMorale(BattleSide::ATTACKER), 2);
	EXPECT_EQ(descendant->battleGetHeroMorale(BattleSide::ATTACKER), 4);
	EXPECT_EQ(child->getHeroOrderState(BattleSide::ATTACKER), childOrder);
	EXPECT_EQ(child->getFocusFireState(BattleSide::ATTACKER), childMark);
	EXPECT_EQ(descendant->getHeroOrderState(BattleSide::ATTACKER), childOrder);
	EXPECT_EQ(known->getHeroOrderState(BattleSide::ATTACKER), parentOrder);
	EXPECT_EQ(sibling->getHeroOrderState(BattleSide::ATTACKER), siblingOrder);
	EXPECT_EQ(known->battleGetHeroMorale(BattleSide::ATTACKER), 2);
	EXPECT_EQ(sibling->battleGetHeroMorale(BattleSide::ATTACKER), 2);
	child->nextRound();
	descendant->nextRound();
	ASSERT_NO_FATAL_FAILURE(acceptFocus(*child, 10, 22));
	ASSERT_NO_FATAL_FAILURE(acceptFocus(*descendant, 20, 23));
	EXPECT_EQ(gameState()->saveToMemory(), native);
	EXPECT_EQ(projectedHitTest::serialized(*gameHandler->randomizer), rng);
}

TEST_F(HeroicSpiritProjectionTest, OwnedSignedMoraleClampsAndEfficiencyCapsReachActualAcceptedOrders)
{
	ASSERT_NO_FATAL_FAILURE(prepareSpirit(true));
	const auto native = gameState()->saveToMemory();
	const auto rng = projectedHitTest::serialized(*gameHandler->randomizer);
	struct Case { int delta; int morale; int efficiency; int percent; };
	for(const auto test : {Case{-5, -3, 0, 20}, Case{-2, 0, 0, 20}, Case{4, 6, 30, 25},
		Case{100, 10, 30, 25}, Case{-100, -10, 0, 20}})
	{
		SCOPED_TRACE(test.delta);
		auto branch = std::make_shared<HypotheticBattle>(environment.get(), known);
		EXPECT_EQ(branch->battleGetHeroMorale(BattleSide::ATTACKER), 2); // Warm before mutation.
		branch->addUnitBonus(braced->unitId(), {heroMorale(test.delta)});
		EXPECT_EQ(branch->battleGetHeroMorale(BattleSide::ATTACKER), test.morale);
		ASSERT_NO_FATAL_FAILURE(acceptFocus(*branch, test.efficiency, test.percent));
		EXPECT_EQ(known->battleGetHeroMorale(BattleSide::ATTACKER), 2);
		EXPECT_FALSE(known->getHeroOrderState(BattleSide::ATTACKER));
	}
	EXPECT_EQ(gameState()->saveToMemory(), native);
	EXPECT_EQ(projectedHitTest::serialized(*gameHandler->randomizer), rng);
}

TEST_F(HeroicSpiritProjectionTest, OwnedHeroQueryRetainsOuterVisibilityAndDoesNotUseCreatureEligibility)
{
	ASSERT_NO_FATAL_FAILURE(prepareSpirit(true));
	known->addUnitBonus(braced->unitId(), {heroMorale(2)});
	CPlayerBattleCallback visible(known.get(), PlayerColor(0)), hidden(known.get(), PlayerColor(1));
	EXPECT_EQ(visible.battleGetHeroMorale(BattleSide::ATTACKER), 4);
	EXPECT_EQ(hidden.battleGetHeroMorale(BattleSide::ATTACKER), 0);
	EXPECT_EQ(visible.battleGetHeroMorale(BattleSide::INVALID), 0);
	CPlayerBattleCallback absent(nullptr, PlayerColor(0));
	EXPECT_EQ(absent.battleGetHeroMorale(BattleSide::ATTACKER), 0);
	// A creature-only NO_MORALE record cannot suppress actual hero Morale.
	known->addUnitBonus(braced->unitId(), {Bonus(BonusDuration::ONE_BATTLE,
		BonusType::NO_MORALE, BonusSource::OTHER, 1, BonusSourceID())});
	EXPECT_EQ(known->battleGetUnitByID(braced->unitId())->moraleVal(), 0);
	EXPECT_EQ(known->battleGetHeroMorale(BattleSide::ATTACKER), 4);
	ASSERT_NO_FATAL_FAILURE(acceptFocus(*known, 20, 23));
}

TEST_F(HeroicSpiritProjectionTest, NativeEspritAdjustsArmyPenaltyBeforeActualOrderAcceptance)
{
	ASSERT_NO_FATAL_FAILURE(prepareSpirit(false, true, 3));
	EXPECT_EQ(attackerSideHero->valOfBonuses(BonusType::MORALE), 1);
	EXPECT_EQ(attackerSideHero->moraleVal(), 2);
	EXPECT_EQ(battle()->battleGetHeroMorale(BattleSide::ATTACKER), attackerSideHero->moraleVal());
	ASSERT_NO_FATAL_FAILURE(acceptNativeFocus(10));
}

TEST_F(HeroicSpiritProjectionTest, OwnedEspritUsesCurrentRawSourcesAndPreservesAcceptedIssuance)
{
	ASSERT_NO_FATAL_FAILURE(prepareSpirit(true, true, 3));
	const auto native = gameState()->saveToMemory();
	const auto rng = projectedHitTest::serialized(*gameHandler->randomizer);
	auto child = std::make_shared<HypotheticBattle>(environment.get(), known);
	auto sibling = std::make_shared<HypotheticBattle>(environment.get(), known);
	ASSERT_NO_FATAL_FAILURE(acceptFocus(*known, 10, 22));
	ASSERT_NO_FATAL_FAILURE(acceptFocus(*sibling, 10, 22));
	const auto parentOrder = known->getHeroOrderState(BattleSide::ATTACKER);
	const auto siblingOrder = sibling->getHeroOrderState(BattleSide::ATTACKER);
	auto penalty = heroMorale(-2);
	penalty.source = BonusSource::ARMY;
	child->addUnitBonus(braced->unitId(), {penalty});
	EXPECT_EQ(child->battleGetHeroMorale(BattleSide::ATTACKER), 1); // -2 becomes -1.
	ASSERT_NO_FATAL_FAILURE(acceptFocus(*child, 5, 21));
	const auto accepted = child->getHeroOrderState(BattleSide::ATTACKER);
	auto descendant = std::make_shared<HypotheticBattle>(environment.get(), child);
	child->removeUnitBonus(braced->unitId(), {penalty});
	EXPECT_EQ(child->battleGetHeroMorale(BattleSide::ATTACKER), 2);
	EXPECT_EQ(descendant->battleGetHeroMorale(BattleSide::ATTACKER), 1);
	EXPECT_EQ(child->getHeroOrderState(BattleSide::ATTACKER), accepted);
	EXPECT_EQ(descendant->getHeroOrderState(BattleSide::ATTACKER), accepted);
	EXPECT_EQ(known->getHeroOrderState(BattleSide::ATTACKER), parentOrder);
	EXPECT_EQ(sibling->getHeroOrderState(BattleSide::ATTACKER), siblingOrder);
	child->nextRound();
	descendant->nextRound();
	ASSERT_NO_FATAL_FAILURE(acceptFocus(*child, 10, 22));
	ASSERT_NO_FATAL_FAILURE(acceptFocus(*descendant, 5, 21));
	EXPECT_EQ(gameState()->saveToMemory(), native);
	EXPECT_EQ(projectedHitTest::serialized(*gameHandler->randomizer), rng);
}

TEST_F(HeroicSpiritProjectionTest, EspritPreservesHostileSourceClassificationAndRestacksAdjustedCopies)
{
	ASSERT_NO_FATAL_FAILURE(prepareSpirit(false, true, 3));
	releaseKnown();
	// Same-key stacking must retain the ordinary -1 after the ARMY -1 becomes
	// zero. Adjusting only an already selected ARMY winner could lose that source.
	auto army = std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::MORALE,
		BonusSource::ARMY, -1, BonusSourceID());
	army->stacking = "hero-morale-esprit-source";
	auto ordinary = std::make_shared<Bonus>(*army);
	ordinary->source = BonusSource::OTHER;
	attackerSideHero->addNewBonus(army);
	attackerSideHero->addNewBonus(ordinary);
	auto hostile = std::make_shared<Bonus>(*army);
	hostile->stacking.clear();
	hostile->val = -2;
	hostile->appliedByEnemy = true;
	attackerSideHero->addNewBonus(hostile);
	auto enemyOwner = std::make_shared<Bonus>(*hostile);
	enemyOwner->appliedByEnemy = false;
	enemyOwner->bonusOwner = PlayerColor(1);
	attackerSideHero->addNewBonus(enemyOwner);
	EXPECT_EQ(attackerSideHero->moraleVal(), -3);
	EXPECT_EQ(battle()->battleGetHeroMorale(BattleSide::ATTACKER), attackerSideHero->moraleVal());
	ASSERT_NO_FATAL_FAILURE(captureOwned());
	ASSERT_NO_FATAL_FAILURE(startOwned());
	EXPECT_EQ(known->battleGetHeroMorale(BattleSide::ATTACKER), -3);
	EXPECT_EQ(army->val, -1); // Never rewrite the exported source or its cached view.
	EXPECT_EQ(ordinary->val, -1);
	ASSERT_NO_FATAL_FAILURE(acceptFocus(*known, 0, 20));
}

TEST_F(HeroicSpiritProjectionTest, NativeGlyphsComposeWithRawMoraleAndReachActualAcceptedOrder)
{
	glyphsScenario = true;
	ASSERT_NO_FATAL_FAILURE(prepareSpirit(false, false, 3));
	EXPECT_EQ(attackerSideHero->valOfBonuses(BonusType::MORALE), 3);
	ASSERT_EQ(newHorizonsGlyphsOfFear::moraleBonuses(attackerSideHero)->totalValue(), -1);
	EXPECT_EQ(attackerSideHero->moraleVal(), 2);
	EXPECT_EQ(battle()->battleGetHeroMorale(BattleSide::ATTACKER), attackerSideHero->moraleVal());
	ASSERT_NO_FATAL_FAILURE(acceptNativeFocus(10));
}

TEST_F(HeroicSpiritProjectionTest, OwnedGlyphsContextComposesWithChildSourcesAndFixedIssuance)
{
	glyphsScenario = true;
	ASSERT_NO_FATAL_FAILURE(prepareSpirit(true, false, 3));
	const auto native = gameState()->saveToMemory();
	auto child = std::make_shared<HypotheticBattle>(environment.get(), known);
	auto sibling = std::make_shared<HypotheticBattle>(environment.get(), known);
	for(const auto & branch : {known, child, sibling}) EXPECT_EQ(branch->battleGetHeroMorale(BattleSide::ATTACKER), 2);
	ASSERT_NO_FATAL_FAILURE(acceptFocus(*known, 10, 22));
	ASSERT_NO_FATAL_FAILURE(acceptFocus(*sibling, 10, 22));
	const auto parentOrder = known->getHeroOrderState(BattleSide::ATTACKER);
	const auto siblingOrder = sibling->getHeroOrderState(BattleSide::ATTACKER);
	const auto grant = heroMorale(2);
	child->addUnitBonus(braced->unitId(), {grant});
	EXPECT_EQ(child->battleGetHeroMorale(BattleSide::ATTACKER), 4); // Raw5 plus captured Glyphs-1.
	ASSERT_NO_FATAL_FAILURE(acceptFocus(*child, 20, 23));
	const auto accepted = child->getHeroOrderState(BattleSide::ATTACKER);
	auto descendant = std::make_shared<HypotheticBattle>(environment.get(), child);
	child->removeUnitBonus(braced->unitId(), {grant});
	EXPECT_EQ(child->battleGetHeroMorale(BattleSide::ATTACKER), 2);
	EXPECT_EQ(descendant->battleGetHeroMorale(BattleSide::ATTACKER), 4);
	EXPECT_EQ(child->getHeroOrderState(BattleSide::ATTACKER), accepted);
	EXPECT_EQ(descendant->getHeroOrderState(BattleSide::ATTACKER), accepted);
	EXPECT_EQ(known->getHeroOrderState(BattleSide::ATTACKER), parentOrder);
	EXPECT_EQ(sibling->getHeroOrderState(BattleSide::ATTACKER), siblingOrder);
	EXPECT_EQ(gameState()->saveToMemory(), native);
}

TEST_F(HeroicSpiritProjectionTest, OutsideRadiusGlyphsDoNotChangeNativeOrOwnedHeroMorale)
{
	glyphsScenario = true;
	glyphsDistance = 9;
	ASSERT_NO_FATAL_FAILURE(prepareSpirit(true, false, 3));
	EXPECT_TRUE(newHorizonsGlyphsOfFear::moraleBonuses(attackerSideHero)->empty());
	EXPECT_EQ(known->battleGetHeroMorale(BattleSide::ATTACKER), 3);
	ASSERT_NO_FATAL_FAILURE(acceptFocus(*known, 15, 22));
}

TEST_F(HeroicSpiritProjectionTest, FriendlyFortressGlyphsDoNotChangeNativeOrOwnedHeroMorale)
{
	glyphsScenario = true;
	friendlyGlyphs = true;
	ASSERT_NO_FATAL_FAILURE(prepareSpirit(true, false, 3));
	EXPECT_TRUE(newHorizonsGlyphsOfFear::moraleBonuses(attackerSideHero)->empty());
	EXPECT_EQ(known->battleGetHeroMorale(BattleSide::ATTACKER), 3);
	ASSERT_NO_FATAL_FAILURE(acceptFocus(*known, 15, 22));
}

TEST_F(HeroicSpiritProjectionTest, EspritAndGlyphsComposeOnceInNativeAndOwnedOrders)
{
	glyphsScenario = true;
	ASSERT_NO_FATAL_FAILURE(prepareSpirit(true, true, 3));
	EXPECT_EQ(attackerSideHero->moraleVal(), 1); // +3, recovered ARMY-1, Glyphs-1.
	EXPECT_EQ(known->battleGetHeroMorale(BattleSide::ATTACKER), 1);
	ASSERT_NO_FATAL_FAILURE(acceptFocus(*known, 5, 21));
	ASSERT_NO_FATAL_FAILURE(acceptNativeFocus(5));
}

TEST_F(HeroicSpiritProjectionTest, NativeHeroFlagsUseActualMoralePrecedenceAndAcceptedOrders)
{
	ASSERT_NO_FATAL_FAILURE(prepareSpirit(false));
	releaseKnown();
	struct Case { std::vector<BonusType> flags; int morale; int efficiency; };
	const std::vector<Case> cases{
		{{BonusType::MAX_MORALE}, 10, 30},
		{{BonusType::MINIMUM_MORALE}, 4, 20},
		{{BonusType::NO_MORALE}, 0, 0},
		{{BonusType::NO_MORALE, BonusType::MINIMUM_MORALE}, 0, 0},
		{{BonusType::MAX_MORALE, BonusType::NO_MORALE}, 10, 30}};
	for(size_t index = 0; index < cases.size(); ++index)
	{
		SCOPED_TRACE(index);
		const auto & test = cases[index];
		std::vector<std::shared_ptr<Bonus>> flags;
		for(const auto type : test.flags)
		{
			auto bonus = std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, type,
				BonusSource::OTHER, type == BonusType::MINIMUM_MORALE ? 4 : 0, BonusSourceID());
			if(type == BonusType::MINIMUM_MORALE) bonus->valType = BonusValueType::INDEPENDENT_MAX;
			attackerSideHero->addNewBonus(bonus);
			flags.push_back(bonus);
		}
		EXPECT_EQ(attackerSideHero->moraleVal(), test.morale);
		EXPECT_EQ(battle()->battleGetHeroMorale(BattleSide::ATTACKER), attackerSideHero->moraleVal());
		ASSERT_NO_FATAL_FAILURE(acceptNativeFocus(test.efficiency));
		for(const auto & flag : flags) attackerSideHero->removeBonus(flag);
		EXPECT_EQ(battle()->battleGetHeroMorale(BattleSide::ATTACKER), 2);
		advanceRound();
		activate(braced->unitId());
	}
	for(const auto type : {BonusType::NON_LIVING, BonusType::MECHANICAL, BonusType::UNDEAD, BonusType::SIEGE_WEAPON})
	{
		const auto flag = std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, type,
			BonusSource::OTHER, 0, BonusSourceID());
		attackerSideHero->addNewBonus(flag);
		EXPECT_EQ(attackerSideHero->moraleVal(), 0);
		EXPECT_EQ(battle()->battleGetHeroMorale(BattleSide::ATTACKER), attackerSideHero->moraleVal());
		attackerSideHero->removeBonus(flag);
	}
}

TEST_F(HeroicSpiritProjectionTest, OwnedHeroFlagsRemainBranchLocalAndNeverRewriteIssuedSnapshots)
{
	ASSERT_NO_FATAL_FAILURE(prepareSpirit(false));
	releaseKnown();
	auto floor = std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::MINIMUM_MORALE,
		BonusSource::OTHER, 4, BonusSourceID());
	floor->valType = BonusValueType::INDEPENDENT_MAX;
	attackerSideHero->addNewBonus(floor);
	ASSERT_EQ(battle()->battleGetHeroMorale(BattleSide::ATTACKER), attackerSideHero->moraleVal());
	ASSERT_EQ(attackerSideHero->moraleVal(), 4);
	ASSERT_NO_FATAL_FAILURE(captureOwned());
	ASSERT_NO_FATAL_FAILURE(startOwned());
	const auto native = gameState()->saveToMemory();
	auto child = std::make_shared<HypotheticBattle>(environment.get(), known);
	auto sibling = std::make_shared<HypotheticBattle>(environment.get(), known);
	ASSERT_NO_FATAL_FAILURE(acceptFocus(*known, 20, 23));
	const auto parentOrder = known->getHeroOrderState(BattleSide::ATTACKER);
	auto noMorale = heroMorale(0);
	noMorale.type = BonusType::NO_MORALE;
	auto maximum = noMorale;
	maximum.type = BonusType::MAX_MORALE;
	child->addUnitBonus(braced->unitId(), {noMorale});
	sibling->addUnitBonus(braced->unitId(), {noMorale, maximum});
	EXPECT_EQ(child->battleGetHeroMorale(BattleSide::ATTACKER), 0);
	EXPECT_EQ(sibling->battleGetHeroMorale(BattleSide::ATTACKER), 10);
	ASSERT_NO_FATAL_FAILURE(acceptFocus(*child, 0, 20));
	ASSERT_NO_FATAL_FAILURE(acceptFocus(*sibling, 30, 25));
	const auto childOrder = child->getHeroOrderState(BattleSide::ATTACKER);
	const auto siblingOrder = sibling->getHeroOrderState(BattleSide::ATTACKER);
	auto descendant = std::make_shared<HypotheticBattle>(environment.get(), child);
	child->removeUnitBonus(braced->unitId(), {noMorale});
	sibling->removeUnitBonus(braced->unitId(), {maximum});
	EXPECT_EQ(child->battleGetHeroMorale(BattleSide::ATTACKER), 4);
	EXPECT_EQ(sibling->battleGetHeroMorale(BattleSide::ATTACKER), 0);
	EXPECT_EQ(descendant->battleGetHeroMorale(BattleSide::ATTACKER), 0);
	EXPECT_EQ(known->battleGetHeroMorale(BattleSide::ATTACKER), 4);
	EXPECT_EQ(known->getHeroOrderState(BattleSide::ATTACKER), parentOrder);
	EXPECT_EQ(child->getHeroOrderState(BattleSide::ATTACKER), childOrder);
	EXPECT_EQ(descendant->getHeroOrderState(BattleSide::ATTACKER), childOrder);
	EXPECT_EQ(sibling->getHeroOrderState(BattleSide::ATTACKER), siblingOrder);
	EXPECT_EQ(gameState()->saveToMemory(), native);
}
