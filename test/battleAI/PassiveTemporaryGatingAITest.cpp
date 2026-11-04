/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../server/battles/BattleTestFixture.h"
#include "../../AI/BattleAI/TemporaryGatingProjection.h"
#include "../../AI/BattleAI/HypotheticBattle.h"
#include "../../AI/BattleAI/AttackPossibility.h"
#include "../../AI/BattleAI/FixedActionValue.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/callback/GameRandomizer.h"
#include "../../lib/gameState/CGameState.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/mapping/CMap.h"
#include "../../lib/modding/CModHandler.h"
#include "../../lib/networkPacks/StackLocation.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include "../../server/CGameHandler.h"
namespace
{
class GateEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit GateEnvironment(std::shared_ptr<CGameState> source) : state(std::move(source)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};
class PassiveTemporaryGatingAITest : public BattleTestFixture
{
protected:
	void mapLoaded(CMap * map) override
	{
		BattleTestFixture::mapLoaded(map);
		for(const auto & object : map->objects)
			if(auto * hero = dynamic_cast<CGHeroInstance *>(object.get()); hero && hero->getOwner() == PlayerColor(0))
				hero->setHeroType(HeroTypeID(HeroTypeID::decode("core:calh")));
	}
	void SetUp() override
	{
		BattleTestFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires New Horizons content";
		startGame();
		const auto imp = CreatureID(CreatureID::decode("core:imp"));
		ASSERT_TRUE(gameHandler->changeStackType(StackLocation(attackerSideHero->id, SlotID(0)), imp.toCreature()));
		ASSERT_TRUE(gameHandler->changeStackCount(StackLocation(attackerSideHero->id, SlotID(0)), 12, ChangeValueMode::ABSOLUTE));
		attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:demonicGating")),
			MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		auto & perks = const_cast<newHorizonsHeroes::PerkState &>(attackerSideHero->getPerkState());
		perks.rules["demonicGatingPolicy"]["version"].Integer() = 2;
		perks.selected.clear();
		attackerSideHero->setDemonicReserve({});
		startBattle();
		for(const auto & owned : battle()->stacks)
		{
			auto * unit = owned.get();
			if(unit->isTurret() || unit->isGhost()) continue;
			BattleStackMoved move;
			move.battleID = BattleID(0);
			move.stack = unit->unitId();
			move.teleporting = true;
			move.tilesToMove.insert(BattleHex(unit->unitSide() == BattleSide::ATTACKER ? leftHex : rightHex));
			gameHandler->sendAndApply(move);
			// BLOCKS_RETALIATION protects an attacker from its target; this
			// fixture instead needs the defender to be unable to retaliate.
			if(unit->unitSide() == BattleSide::DEFENDER)
				unit->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
					BonusType::NO_RETALIATION, BonusSource::OTHER, 0, BonusSourceID()));
		}
		beginCombat();
	}
	std::vector<std::byte> randomState()
	{
		CMemorySerializer wire;
		wire.oser & *gameHandler->randomizer;
		wire.oser & dynamic_cast<CRandomGenerator &>(*getRNG());
		wire.oser & CRandomGenerator::getDefault();
		return wire.extractBuffer();
	}
};
}
TEST_F(PassiveTemporaryGatingAITest, ShortlistIsDeterministicBoundedAndNativelyLegal)
{
	const auto * source = battle()->battleActiveUnit();
	ASSERT_EQ(source->unitSide(), BattleSide::ATTACKER);
	const auto rng = randomState();
	temporaryGatingAI::Workload first, second;
	const auto a = temporaryGatingAI::candidates(*battle(), *source, BattleSide::ATTACKER, &first);
	const auto b = temporaryGatingAI::candidates(*battle(), *source, BattleSide::ATTACKER, &second);
	ASSERT_FALSE(a.empty());
	ASSERT_EQ(a.size(), b.size());
	EXPECT_LE(first.nativePlans, temporaryGatingAI::DEPLOYMENT_LIMIT);
	EXPECT_LE(first.geometryHexes, GameConstants::BFIELD_SIZE);
	for(size_t i = 0; i < a.size(); ++i)
	{
		EXPECT_EQ(a[i].action, b[i].action);
		const auto planned = newHorizonsDemonicGating::plan(*battle(), a[i].action);
		ASSERT_TRUE(planned);
		EXPECT_TRUE(planned->temporary);
		EXPECT_EQ(planned->creature, source->creatureId());
	}
	EXPECT_EQ(randomState(), rng);
	EXPECT_FALSE(source->hasOpenedDemonicGate());
	EXPECT_TRUE(attackerSideHero->getDemonicReserve().empty());
}
TEST_F(PassiveTemporaryGatingAITest, UsefulArrivalIsValuedWithoutMutatingArmyOrRandomState)
{
	auto environment = std::make_shared<GateEnvironment>(gameState());
	auto visible = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	auto parent = std::make_shared<HypotheticBattle>(environment.get(), visible);
	const auto * source = battle()->battleActiveUnit();
	const auto rng = randomState();
	const auto count = source->getCount();
	const auto roster = battle()->stacks.size();
	DamageCache damage;
	damage.buildDamageCache(parent, BattleSide::ATTACKER);
	temporaryGatingAI::Workload work;
	const auto action = temporaryGatingAI::projectedChoice(environment.get(), parent, *source,
		BattleSide::ATTACKER, damage, 0, 3, &work);
	ASSERT_TRUE(action) << "owned=" << parent->usesOwnedFrameQueries()
		<< " nativePlans=" << work.nativePlans << " projected=" << work.postMovementPlans
		<< " attacks=" << work.arrivingAttacks << " scored=" << work.scoredPlans;
	EXPECT_TRUE(newHorizonsDemonicGating::plan(*battle(), *action));
	EXPECT_LE(work.nativePlans, temporaryGatingAI::DEPLOYMENT_LIMIT);
	EXPECT_LE(work.postMovementPlans, temporaryGatingAI::DEPLOYMENT_LIMIT);
	EXPECT_LE(work.arrivingAttacks, temporaryGatingAI::DEPLOYMENT_LIMIT);
	EXPECT_EQ(source->getCount(), count);
	EXPECT_EQ(battle()->stacks.size(), roster);
	EXPECT_TRUE(attackerSideHero->getDemonicReserve().empty());
	EXPECT_FALSE(source->hasOpenedDemonicGate());
	EXPECT_EQ(randomState(), rng);
}

