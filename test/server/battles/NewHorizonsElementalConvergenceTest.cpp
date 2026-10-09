/*
 * NewHorizonsElementalConvergenceTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "NewHorizonsElementalTerrainFixture.h"
#include "../../../AI/BattleAI/BattleEvaluator.h"
#include "../../../AI/BattleAI/SpellTargetsEvaluator.h"
#include "../../../AI/BattleAI/StackWithBonuses.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/callback/CBattleCallback.h"
#include "../../../lib/battle/CUnitState.h"
#include "../../../lib/battle/NewHorizonsElementalRebirth.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/BattleSpellMechanics.h"
#include "../../../lib/spells/ISpellMechanics.h"
#include "../../../lib/spells/NewHorizonsElementalTerrain.h"
#include "../../../lib/spells/NewHorizonsMagic.h"
#include "../../../lib/spells/effects/Effect.h"

class NewHorizonsElementalConvergenceTest : public NewHorizonsElementalTerrainFixture
{
protected:
	const BattleHex selected{8, 5};
	CStack * friendly = nullptr;
	static SpellID spell() { return SpellID(SpellID::decode("new-horizons:elementalConvergence")); }
	void expectPaidAI(TerrainId terrain, const std::string & arena, int spellPower);

	void prepare(TerrainId terrain = TerrainId::DIRT, const std::string & arena = "core:dirt_hills",
		bool healthArtifact = false, int forceCount = 20)
	{
		startGame();
		ASSERT_GE(spell().getNum(), 0);
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		attackerSideHero->addSpellToSpellbook(spell());
		attackerSideHero->addSpellToSpellbook(SpellID::HASTE);
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 41, ChangeValueMode::ABSOLUTE);
		attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
		setTestSpellPointTotal(attackerSideHero, 1000);
		if(healthArtifact)
			giveArtifact(attackerSideHero, ArtifactID(ArtifactID::decode("core:ringOfVitality")), ArtifactPosition::MISC1);
		startTerrainBattle(terrain, arena);
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
		friendly = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(2, 5), forceCount);
		ASSERT_NE(friendly, nullptr);
		ASSERT_NE(addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), BattleHex(14, 5), forceCount), nullptr);
		beginCombat();
		gameState()->getMap().getTile(int3(4, 4, 0)).terrainType = TerrainId::LAVA;
	}

	bool castAt(BattleHex hex)
	{
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = BattleSide::ATTACKER;
		action.spell = spell();
		action.aimToHex(hex);
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action);
	}

	const CStack * summoned() const
	{
		const CStack * result = nullptr;
		for(const auto * unit : battle()->battleGetStacksIf([](const CStack * stack)
			{ return stack->alive() && stack->isSummoned() && stack->natureSummoned; }))
		{
			if(result)
				return nullptr;
			result = unit;
		}
		return result;
	}
};

TEST_F(NewHorizonsElementalConvergenceTest, LegalSelectedPositionUsesCapturedTerrainExactPoolAndNativeAbilities)
{
	ASSERT_NO_FATAL_FAILURE(prepare(TerrainId::GRASS, "core:grass_hills", true));
	const auto armySizeBefore = attackerSideHero->Slots().size();
	const auto forecast = battle()->getSpellEffectValue(spell().toSpell(), attackerSideHero, spells::Mode::HERO, selected);
	ASSERT_NE(forecast, nullptr);
	EXPECT_EQ(forecast->hpDelta, 455);
	const auto type = creatureByName("core:airElemental");
	const auto effectiveHP = newHorizonsElementalRebirth::effectiveSummonMaxHP(
		attackerSideHero, type, attackerSideHero->getOwner(), BattleSide::ATTACKER);
	const auto * ordinary = addStack(BattleSide::ATTACKER, type, BattleHex(3, 5), 1);
	ASSERT_NE(ordinary, nullptr);
	EXPECT_EQ(effectiveHP, ordinary->getMaxHealth());
	EXPECT_EQ(forecast->unitsDelta, (455 + effectiveHP - 1) / effectiveHP);
	ASSERT_TRUE(castAt(selected));
	const auto * output = summoned();
	ASSERT_NE(output, nullptr);
	EXPECT_EQ(output->creatureId(), type);
	EXPECT_EQ(output->getPosition(), selected);
	EXPECT_EQ(output->getAvailableHealth(), 455);
	EXPECT_EQ(output->getCount(), forecast->unitsDelta);
	EXPECT_EQ(output->getMaxHealth(), effectiveHP);
	EXPECT_GT(ordinary->getAllBonuses(Selector::type()(BonusType::SPELL_IMMUNITY))->size(), 0u);
	EXPECT_EQ(output->getAllBonuses(Selector::type()(BonusType::SPELL_IMMUNITY))->size(),
		ordinary->getAllBonuses(Selector::type()(BonusType::SPELL_IMMUNITY))->size());
	EXPECT_EQ(output->getInitiative(), ordinary->getInitiative());
	EXPECT_EQ(output->getRebirthOriginalAggregateHP(), 0);
	EXPECT_EQ(attackerSideHero->Slots().size(), armySizeBefore);
	auto state = output->acquireState();
	const auto data = state->save();
	state->load(data);
	EXPECT_TRUE(state->summoned);
	EXPECT_TRUE(state->natureSummoned);
	EXPECT_EQ(state->getAvailableHealth(), 455);
}

TEST_F(NewHorizonsElementalConvergenceTest, NatureRankScalesOnlyPowerComponentAndPaidCastMatchesPreview)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const SecondarySkill nature(SecondarySkill::decode("new-horizons:natureMagic"));
	ASSERT_GE(nature.getNum(), 0);
	int64_t expertPool = 0;
	for(int rank = MasteryLevel::NONE; rank <= MasteryLevel::EXPERT; ++rank)
	{
		attackerSideHero->setSecSkillLevel(nature, rank, ChangeValueMode::ABSOLUTE);
		const auto coefficient = newHorizonsMagic::spellPowerCoefficientBasisPoints(
			battle()->getMagicRules(), attackerSideHero, spell());
		const auto expected = (25000 + spells::scaleSpellPowerComponentWithCoefficientBasisPoints(
			5LL * 41 * 100, 1, coefficient, 0, 0)) / 100;
		const auto forecast = battle()->getSpellEffectValue(spell().toSpell(), attackerSideHero, spells::Mode::HERO, selected);
		ASSERT_NE(forecast, nullptr);
		EXPECT_EQ(forecast->hpDelta, expected) << "Nature rank " << rank;
		expertPool = expected;
	}
	const auto mana = attackerSideHero->getManaAvailable();
	ASSERT_TRUE(castAt(selected));
	ASSERT_NE(summoned(), nullptr);
	EXPECT_EQ(summoned()->creatureId(), creatureByName("core:earthElemental"));
	EXPECT_EQ(summoned()->getAvailableHealth(), expertPool);
	EXPECT_LT(attackerSideHero->getManaAvailable(), mana);
}

TEST_F(NewHorizonsElementalConvergenceTest, CoastalSandOverridesInlandEarthAndIllegalTargetDoesNotSpendAction)
{
	ASSERT_NO_FATAL_FAILURE(prepare(TerrainId::SAND, "core:sand_shore"));
	const auto mana = attackerSideHero->getManaAvailable();
	EXPECT_FALSE(castAt(friendly->getPosition()));
	EXPECT_FALSE(castAt(BattleHex::INVALID));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana);
	EXPECT_EQ(summoned(), nullptr);
	ASSERT_TRUE(castAt(selected)) << "Rejected placements must leave the Hero Action available";
	ASSERT_NE(summoned(), nullptr);
	EXPECT_EQ(summoned()->creatureId(), creatureByName("core:waterElemental"));
	EXPECT_EQ(summoned()->getPosition(), selected);
}

TEST_F(NewHorizonsElementalConvergenceTest, SelectedConjurerScalesWholePoolAndInitiativeExpiresAfterFirstRound)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	constexpr auto natureKey = "new-horizons:natureMagic";
	const SecondarySkill nature(SecondarySkill::decode(natureKey));
	attackerSideHero->setSecSkillLevel(nature, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	attackerSideHero->applyPerkSelection({natureKey, "new-horizons:natureMagic.herbalist"});
	attackerSideHero->setSecSkillLevel(nature, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
	attackerSideHero->applyPerkSelection({natureKey, "new-horizons:natureMagic.geomancer"});
	attackerSideHero->setSecSkillLevel(nature, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	attackerSideHero->applyPerkSelection({natureKey, "new-horizons:natureMagic.elementalConjurer"});
	ASSERT_TRUE(attackerSideHero->hasActivePerk(natureKey, "new-horizons:natureMagic.elementalConjurer"));
	const auto coefficient = newHorizonsMagic::spellPowerCoefficientBasisPoints(
		battle()->getMagicRules(), attackerSideHero, spell());
	const auto expected = (250 * 130 + spells::scaleSpellPowerComponentWithCoefficientBasisPoints(
		5LL * 41 * 130, 1, coefficient, 0, 0)) / 100;
	const auto forecast = battle()->getSpellEffectValue(spell().toSpell(), attackerSideHero, spells::Mode::HERO, selected);
	ASSERT_NE(forecast, nullptr);
	EXPECT_EQ(forecast->hpDelta, expected);
	const auto * ordinary = addStack(BattleSide::ATTACKER, creatureByName("core:earthElemental"), BattleHex(3, 5), 1);
	ASSERT_NE(ordinary, nullptr);
	const auto baseInitiative = ordinary->getInitiative();
	ASSERT_TRUE(castAt(selected));
	ASSERT_NE(summoned(), nullptr);
	const auto id = summoned()->unitId();
	EXPECT_EQ(summoned()->getAvailableHealth(), expected);
	EXPECT_EQ(summoned()->getInitiative(), baseInitiative + 2);
	BattleNextRound next;
	next.battleID = BattleID(0);
	gameHandler->sendAndApply(next);
	const auto * later = battle()->battleGetUnitByID(id);
	ASSERT_NE(later, nullptr);
	EXPECT_EQ(later->getInitiative(), baseInitiative);
	EXPECT_EQ(later->getAvailableHealth(), expected);
}

namespace
{
class ConvergenceEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit ConvergenceEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};
class ConvergenceCallback final : public CBattleCallback
{
public:
	std::vector<BattleAction> submitted;
	ConvergenceCallback() : CBattleCallback(PlayerColor(0), nullptr) {}
	void battleMakeSpellAction(const BattleID &, const BattleAction & action) override { submitted.push_back(action); }
};
}

void NewHorizonsElementalConvergenceTest::expectPaidAI(TerrainId terrain,
	const std::string & arena, int spellPower)
{
	ASSERT_NO_FATAL_FAILURE(prepare(terrain, arena, false, 1));
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, spellPower, ChangeValueMode::ABSOLUTE);
	for(const auto known : attackerSideHero->getSpellsInSpellbook())
		if(known != spell())
			attackerSideHero->removeSpellFromSpellbook(known);
	Bonus immobilized;
	immobilized.type = BonusType::STACKS_SPEED;
	immobilized.duration = BonusDuration::ONE_BATTLE;
	immobilized.val = -friendly->getMovementRange();
	friendly->addNewBonus(std::make_shared<Bonus>(immobilized));
	BattleSetActiveStack activate;
	activate.battleID = BattleID(0);
	activate.stack = friendly->unitId();
	activate.reason = BattleUnitTurnReason::TURN_QUEUE;
	gameHandler->sendAndApply(activate);
	auto callback = std::make_shared<ConvergenceCallback>();
	callback->onBattleStarted(battle());
	auto environment = std::make_shared<ConvergenceEnvironment>(gameState());
	BattleEvaluator evaluator(environment, callback, friendly, PlayerColor(0), BattleID(0), BattleSide::ATTACKER, 1.0f, 2);
	evaluator.selectStackAction(friendly);
	const auto mana = attackerSideHero->getManaAvailable();
	ASSERT_TRUE(evaluator.attemptCastingSpell(friendly));
	ASSERT_EQ(callback->submitted.size(), 1u);
	const auto action = callback->submitted.front();
	ASSERT_EQ(action.spell, spell());
	const auto target = action.getTarget(battle());
	ASSERT_EQ(target.size(), 1u);
	ASSERT_EQ(target.front().unitValue, nullptr);
	const auto forecast = battle()->getSpellEffectValue(spell().toSpell(), attackerSideHero,
		spells::Mode::HERO, target.front().hexValue);
	ASSERT_NE(forecast, nullptr);
	ASSERT_GT(forecast->hpDelta, 0);
	HypotheticBattle projected(environment.get(), callback->getBattle(BattleID(0)));
	spells::BattleCast cast(&projected, attackerSideHero, spells::Mode::HERO, spell().toSpell());
	const auto mechanics = spell().toSpell()->battleMechanics(&cast);
	ASSERT_TRUE(mechanics->canBeCastAt(target));
	mechanics->castEval(projected.getServerCallback(), target);
	const battle::Unit * predicted = nullptr;
	for(const auto * unit : projected.battleGetAllUnits(false))
		if(unit->isSummoned() && unit->acquireState()->natureSummoned)
			predicted = unit;
	ASSERT_NE(predicted, nullptr);
	const auto expectedType = newHorizonsElementalTerrain::primaryElemental(*battle());
	ASSERT_TRUE(expectedType);
	EXPECT_EQ(predicted->creatureId(), *expectedType);
	EXPECT_EQ(predicted->getAvailableHealth(), forecast->hpDelta);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana);
	EXPECT_EQ(summoned(), nullptr);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	ASSERT_NE(summoned(), nullptr);
	EXPECT_EQ(summoned()->getPosition(), target.front().hexValue);
	EXPECT_EQ(summoned()->getAvailableHealth(), predicted->getAvailableHealth());
	EXPECT_EQ(summoned()->getCount(), predicted->getCount());
	EXPECT_LT(attackerSideHero->getManaAvailable(), mana);
}

TEST_F(NewHorizonsElementalConvergenceTest, BattleAISelectsLegalHexAndPaidCastAgreesWithDetachedPreview)
{
	ASSERT_NO_FATAL_FAILURE(expectPaidAI(TerrainId::GRASS, "core:grass_hills", 100));
}

TEST_F(NewHorizonsElementalConvergenceTest, EarthBattleAISelectsPaidCastAtProvisionalSpellPower)
{
	ASSERT_NO_FATAL_FAILURE(expectPaidAI(TerrainId::DIRT, "core:dirt_hills", 41));
}

TEST(NewHorizonsElementalTerrainRulesTest, AuthoredMappingIncludesApprovedRowsAndRejectsUnknownContext)
{
	using newHorizonsElementalTerrain::resolve;
	const auto earth = CreatureID(CreatureID::decode("core:earthElemental"));
	const auto water = CreatureID(CreatureID::decode("core:waterElemental"));
	EXPECT_EQ(resolve(TerrainId::DIRT, "core:dirt_hills"), earth);
	EXPECT_EQ(resolve(TerrainId::SAND, "core:sand_mesas"), earth);
	EXPECT_EQ(resolve(TerrainId::NONE, "", "custom:wasteland"), earth);
	EXPECT_EQ(resolve(TerrainId::SWAMP, "core:swamp_trees"), water);
	EXPECT_EQ(resolve(TerrainId::SAND, "core:sand_shore"), water);
	EXPECT_EQ(resolve(TerrainId::SNOW, "core:snow_trees"), water);
	EXPECT_EQ(resolve(TerrainId::WATER, ""), water);
	EXPECT_EQ(resolve(TerrainId::LAVA, "core:lava"), CreatureID(CreatureID::decode("core:fireElemental")));
	EXPECT_EQ(resolve(TerrainId::GRASS, "core:grass_hills"), CreatureID(CreatureID::decode("core:airElemental")));
	EXPECT_EQ(resolve(TerrainId::ROUGH, "core:subterranean"), earth);
	EXPECT_EQ(resolve(TerrainId::SUBTERRANEAN, "core:subterranean"), earth);
	EXPECT_EQ(resolve(TerrainId::GRASS, "core:magic_plains"), CreatureID(CreatureID::decode("core:magicElemental")));
	EXPECT_FALSE(resolve(TerrainId::NONE, "custom:unmapped", "custom:unmapped"));
}
