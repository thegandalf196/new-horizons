/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "RunicReflectionFixture.h"

namespace
{
// Both paired effects own their targeting and resistance handling. Choose a
// seed whose first bounded samples all resist the native portion, independent
// of whether that canonical owner excludes friendly recipients from its rolls.
void seedNativeResistance(CGameHandler & handler, const CGameState & state)
{
	for(int seed = 0; seed < 4096; ++seed)
	{
		GameRandomizer probe(state); probe.setSeed(seed);
		bool native = true;
		for(int i = 0; i < 3; ++i) native &= probe.getDefault().nextInt(0, 99) < 40;
		if(native) { handler.randomizer->setSeed(seed); return; }
	}
	throw std::logic_error("No paired resistance fixture seed");
}
}

TEST_F(NewHorizonsRunicReflectionCombatTest, SoulChainResistsWithoutCreatingAnIllegalOneEndedReflectedLink)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const SpellID spell(SpellID::decode("new-horizons:soulChain"));
	attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:shadowMagic")), 3, ChangeValueMode::ABSOLUTE);
	attackerSideHero->addSpellToSpellbook(spell);
	auto * second = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(73), 1000);
	const auto firstBefore = dwarf->save(), secondBefore = second->save(), ownBefore = casterArmy->save();
	const auto mana = attackerSideHero->getManaAvailable();
	seedNativeResistance(*gameHandler, *gameState());
	BattleAction action; action.actionType = EActionType::HERO_SPELL;
	action.side = BattleSide::ATTACKER; action.spell = spell;
	action.aimToUnit(dwarf); action.aimToUnit(second);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_EQ(dwarf->save(), firstBefore);
	EXPECT_EQ(second->save(), secondBefore);
	EXPECT_EQ(casterArmy->save(), ownBefore);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana - 12);
	const auto casts = server.castsOf(spell);
	ASSERT_EQ(casts.size(), 1u);
	EXPECT_TRUE(casts.front().announcement.reflectedCres.empty());
	EXPECT_EQ(battle()->getHeroActionAllowances(BattleSide::ATTACKER).remainingCounts(battle()->getRound()).heroActions, 0u);
}

TEST_F(NewHorizonsRunicReflectionCombatTest, RealityWarpResistsWithoutFabricatingAReciprocalReflectedEndpoint)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const SpellID spell(SpellID::decode("new-horizons:realityWarp"));
	attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:chaosMagic")), 3, ChangeValueMode::ABSOLUTE);
	attackerSideHero->addSpellToSpellbook(spell);
	for(const auto & [unit, spellId, type, value] : {
		std::tuple{casterArmy, SpellID::HASTE, BonusType::STACKS_SPEED, 7},
		std::tuple{dwarf, SpellID::SLOW, BonusType::STACKS_INITIATIVE, -25}})
	{
		auto bonus = std::make_shared<Bonus>(BonusDuration::N_TURNS,
			type, BonusSource::SPELL_EFFECT, value, BonusSourceID(SpellID(spellId)));
		bonus->turnsRemain = 3;
		unit->addNewBonus(bonus);
	}
	const auto firstBefore = dwarf->save(), ownBefore = casterArmy->save();
	const auto mana = attackerSideHero->getManaAvailable();
	seedNativeResistance(*gameHandler, *gameState());
	BattleAction action; action.actionType = EActionType::HERO_SPELL;
	action.side = BattleSide::ATTACKER; action.spell = spell;
	action.aimToUnit(casterArmy); action.aimToUnit(dwarf);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_EQ(dwarf->save(), firstBefore);
	EXPECT_EQ(casterArmy->save(), ownBefore);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana - 15);
	const auto casts = server.castsOf(spell);
	ASSERT_EQ(casts.size(), 1u);
	EXPECT_TRUE(casts.front().announcement.reflectedCres.empty());
	EXPECT_EQ(battle()->getHeroActionAllowances(BattleSide::ATTACKER).remainingCounts(battle()->getRound()).heroActions, 0u);
}
