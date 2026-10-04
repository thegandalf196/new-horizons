/*
 * NewHorizonsMagicResistanceTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later; see license.txt file
 */
#include "StdInc.h"
#include "HeroCommandFixture.h"

#include "../../../AI/BattleAI/StackWithBonuses.h"
#include "../../../lib/CRandomGenerator.h"
#include "../../../lib/CCreatureHandler.h"
#include "../../../lib/GameConstants.h"
#include "../../../lib/battle/BattleAction.h"
#include "../../../lib/battle/BattleHexArray.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/entities/artifact/CArtifact.h"
#include "../../../lib/entities/artifact/CArtifactInstance.h"
#include "../../../lib/gameState/CGameState.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/mapping/CMap.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/ISpellMechanics.h"
#include "../../../lib/spells/NewHorizonsMagic.h"
#include "../../../lib/spells/Problem.h"
#include "../../../server/CGameHandler.h"
#include "../../../server/battles/BattleProcessor.h"

#include <vcmi/Environment.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace
{
ArtifactID artifactNamed(const char * name)
{
	return ArtifactID(ArtifactID::decode(name));
}

ArtifactPosition firstFreeHeroSlot(const CGHeroInstance & hero, ArtifactID artifact)
{
	const auto * type = artifact.toArtifact();
	if(!type)
		return ArtifactPosition::PRE_FIRST;

	for(const auto slot : type->getPossibleSlots().at(ArtBearer::HERO))
		if(!hero.getArt(slot))
			return slot;

	return ArtifactPosition::PRE_FIRST;
}

class MagicResistancePredictionEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;

public:
	explicit MagicResistancePredictionEnvironment(std::shared_ptr<CGameState> state)
		: state(std::move(state))
	{}

	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class NewHorizonsMagicResistanceTest : public HeroCommandFixture
{
protected:
	bool enableNewHorizonsRules = true;
	CStack * protectedStack = nullptr;
	CStack * auraStack = nullptr;

	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";
	}

	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		if(enableNewHorizonsRules)
			loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS,
				JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
		else
			loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, JsonNode());
	}

	void equipResistanceArtifacts(CGHeroInstance * hero)
	{
		for(const auto * name : {"core:garnitureOfInterference", "core:surcoatOfCounterpoise",
			"core:bootsOfPolarity"})
		{
			const auto artifact = artifactNamed(name);
			ASSERT_NE(artifact.toArtifact(), nullptr) << name;
			const auto slot = firstFreeHeroSlot(*hero, artifact);
			ASSERT_NE(slot, ArtifactPosition::PRE_FIRST) << name;
			giveArtifact(hero, artifact, slot);
			const auto * equipped = hero->getArt(slot);
			ASSERT_NE(equipped, nullptr) << name;
			EXPECT_EQ(equipped->getTypeId(), artifact) << name;
		}
	}

	BattleHex freeAdjacentHex(const CStack * target, BattleSide side) const
	{
		const auto unicorn = creatureByName("core:unicorn");
		const bool isDoubleWide = unicorn.toCreature()->isDoubleWide();
		for(const auto & hex : target->getSurroundingHexes().toVector())
		{
			if(!hex.isAvailable() || battle()->battleGetUnitByPos(hex, false))
				continue;
			if(isDoubleWide)
			{
				const auto tail = battle::Unit::occupiedHex(hex, true, side);
				if(!tail.isAvailable() || battle()->battleGetUnitByPos(tail, false))
					continue;
			}
			return hex;
		}
		return BattleHex::INVALID;
	}

	void prepareBattle(BattleSide protectedSide, SpellID spell)
	{
		startGame();
		auto * caster = attackerSideHero;
		auto * equipmentOwner = protectedSide == BattleSide::ATTACKER
			? attackerSideHero : defenderSideHero;
		equipResistanceArtifacts(equipmentOwner);
		giveArtifact(caster, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		caster->addSpellToSpellbook(spell);
		caster->setPrimarySkill(PrimarySkill::SPELL_POWER, 0, ChangeValueMode::ABSOLUTE);
		caster->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
		setTestSpellPointTotal(caster, 1000);

		startBattle();
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);

		const auto targetHex = protectedSide == BattleSide::ATTACKER ? BattleHex(7, 5) : BattleHex(11, 5);
		const auto enemyHex = protectedSide == BattleSide::ATTACKER ? BattleHex(14, 5) : BattleHex(3, 5);
		protectedStack = addStack(protectedSide, creatureByName("core:battleDwarf"), targetHex, 1000);
		auto * hostile = addStack(protectedSide == BattleSide::ATTACKER
			? BattleSide::DEFENDER : BattleSide::ATTACKER, creatureByName("core:pikeman"), enemyHex, 1000);
		ASSERT_NE(protectedStack, nullptr);
		ASSERT_NE(hostile, nullptr);

		const auto auraHex = freeAdjacentHex(protectedStack, protectedSide);
		ASSERT_TRUE(auraHex.isAvailable()) << "The controlled Battle Dwarf needs an adjacent Unicorn aura";
		auraStack = addStack(protectedSide, creatureByName("core:unicorn"), auraHex, 1);
		ASSERT_NE(auraStack, nullptr);
		const auto adjacent = battle()->battleAdjacentUnits(protectedStack);
		ASSERT_TRUE(std::ranges::any_of(adjacent, [this](const auto * unit)
			{ return unit->unitId() == auraStack->unitId(); }));
		beginCombat();
	}

	bool activateHeroActionSide(BattleSide side)
	{
		const PlayerColor player = side == BattleSide::ATTACKER
			? attackerSideHero->getOwner() : defenderSideHero->getOwner();
		for(int attempt = 0; attempt < 64; ++attempt)
		{
			const auto * active = battle()->battleActiveUnit();
			if(!active)
				return false;
			if(battle()->battleGetOwner(active) == player
				&& battle()->battleGetActionController(active) == player)
				return true;
			const auto activeOwner = battle()->battleGetOwner(active);
			if(!gameHandler->battles->makePlayerBattleAction(BattleID(0), activeOwner,
				BattleAction::makeDefend(active)))
				return false;
		}
		return false;
	}

	bool castHeroSpell(SpellID spell, CStack * target)
	{
		if(!activateHeroActionSide(BattleSide::ATTACKER))
			return false;
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = BattleSide::ATTACKER;
		action.spell = spell;
		action.aimToUnit(target);
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), attackerSideHero->getOwner(), action);
	}

	std::optional<int> seedForResistanceDraw(const CStack * target, int expectedDraw) const
	{
		const auto units = battle()->battleGetAllUnits(false);
		const auto targetIt = std::ranges::find_if(units, [target](const auto * unit)
			{ return unit->unitId() == target->unitId(); });
		if(targetIt == units.end())
			return std::nullopt;

		for(int candidateSeed = 1; candidateSeed < 100000; ++candidateSeed)
		{
			CRandomGenerator candidate(candidateSeed);
			int targetDraw = -1;
			for(const auto * unit : units)
			{
				const int draw = candidate.nextInt(0, 99);
				if(unit->unitId() == target->unitId())
					targetDraw = draw;
			}
			if(targetDraw == expectedDraw)
				return candidateSeed;
		}
		return std::nullopt;
	}
};
}