TEST_F(PassiveTemporaryGatingAITest, UnchangedDetachedOutcomeHasZeroMarginalPressure)
{
	auto environment = std::make_shared<GateEnvironment>(gameState());
	auto visible = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	auto parent = std::make_shared<HypotheticBattle>(environment.get(), visible);
	DamageCache damage;
	damage.buildDamageCache(parent, BattleSide::ATTACKER);
	fixedActionValue::Reference reference(parent, PlayerColor(0), damage, 1, 1);
	auto outcome = std::make_shared<HypotheticBattle>(environment.get(), parent);
	std::vector<uint32_t> recipients;
	for(const auto * unit : parent->battleGetAllUnits(false)) recipients.push_back(unit->unitId());
	const auto value = reference.marginal(outcome, recipients);
	ASSERT_TRUE(value);
	EXPECT_FLOAT_EQ(*value, 0);
}

TEST_F(PassiveTemporaryGatingAITest, ForcedAttackRecipeRetainsNoTemporaryCandidatePointers)
{
	auto * actor = battle()->getStack(battle()->battleActiveUnit()->unitId());
	Bonus forced(BonusDuration::N_TURNS, BonusType::ATTACKS_NEAREST_CREATURE,
		BonusSource::SPELL_EFFECT, 1, BonusSourceID(SpellID(SpellID::BERSERK)));
	forced.turnsRemain = 2;
	actor->addNewBonus(std::make_shared<Bonus>(forced));
	ASSERT_NE(addStack(BattleSide::ATTACKER, actor->creatureId(), BattleHex(leftHex - 1), 1), nullptr);
	for(const auto * unit : battle()->battleGetAllUnits(false))
		if(unit->unitSide() == BattleSide::DEFENDER)
		{
			BattleStackMoved move;
			move.battleID = BattleID(0);
			move.stack = unit->unitId();
			move.teleporting = true;
			move.tilesToMove.insert(BattleHex(rightHex + 4));
			gameHandler->sendAndApply(move);
		}
	auto environment = std::make_shared<GateEnvironment>(gameState());
	auto visible = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	auto parent = std::make_shared<HypotheticBattle>(environment.get(), visible);
	DamageCache damage;
	damage.buildDamageCache(parent, BattleSide::ATTACKER);
	fixedActionValue::Reference reference(parent, PlayerColor(0), damage, 1, 1);
	auto outcome = std::make_shared<HypotheticBattle>(environment.get(), parent);
	std::vector<uint32_t> recipients;
	for(const auto * unit : parent->battleGetAllUnits(false)) recipients.push_back(unit->unitId());
	const auto value = reference.marginal(outcome, recipients);
	ASSERT_TRUE(value);
	EXPECT_FLOAT_EQ(*value, 0);
}

