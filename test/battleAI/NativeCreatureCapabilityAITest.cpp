/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "HeroQueueSelectorFixture.h"
#include "../../AI/BattleAI/PotentialTargets.h"
#include "../../AI/BattleAI/CreatureSpellTiming.h"
#include "../../lib/battle/NewHorizonsCreatureSpecialtyCapabilities.h"
#include "../../lib/battle/NewHorizonsCreatureAbilitySuppression.h"
#include "../../lib/nativeRules/NativeRulePilots.h"
#include "../../lib/spells/BattleSpellMechanics.h"
#include "../../lib/spells/CSpell.h"
using namespace heroQueueTest;
class NativeCreatureCapabilityAITest : public HeroQueueSelectorTest
{
protected:
	NativeCreatureCapabilityAITest() { useRevisedOrders = true; }
	void prepare(const char * hero)
	{
		ASSERT_NO_FATAL_FAILURE(world());
		attackerSideHero->setHeroType(HeroTypeID(HeroTypeID::decode(hero)));
		ASSERT_NO_FATAL_FAILURE(arena());
	}
	void branches()
	{
		environment = std::make_shared<QueueEnvironment>(gameState());
		live = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
		parent = std::make_shared<HypotheticBattle>(environment.get(), live);
		child = std::make_shared<HypotheticBattle>(environment.get(), parent);
		sibling = std::make_shared<HypotheticBattle>(environment.get(), parent);
	}
};
TEST_F(NativeCreatureCapabilityAITest, StonecallerUsesOrdinaryShotCandidatesAndSpendsOnlyProjectedAmmunition)
{
	ASSERT_NO_FATAL_FAILURE(prepare("core:thunar"));
	const auto * earth = addStack(BattleSide::ATTACKER, creatureByName("core:earthElemental"), BattleHex(6, 3), 10);
	ASSERT_NE(earth, nullptr);
	ASSERT_NO_FATAL_FAILURE(branches());
	const auto outside = observation();
	const auto source = child->getForUpdate(earth->unitId());
	ASSERT_TRUE(newHorizonsCreatureSpecialtyCapabilities::stonecallerRanged(*source));
	ASSERT_EQ(source->shots.available(), 12);
	DamageCache cache;
	PotentialTargets targets(source.get(), cache, child);
	const auto shot = std::ranges::find_if(targets.possibleAttacks, [&](const auto & candidate)
		{ return candidate.attack.shooting && candidate.attack.defender->unitId() == enemy->unitId(); });
	ASSERT_NE(shot, targets.possibleAttacks.end());
	ASSERT_NE(shot->attackerState, nullptr);
	EXPECT_EQ(shot->attackerState->shots.available(), 11);
	EXPECT_EQ(source->shots.available(), 12);
	EXPECT_EQ(parent->getForUpdate(earth->unitId())->shots.available(), 12);
	EXPECT_EQ(sibling->getForUpdate(earth->unitId())->shots.available(), 12);
	EXPECT_EQ(observation(), outside);
}
TEST_F(NativeCreatureCapabilityAITest, ExhaustedStonecallerRetainsMeleeAndStableCapabilityWithoutShooting)
{
	ASSERT_NO_FATAL_FAILURE(prepare("core:thunar"));
	const auto * magma = addStack(BattleSide::ATTACKER, creatureByName("core:magmaElemental"), BattleHex(6, 3), 10);
	ASSERT_NE(magma, nullptr);
	ASSERT_NO_FATAL_FAILURE(branches());
	const auto outside = observation();
	const auto source = child->getForUpdate(magma->unitId());
	source->shots.use(source->shots.available());
	ASSERT_TRUE(newHorizonsCreatureSpecialtyCapabilities::stonecallerRanged(*source));
	EXPECT_FALSE(source->canShoot());
	EXPECT_TRUE(source->hasBonusOfType(BonusType::NO_MELEE_PENALTY));
	DamageCache cache;
	PotentialTargets targets(source.get(), cache, child);
	EXPECT_FALSE(std::ranges::any_of(targets.possibleAttacks, [](const auto & candidate) { return candidate.attack.shooting; }));
	EXPECT_FALSE(targets.possibleAttacks.empty());
	EXPECT_EQ(observation(), outside);
}
TEST_F(NativeCreatureCapabilityAITest, ArchshamanFinderEvaluatesThreeActualSpellsWithoutHeroPowerOrRandomDraw)
{
	ASSERT_NO_FATAL_FAILURE(prepare("core:vey"));
	auto * ogre = addStack(BattleSide::ATTACKER, creatureByName("core:ogreMage"), BattleHex(6, 3), 10);
	ASSERT_NE(ogre, nullptr);
	// An adjacent durable recipient keeps Bloodlust's ordinary attack-pressure
	// gain observable instead of capping every improved hit at a tiny stack's HP.
	ASSERT_NE(addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(8, 6), 10000), nullptr);
	ASSERT_NO_FATAL_FAILURE(activate(ogre));
	ASSERT_NO_FATAL_FAILURE(branches());
	callback = std::make_shared<QueueCallback>(); callback->onBattleStarted(battle());
	BattleEvaluator evaluator(environment, callback, ogre, PlayerColor(0), BattleID(0), BattleSide::ATTACKER, 1, 2);
	const auto outside = observation();
	const auto aiRng = wire(CRandomGenerator::getDefault());
	const auto charges = ogre->casts.available();
	ASSERT_EQ(charges, 3);
	for(const auto spell : {SpellID(SpellID::BLOODLUST), SpellID(SpellID::BERSERK), SpellID(SpellID::MISFORTUNE)})
	{
		EXPECT_EQ(ogre->getSpellSchoolLevel(spell.toSpell()), 2);
		EXPECT_EQ(ogre->getEffectPower(spell.toSpell()), 0);
		spells::BattleCast cast(live.get(), ogre, spells::Mode::CREATURE_ACTIVE, spell.toSpell());
		const auto mechanics = spell.toSpell()->battleMechanics(&cast);
		const auto aim = spell == SpellID::BLOODLUST ? actor : enemy;
		EXPECT_TRUE(mechanics->canBeCastAt(spells::Target{spells::Destination(aim)}));
	}
	creatureSpellTiming::Sample timing;
	const auto best = evaluator.findBestCreatureSpell(ogre, &timing);
	EXPECT_EQ(timing.ordinarySpells, 3u);
	EXPECT_GE(timing.ordinaryTargets, 3u);
	EXPECT_EQ(ogre->casts.available(), charges);
	EXPECT_EQ(wire(CRandomGenerator::getDefault()), aiRng);
	EXPECT_EQ(observation(), outside);
	ASSERT_TRUE(best);
	EXPECT_GT(best->value, 0);
	EXPECT_TRUE(best->spell->getId() == SpellID::BLOODLUST || best->spell->getId() == SpellID::BERSERK
		|| best->spell->getId() == SpellID::MISFORTUNE);
	spells::BattleCast selected(live.get(), ogre, spells::Mode::CREATURE_ACTIVE, best->spell);
	EXPECT_TRUE(best->spell->battleMechanics(&selected)->canBeCastAt(best->dest));
}
TEST_F(NativeCreatureCapabilityAITest, ArchshamanSuppressionAndEmptyChargesKeepExistingCastAdmission)
{
	ASSERT_NO_FATAL_FAILURE(prepare("core:vey"));
	auto * ogre = addStack(BattleSide::ATTACKER, creatureByName("core:ogreMage"), BattleHex(6, 3), 10);
	ASSERT_NE(ogre, nullptr);
	ASSERT_NO_FATAL_FAILURE(activate(ogre));
	environment = std::make_shared<QueueEnvironment>(gameState());
	callback = std::make_shared<QueueCallback>(); callback->onBattleStarted(battle());
	const auto suppression = nativeRules::creatureSuppression(SpellID(SpellID::FORGETFULNESS), "creatureAbilitySuppression", 3, false);
	effect(ogre, suppression);
	const auto charges = ogre->casts.available();
	{
		BattleEvaluator suppressed(environment, callback, ogre, PlayerColor(0), BattleID(0), BattleSide::ATTACKER, 1, 2);
		EXPECT_FALSE(suppressed.findBestCreatureSpell(ogre));
		EXPECT_EQ(ogre->casts.available(), charges);
	}
	SetStackEffect remove; remove.battleID = BattleID(0); remove.toRemove.emplace_back(ogre->unitId(), std::vector<Bonus>{suppression});
	gameHandler->sendAndApply(remove);
	ogre->casts.use(ogre->casts.available());
	BattleEvaluator exhausted(environment, callback, ogre, PlayerColor(0), BattleID(0), BattleSide::ATTACKER, 1, 2);
	EXPECT_FALSE(exhausted.findBestCreatureSpell(ogre));
	EXPECT_EQ(ogre->casts.available(), 0);
}
