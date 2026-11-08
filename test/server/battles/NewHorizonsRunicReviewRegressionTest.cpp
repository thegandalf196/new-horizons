/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "RunicReflectionFixture.h"

TEST_F(NewHorizonsRunicReflectionCombatTest, ResistedHandOfFatePrimaryReturnsOnlyItsPrimaryShareWithoutAnotherSpillDraw)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const SpellID spell(SpellID::decode("new-horizons:handOfFate"));
	attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:chaosMagic")), 3, ChangeValueMode::ABSOLUTE);
	attackerSideHero->addSpellToSpellbook(spell);
	const auto expectedRng = chooseSample(0, 40, true);
	const auto ownHealth = casterArmy->getAvailableHealth(), nativeHealth = dwarf->getAvailableHealth();
	const auto mana = attackerSideHero->getManaAvailable();
	const auto injuriesBefore = server.injuries.size();
	BattleAction action; action.actionType = EActionType::HERO_SPELL;
	action.side = BattleSide::ATTACKER; action.spell = spell; action.aimToUnit(dwarf);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_EQ(dwarf->getAvailableHealth(), nativeHealth);
	EXPECT_EQ(casterArmy->getAvailableHealth(), ownHealth - 70);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana - 12);
	std::vector<uint32_t> injuredRecipients;
	for(size_t i = injuriesBefore; i < server.injuries.size(); ++i)
		for(const auto & hit : server.injuries[i].stacks) injuredRecipients.push_back(hit.stackAttacked);
	EXPECT_EQ(injuredRecipients, std::vector<uint32_t>{casterArmy->unitId()});
	const auto casts = server.castsOf(spell);
	ASSERT_EQ(casts.size(), 1u);
	EXPECT_TRUE(casts.front().announcement.reflectedCres.contains(dwarf->unitId()));
	EXPECT_EQ(wire(*gameHandler->randomizer), expectedRng)
		<< "A reflected primary must not choose or resolve a new collateral recipient";
	EXPECT_EQ(battle()->getHeroActionAllowances(BattleSide::ATTACKER).remainingCounts(battle()->getRound()).heroActions, 0u);
}

TEST_F(NewHorizonsRunicReflectionCombatTest, PandemoniumFullResistanceRemainsResistanceDespiteNativeReflectionAndCapturedDebuffs)
{
	ASSERT_NO_FATAL_FAILURE(prepare(60));
	ASSERT_EQ(dwarf->magicResistance(), 100);
	const SpellID spell(SpellID::decode("new-horizons:pandemonium"));
	attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:chaosMagic")), 3, ChangeValueMode::ABSOLUTE);
	attackerSideHero->addSpellToSpellbook(spell);
	Bonus slow(BonusDuration::N_TURNS, BonusType::STACKS_INITIATIVE,
		BonusSource::SPELL_EFFECT, -25, BonusSourceID(SpellID(SpellID::SLOW)));
	slow.turnsRemain = 3; slow.statusTags = {BonusStatusTag::DEBUFF}; slow.statusIdentity = "slow";
	SetStackEffect statuses; statuses.battleID = BattleID(0);
	statuses.toAdd.emplace_back(dwarf->unitId(), std::vector<Bonus>{slow});
	statuses.toAdd.emplace_back(casterArmy->unitId(), std::vector<Bonus>{slow});
	gameHandler->sendAndApply(statuses);
	ASSERT_EQ(newHorizonsDebuffStatuses::snapshot(*dwarf).count(), 1u);
	ASSERT_EQ(newHorizonsDebuffStatuses::snapshot(*casterArmy).count(), 1u);
	const auto expectedRng = chooseSample(0, 40, false);
	const auto ownHealth = casterArmy->getAvailableHealth(), nativeHealth = dwarf->getAvailableHealth();
	BattleAction action; action.actionType = EActionType::HERO_SPELL;
	action.side = BattleSide::ATTACKER; action.spell = spell;
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_EQ(dwarf->getAvailableHealth(), nativeHealth);
	EXPECT_EQ(casterArmy->getAvailableHealth(), ownHealth - 20)
		<< "Only the ordinary own-army Pandemonium share may resolve";
	const auto casts = server.castsOf(spell); ASSERT_EQ(casts.size(), 1u);
	EXPECT_TRUE(casts.front().announcement.reflectedCres.empty());
	EXPECT_EQ(wire(*gameHandler->randomizer), expectedRng);
}