TEST_F(PassiveTemporaryGatingAITest, PassiveAdvancedPerksKeepSinglePlacementSearchAndUseNativeArrivalBonuses)
{
	attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:demonicGating")),
		MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
	const auto * source = battle()->battleActiveUnit();
	const auto rng = randomState();
	for(const auto * perk : {"infernalOnslaught", "hellstride"})
	{
		SCOPED_TRACE(perk);
		auto & selected = const_cast<newHorizonsHeroes::PerkState &>(attackerSideHero->getPerkState());
		selected.selected.clear();
		attackerSideHero->applyPerkSelection({"new-horizons:demonicGating", "new-horizons:demonicGating.wideGate"});
		attackerSideHero->applyPerkSelection({"new-horizons:demonicGating",
			std::string("new-horizons:demonicGating.") + perk});
		auto environment = std::make_shared<GateEnvironment>(gameState());
		auto visible = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
		auto parent = std::make_shared<HypotheticBattle>(environment.get(), visible);
		DamageCache damage;
		damage.buildDamageCache(parent, BattleSide::ATTACKER);
		temporaryGatingAI::Workload work;
		const auto action = temporaryGatingAI::projectedChoice(environment.get(), parent, *source,
			BattleSide::ATTACKER, damage, 0, 3, &work);
		ASSERT_TRUE(action);
		EXPECT_EQ(action->target.size(), 1u);
		EXPECT_LE(work.nativePlans, 8);
		EXPECT_LE(work.postMovementPlans, 8);
		EXPECT_LE(work.arrivingAttacks, 8);
		EXPECT_EQ(work.movementPreviews, 0);
		EXPECT_TRUE(newHorizonsDemonicGating::plan(*battle(), *action));
		EXPECT_FALSE(source->hasOpenedDemonicGate());
		EXPECT_EQ(randomState(), rng);
	}
}

TEST_F(PassiveTemporaryGatingAITest, TemporaryPolicyDoesNotSearchMovementForARetainedHistoricalMobilePerk)
{
	const auto rng = randomState();
	auto & state = const_cast<newHorizonsHeroes::PerkState &>(attackerSideHero->getPerkState());
	const JsonNode historical(JsonPath::builtin("test/newHorizonsHistoricalFactionPerks"));
	state.rules["skills"]["new-horizons:demonicGating"] = historical["skills"]["new-horizons:demonicGating"];
	state.selected.clear();
	attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:demonicGating")),
		MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
	attackerSideHero->applyPerkSelection({"new-horizons:demonicGating", "new-horizons:demonicGating.wideGate"});
	attackerSideHero->applyPerkSelection({"new-horizons:demonicGating", "new-horizons:demonicGating.mobileGate"});
	ASSERT_TRUE(newHorizonsDemonicGating::usesTemporaryGating(attackerSideHero));
	ASSERT_TRUE(attackerSideHero->hasActivePerk("new-horizons:demonicGating", "new-horizons:demonicGating.mobileGate"));
	const auto * source = battle()->battleActiveUnit();
	temporaryGatingAI::Workload work;
	const auto candidates = temporaryGatingAI::candidates(*battle(), *source, BattleSide::ATTACKER, &work);
	ASSERT_FALSE(candidates.empty());
	RecordProperty("geometryHexes", work.geometryHexes);
	RecordProperty("nativePlans", work.nativePlans);
	EXPECT_LE(work.geometryHexes, GameConstants::BFIELD_SIZE);
	EXPECT_LE(work.nativePlans, temporaryGatingAI::DEPLOYMENT_LIMIT);
	for(const auto & candidate : candidates)
		EXPECT_EQ(candidate.action.target.size(), 1u);
	EXPECT_EQ(randomState(), rng);
}