TEST_F(NewHorizonsMagicResistanceTest, CappedHostileCastUsesEquippedResistanceAuraProjectionAndActualBoundaryRolls)
{
	prepareBattle(BattleSide::DEFENDER, SpellID::MAGIC_ARROW);
	ASSERT_NE(protectedStack, nullptr);
	ASSERT_NE(auraStack, nullptr);
	ASSERT_EQ(protectedStack->valOfBonuses(BonusType::MAGIC_RESISTANCE), 70)
		<< "Battle Dwarf40 and the three equipped artifacts5+10+15 form the raw70 base";
	EXPECT_EQ(auraStack->valOfBonuses(BonusType::SPELL_RESISTANCE_AURA), 20);
	EXPECT_EQ(protectedStack->magicResistance(), 75)
		<< "The legacy 70-plus-20 composition is 76 before the New Horizons75 cap";
	EXPECT_EQ(battle()->battleGetOwner(protectedStack), defenderSideHero->getOwner());

	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), attackerSideHero->getOwner());
	MagicResistancePredictionEnvironment environment(gameState());
	HypotheticBattle projected(&environment, callback);
	auto projectedTarget = projected.getForUpdate(protectedStack->unitId());
	ASSERT_NE(projectedTarget, nullptr);
	EXPECT_EQ(projectedTarget->magicResistance(), 75);
	const auto liveAuraPosition = auraStack->getPosition();
	projected.moveUnit(auraStack->unitId(), BattleHex(2, 2));
	EXPECT_EQ(projectedTarget->magicResistance(), 70)
		<< "A detached friendly-unit move removes the sole adjacent aura";
	EXPECT_EQ(protectedStack->magicResistance(), 75);
	EXPECT_EQ(auraStack->getPosition(), liveAuraPosition)
		<< "Projection must not mutate the live battle";

	const Bonus projectedResistance(BonusDuration::PERMANENT, BonusType::MAGIC_RESISTANCE,
		BonusSource::OTHER, 5, BonusSourceID());
	projected.addUnitBonus(protectedStack->unitId(), {projectedResistance});
	EXPECT_EQ(projectedTarget->magicResistance(), 75);
	projected.removeUnitBonus(protectedStack->unitId(), {projectedResistance});
	EXPECT_EQ(projectedTarget->magicResistance(), 70);
	projected.moveUnit(auraStack->unitId(), liveAuraPosition);
	EXPECT_EQ(projectedTarget->magicResistance(), 75);
	projected.removeUnit(auraStack->unitId());
	EXPECT_EQ(projectedTarget->magicResistance(), 70);
	EXPECT_EQ(protectedStack->magicResistance(), 75);
	EXPECT_EQ(auraStack->getPosition(), liveAuraPosition);

	const Bonus additionalResistance(BonusDuration::PERMANENT, BonusType::MAGIC_RESISTANCE,
		BonusSource::OTHER, 30, BonusSourceID());
	protectedStack->addNewBonus(std::make_shared<Bonus>(additionalResistance));
	ASSERT_EQ(protectedStack->valOfBonuses(BonusType::MAGIC_RESISTANCE), 100);
	EXPECT_EQ(protectedStack->magicResistance(), 75);
	EXPECT_EQ(callback->battleGetMagicResistance(protectedStack), 75);
	EXPECT_NE(battle()->battleGetOwner(protectedStack), attackerSideHero->getOwner());

	const auto * spell = SpellID(SpellID::MAGIC_ARROW).toSpell();
	ASSERT_NE(spell, nullptr);
	HypotheticBattle castProjection(&environment, callback);
	spells::BattleCast forecast(&castProjection, attackerSideHero, spells::Mode::HERO, spell);
	const auto forecastMechanics = spell->battleMechanics(&forecast);
	auto forecastTarget = castProjection.getForUpdate(protectedStack->unitId());
	ASSERT_NE(forecastTarget, nullptr);
	spells::detail::ProblemImpl forecastProblem;
	const spells::Target forecastAim{spells::Destination(forecastTarget.get())};
	EXPECT_TRUE(forecastMechanics->isReceptive(forecastTarget.get()));
	EXPECT_TRUE(forecastMechanics->canBeCastAt(forecastAim, forecastProblem));
	EXPECT_EQ(forecastTarget->magicResistance(), 75);

	spells::BattleCast liveCast(battle(), attackerSideHero, spells::Mode::HERO, spell);
	const auto liveMechanics = spell->battleMechanics(&liveCast);
	spells::detail::ProblemImpl availabilityProblem;
	const bool castable = liveMechanics->canBeCast(availabilityProblem);
	std::vector<std::string> availabilityProblems;
	availabilityProblem.getAll(availabilityProblems);
	ASSERT_TRUE(castable) << testing::PrintToString(availabilityProblems);
	spells::detail::ProblemImpl targetProblem;
	const spells::Target liveAim{spells::Destination(protectedStack)};
	ASSERT_TRUE(liveMechanics->isReceptive(protectedStack));
	const bool targetable = liveMechanics->canBeCastAt(liveAim, targetProblem);
	std::vector<std::string> targetProblems;
	targetProblem.getAll(targetProblems);
	ASSERT_TRUE(targetable) << testing::PrintToString(targetProblems);
	ASSERT_EQ(battle()->battleCanCastSpell(attackerSideHero, spells::Mode::HERO), ESpellCastProblem::OK);
	ASSERT_TRUE(activateHeroActionSide(BattleSide::ATTACKER));

	const auto resistSeed = seedForResistanceDraw(protectedStack, 74);
	ASSERT_TRUE(resistSeed.has_value()) << "Find a deterministic draw immediately below the capped75% threshold";
	gameHandler->randomizer->setSeed(*resistSeed);
	const auto healthBeforeResist = protectedStack->getAvailableHealth();
	ASSERT_TRUE(castHeroSpell(SpellID::MAGIC_ARROW, protectedStack))
		<< "A hostile raw100% target is still a legal, accepted Hero spell target under the cap";
	EXPECT_EQ(protectedStack->getAvailableHealth(), healthBeforeResist)
		<< "A 74 roll resists at the capped75% chance";

	endRound();
	ASSERT_TRUE(activateHeroActionSide(BattleSide::ATTACKER));
	const auto hitSeed = seedForResistanceDraw(protectedStack, 75);
	ASSERT_TRUE(hitSeed.has_value()) << "Find the exact non-resisting boundary draw75";
	gameHandler->randomizer->setSeed(*hitSeed);
	const auto healthBeforeHit = protectedStack->getAvailableHealth();
	ASSERT_TRUE(castHeroSpell(SpellID::MAGIC_ARROW, protectedStack));
	EXPECT_LT(protectedStack->getAvailableHealth(), healthBeforeHit)
		<< "A 75 roll lands, proving the total chance is capped at75 rather than100";

	protectedStack->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::SPELL_IMMUNITY, BonusSource::OTHER, 1, BonusSourceID(),
		BonusSubtypeID(SpellID(SpellID::MAGIC_ARROW))));
	spells::BattleCast immuneCast(battle(), attackerSideHero, spells::Mode::HERO, spell);
	const auto immuneMechanics = spell->battleMechanics(&immuneCast);
	EXPECT_EQ(protectedStack->magicResistance(), 75)
		<< "Spell-specific immunity remains independent from the probabilistic resistance cap";
	EXPECT_FALSE(immuneMechanics->isReceptive(protectedStack));
	spells::detail::ProblemImpl immunityProblem;
	EXPECT_FALSE(immuneMechanics->canBeCastAt(liveAim, immunityProblem));
}