TEST_F(NewHorizonsRunicReflectionCombatTest, NaturesWrathFullResistanceDoesNotReturnAHostileHopToItsFriendlyHealingRoute)
{
	ASSERT_NO_FATAL_FAILURE(prepare(60));
	ASSERT_EQ(dwarf->magicResistance(), 100);
	const SpellID spell(SpellID::decode(std::string(newHorizonsNaturesWrath::SPELL_KEY)));
	attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:natureMagic")), 3, ChangeValueMode::ABSOLUTE);
	attackerSideHero->addSpellToSpellbook(spell);
	std::vector<std::byte> expectedRng;
	for(int seed = 0; seed < 4096 && expectedRng.empty(); ++seed)
	{
		GameRandomizer expected(*gameState()); expected.setSeed(seed);
		if(expected.getDefault().nextInt(0, 99) >= 40) continue;
		expectedRng = wire(expected); gameHandler->randomizer->setSeed(seed);
	}
	ASSERT_FALSE(expectedRng.empty());
	const auto ownHealth = casterArmy->getAvailableHealth(), nativeHealth = dwarf->getAvailableHealth();
	BattleAction action; action.actionType = EActionType::HERO_SPELL;
	action.side = BattleSide::ATTACKER; action.spell = spell; action.aimToUnit(casterArmy);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_EQ(dwarf->getAvailableHealth(), nativeHealth);
	EXPECT_EQ(casterArmy->getAvailableHealth(), ownHealth);
	const auto casts = server.castsOf(spell); ASSERT_EQ(casts.size(), 1u);
	EXPECT_TRUE(casts.front().announcement.reflectedCres.empty());
	EXPECT_EQ(wire(*gameHandler->randomizer), expectedRng);
}

TEST_F(NewHorizonsRunicReflectionCombatTest, OrdinaryMagicMirrorRetainsItsExistingSecondBounceWithoutAnyRunicParticipant)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	BattleUnitsChanged remove; remove.battleID = BattleID(0);
	remove.changedStacks.emplace_back(dwarf->unitId(), UnitChanges::EOperation::REMOVE);
	gameHandler->sendAndApply(remove);
	dwarf = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(72), 1000);
	ASSERT_EQ(newHorizonsRunicReflection::nativeChance(*dwarf), 0);
	ASSERT_EQ(newHorizonsRunicReflection::nativeChance(*casterArmy), 0);
	for(auto * unit : {casterArmy, dwarf})
		unit->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
			BonusType::MAGIC_MIRROR, BonusSource::OTHER, 50, BonusSourceID()));
	std::vector<std::byte> expectedRng;
	for(int seed = 0; seed < 4096 && expectedRng.empty(); ++seed)
	{
		GameRandomizer expected(*gameState()); expected.setSeed(seed);
		bool matched = true;
		for(int bounce = 0; bounce < 3; ++bounce)
		{
			for(const auto * unit : battle()->battleGetAllUnits(false))
				(void)expected.getDefault().nextInt(0, 99);
			const bool reflects = expected.getDefault().nextInt(0, 99) < 50;
			if(reflects != (bounce < 2)) { matched = false; break; }
			if(reflects)
			{
				const std::vector<const battle::Unit *> pool{bounce == 0 ? casterArmy : dwarf};
				(void)RandomGeneratorUtil::nextItem(pool, expected.getDefault());
			}
		}
		if(matched) { expectedRng = wire(expected); gameHandler->randomizer->setSeed(seed); }
	}
	ASSERT_FALSE(expectedRng.empty());
	const auto ownHealth = casterArmy->getAvailableHealth(), enemyHealth = dwarf->getAvailableHealth();
	const auto mana = attackerSideHero->getManaAvailable();
	ASSERT_TRUE(castArrow());
	EXPECT_EQ(casterArmy->getAvailableHealth(), ownHealth);
	EXPECT_EQ(dwarf->getAvailableHealth(), enemyHealth - 20);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana - 4);
	const auto casts = server.castsOf(SpellID::MAGIC_ARROW); ASSERT_EQ(casts.size(), 1u);
	EXPECT_TRUE(casts.front().announcement.reflectedCres.contains(dwarf->unitId()));
	EXPECT_TRUE(casts.front().announcement.reflectedCres.contains(casterArmy->unitId()));
	EXPECT_EQ(wire(*gameHandler->randomizer), expectedRng);
}