TEST_F(NewHorizonsMagicResistanceTest, PositiveSpellRemainsReceptiveToAnArtifactAndAuraProtectedAlly)
{
	prepareBattle(BattleSide::ATTACKER, SpellID::HASTE);
	ASSERT_NE(protectedStack, nullptr);
	ASSERT_EQ(protectedStack->valOfBonuses(BonusType::MAGIC_RESISTANCE), 70);
	ASSERT_EQ(protectedStack->magicResistance(), 75);
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), attackerSideHero->getOwner());
	EXPECT_EQ(callback->battleGetMagicResistance(protectedStack), 75);
	const auto * spell = SpellID(SpellID::HASTE).toSpell();
	ASSERT_NE(spell, nullptr);

	spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, spell);
	const auto mechanics = spell->battleMechanics(&cast);
	EXPECT_TRUE(mechanics->isReceptive(protectedStack));
	spells::detail::ProblemImpl problem;
	ASSERT_TRUE(mechanics->canBeCast(problem));
	ASSERT_TRUE(mechanics->canBeCastAt(spells::Target{spells::Destination(protectedStack)}, problem));
	ASSERT_TRUE(castHeroSpell(SpellID::HASTE, protectedStack));
	EXPECT_TRUE(protectedStack->hasBonusFrom(BonusSource::SPELL_EFFECT,
		BonusSourceID(SpellID(SpellID::HASTE))));
}

TEST_F(NewHorizonsMagicResistanceTest, LegacyRulesKeepTheUncappedAuraCompositionAndRawHundredBlocksHostileTargeting)
{
	enableNewHorizonsRules = false;
	prepareBattle(BattleSide::DEFENDER, SpellID::MAGIC_ARROW);
	ASSERT_NE(protectedStack, nullptr);
	ASSERT_EQ(protectedStack->valOfBonuses(BonusType::MAGIC_RESISTANCE), 70);
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), attackerSideHero->getOwner());
	EXPECT_EQ(protectedStack->magicResistance(), 76)
		<< "Legacy resistance retains its pre-existing aura composition without the New Horizons cap";
	EXPECT_EQ(callback->battleGetMagicResistance(protectedStack), 76);

	const Bonus additionalResistance(BonusDuration::PERMANENT, BonusType::MAGIC_RESISTANCE,
		BonusSource::OTHER, 30, BonusSourceID());
	protectedStack->addNewBonus(std::make_shared<Bonus>(additionalResistance));
	ASSERT_EQ(protectedStack->valOfBonuses(BonusType::MAGIC_RESISTANCE), 100);
	EXPECT_EQ(protectedStack->magicResistance(), 100);
	EXPECT_EQ(callback->battleGetMagicResistance(protectedStack), 100);

	const auto * spell = SpellID(SpellID::MAGIC_ARROW).toSpell();
	ASSERT_NE(spell, nullptr);
	spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, spell);
	const auto mechanics = spell->battleMechanics(&cast);
	EXPECT_FALSE(mechanics->isReceptive(protectedStack));
	spells::detail::ProblemImpl problem;
	EXPECT_FALSE(mechanics->canBeCastAt(spells::Target{spells::Destination(protectedStack)}, problem));
	ASSERT_TRUE(activateHeroActionSide(BattleSide::ATTACKER));
	const auto manaBefore = attackerSideHero->getManaAvailable();
	EXPECT_FALSE(castHeroSpell(SpellID::MAGIC_ARROW, protectedStack));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore)
		<< "Legacy 100% Magic Resistance still prevents the authoritative cast before spending mana";
}
