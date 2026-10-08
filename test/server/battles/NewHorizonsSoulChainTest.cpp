/*
 * NewHorizonsSoulChainTest.cpp, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"

#include "HeroCommandFixture.h"

#include "../../../lib/battle/NewHorizonsSoulChain.h"
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/bonuses/BonusParameters.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../lib/spells/BattleSpellMechanics.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/NewHorizonsMagic.h"
#include "../../../lib/spells/MagicalDamageReduction.h"
#include "../../../lib/spells/Problem.h"

namespace
{
constexpr std::string_view SOUL_CHAIN_KEY = "new-horizons:soulChain";
constexpr std::string_view SOUL_CHAIN_STATUS = "core:soulChainStatus";
constexpr std::string_view SHADOW_MAGIC_SKILL = "new-horizons:shadowMagic";
constexpr std::string_view SOUL_BINDER_PERK = "new-horizons:shadowMagic.soulBinder";

SpellID soulChainSpell()
{
	return SpellID(SpellID::decode(std::string(SOUL_CHAIN_KEY)));
}

class NewHorizonsSoulChainTest : public HeroCommandFixture
{
protected:
	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";
	}

	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
	}

	void configureCaster(CGHeroInstance * hero, bool soulBinder = false)
	{
		ASSERT_NE(hero, nullptr);
		const auto spell = soulChainSpell();
		ASSERT_NE(spell, SpellID::NONE);
		giveArtifact(hero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		hero->addSpellToSpellbook(spell);
		hero->addSpellToSpellbook(SpellID::DISPEL);
		hero->setPrimarySkill(PrimarySkill::SPELL_POWER, 100, ChangeValueMode::ABSOLUTE);
		hero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
		const auto skill = SecondarySkill(SecondarySkill::decode(std::string(SHADOW_MAGIC_SKILL)));
		ASSERT_TRUE(skill.hasValue());
		hero->setSecSkillLevel(skill, soulBinder ? MasteryLevel::ADVANCED : MasteryLevel::BASIC,
			ChangeValueMode::ABSOLUTE);
		if(soulBinder)
		{
			hero->applyPerkSelection({std::string(SHADOW_MAGIC_SKILL),
				"new-horizons:shadowMagic.malediction"});
			hero->applyPerkSelection({std::string(SHADOW_MAGIC_SKILL), std::string(SOUL_BINDER_PERK)});
			ASSERT_TRUE(hero->hasActivePerk(std::string(SHADOW_MAGIC_SKILL), std::string(SOUL_BINDER_PERK)));
		}
		setTestSpellPointTotal(hero, 1000);
	}

	void removeDeployedUnits()
	{
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
	}

	void prepare(bool binder = false)
	{
		startGame();
		configureCaster(attackerSideHero, binder);
		configureCaster(defenderSideHero);
		startBattle();
		removeDeployedUnits();

		friendly = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(4, 5), 1000);
		primary = addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), BattleHex(14, 4), 1000);
		secondary = addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), BattleHex(12, 4), 1000);
		otherSecondary = addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), BattleHex(13, 6), 1000);
		fourthEnemy = addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), BattleHex(15, 7), 1000);
		for(const auto & hex : secondary->getSurroundingHexes().toVector())
			if(hex.isValid() && !battle()->battleGetUnitByPos(hex, false))
			{
				attacker = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), hex, 100);
				break;
			}
		ASSERT_NE(attacker, nullptr);
		ASSERT_NE(friendly, nullptr);
		ASSERT_NE(primary, nullptr);
		ASSERT_NE(secondary, nullptr);
		ASSERT_NE(otherSecondary, nullptr);
		ASSERT_NE(fourthEnemy, nullptr);
		beginCombat();
	}

	BattleAction action(BattleSide side, const std::vector<const CStack *> & targets) const
	{
		BattleAction result;
		result.actionType = EActionType::HERO_SPELL;
		result.side = side;
		result.spell = soulChainSpell();
		for(const auto * target : targets)
			result.aimToUnit(target);
		return result;
	}

	bool cast(BattleSide side, const std::vector<const CStack *> & targets) const
	{
		return gameHandler->battles->makePlayerBattleAction(BattleID(0),
			battle()->sideToPlayer(side), action(side, targets));
	}

	bool castOn(const CGHeroInstance * hero, SpellID spell, const CStack * target) const
	{
		return HeroCommandFixture::castOn(hero, spell, target);
	}

	const Bonus * status(const CStack * unit) const
	{
		if(!unit)
			return nullptr;
		const auto spell = soulChainSpell();
		const auto statusId = ScriptID(ScriptID::decode(std::string(SOUL_CHAIN_STATUS)));
		const auto bonuses = unit->getBonuses(Selector::source(BonusSource::SPELL_EFFECT,
			BonusSourceID(spell)).And(Selector::typeSubtype(BonusType::COMBAT_EVENT_TRIGGER,
			BonusSubtypeID(statusId))));
		return bonuses->empty() ? nullptr : bonuses->front().get();
	}

	bool activateStack(const CStack * stack)
	{
		const auto maximumActions = battle()->stacks.size() * 12 + 12;
		for(size_t index = 0; index < maximumActions; ++index)
		{
			const auto * active = battle()->battleActiveUnit();
			if(!active)
				return false;
			if(active->unitId() == stack->unitId())
				return true;
			if(!gameHandler->battles->makePlayerBattleAction(BattleID(0),
				battle()->sideToPlayer(active->unitSide()), BattleAction::makeDefend(active)))
				return false;
		}
		return false;
	}

	void injure(const CStack * target, int64_t amount, const CStack * source)
	{
		BattleStackAttacked hit;
		hit.attackerID = source->unitId();
		hit.stackAttacked = target->unitId();
		hit.damageAmount = amount;
		CStack::prepareAttacked(hit, gameHandler->getRandomGenerator(), target->acquireState());
		StacksInjured injury;
		injury.battleID = BattleID(0);
		injury.stacks.push_back(hit);
		gameHandler->sendAndApply(injury);
	}

	void addReciprocalMarker(CStack * unit, const CStack * linkedPrimary)
	{
		Bonus marker(BonusDuration::N_TURNS, BonusType::COMBAT_EVENT_TRIGGER,
			BonusSource::SPELL_EFFECT, 3000, BonusSourceID(soulChainSpell()),
			BonusSubtypeID(ScriptID(ScriptID::decode(std::string(SOUL_CHAIN_STATUS)))));
		marker.turnsRemain = 2;
		JsonNode parameters;
		parameters["primaryUnitId"].Integer() = linkedPrimary->unitId();
		parameters["casterSide"].Integer() = static_cast<int32_t>(BattleSide::ATTACKER);
		marker.parameters = std::make_shared<BonusParameters>(parameters);
		unit->addNewBonus(std::make_shared<Bonus>(marker));
	}

	CStack * attacker = nullptr;
	CStack * friendly = nullptr;
	CStack * primary = nullptr;
	CStack * secondary = nullptr;
	CStack * otherSecondary = nullptr;
	CStack * fourthEnemy = nullptr;
};
}

TEST_F(NewHorizonsSoulChainTest, CombatCastingEchoUsesSerializedCaptureAfterReadinessConsumption)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto warcasting = SecondarySkill(SecondarySkill::decode("new-horizons:warcasting"));
	ASSERT_TRUE(warcasting.hasValue());
	attackerSideHero->setSecSkillLevel(warcasting, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
	attackerSideHero->applyPerkSelection({"new-horizons:warcasting", "new-horizons:warcasting.martialChanneling"});
	attackerSideHero->applyPerkSelection({"new-horizons:warcasting", "new-horizons:warcasting.combatCasting"});
	primary->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::SPELL_DAMAGE_REDUCTION, BonusSource::CREATURE_ABILITY, 50,
		BonusSourceID(), BonusSubtypeID(SpellSchool::ANY)));
	battle()->getSide(BattleSide::ATTACKER).warcastingState.recordAcceptedAction(
		AlternatingHeroActionState::Action::ORDER, battle()->getRound(), 20);
	ASSERT_TRUE(cast(BattleSide::ATTACKER, {primary, secondary}));
	const auto link = newHorizonsSoulChain::linkFor(secondary);
	ASSERT_TRUE(link.has_value());
	EXPECT_EQ(spells::capturedMdrPenetrations(link->mdrPenetration, primary->unitId()), (std::vector<int>{15}));
	EXPECT_EQ(battle()->getSide(BattleSide::ATTACKER).warcastingState.bonusFor(
		AlternatingHeroActionState::Action::SPELL, battle()->getRound()), 0);
	const auto raw = newHorizonsSoulChain::echoDamage(500, link->echoBasisPoints);
	const auto expected = spells::calculateMagicalDamageReduction(raw, {50}, std::vector<int>{15}).damageWithPenetration;
	const auto before = primary->getAvailableHealth();
	injure(secondary, 500, attacker);
	EXPECT_EQ(before - primary->getAvailableHealth(), expected);
	CMemorySerializer wire;
	wire.oser.version = ESerializationVersion::CURRENT;
	wire.iser.version = ESerializationVersion::CURRENT;
	Bonus saved(*status(secondary));
	wire.oser & saved;
	Bonus restored;
	wire.iser & restored;
	ASSERT_NE(restored.parameters, nullptr);
	EXPECT_EQ(restored.parameters->toCustom<JsonNode>()["mdrPenetration"], link->mdrPenetration);
}

TEST_F(NewHorizonsSoulChainTest, CanonicalRegistrationAndEchoFormulaUseSavedSchoolCoefficient)
{
	const JsonNode rules(JsonPath::builtin("config/newHorizonsMagic"));
	ASSERT_NO_THROW(newHorizonsMagic::validateRules(rules));
	const auto spell = soulChainSpell();
	ASSERT_NE(spell, SpellID::NONE);
	EXPECT_TRUE(newHorizonsSoulChain::isEnabled(rules));
	EXPECT_EQ(newHorizonsMagic::spellLevel(rules, spell), 3);
	for(int rank = 0; rank < 4; ++rank)
		EXPECT_EQ(newHorizonsMagic::spellCost(rules, spell, rank), 12);
	EXPECT_EQ(newHorizonsSoulChain::echoPercentBasisPoints(100, 10000, false), 3000);
	EXPECT_EQ(newHorizonsSoulChain::echoPercentBasisPoints(100, 11500, false), 3150);
	EXPECT_EQ(newHorizonsSoulChain::echoPercentBasisPoints(1000, 10000, false), 4000);
	EXPECT_EQ(newHorizonsSoulChain::echoPercentBasisPoints(1000, 10000, true), 5500);
	EXPECT_EQ(newHorizonsSoulChain::echoDamage(501, 3000), 150);
	EXPECT_EQ(newHorizonsSoulChain::echoDamage(501, 5500), 275);
}

TEST_F(NewHorizonsSoulChainTest, ExplicitTargetVectorLinksOnlyItsSecondaries)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true));
	const auto manaBefore = attackerSideHero->getManaAvailable();
	const auto invalidActionsBefore = server.startedActions.size();

	EXPECT_FALSE(cast(BattleSide::ATTACKER, {primary, primary}));
	EXPECT_FALSE(cast(BattleSide::ATTACKER, {primary, friendly}));
	EXPECT_FALSE(cast(BattleSide::ATTACKER, {primary, secondary, otherSecondary, fourthEnemy}));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore);
	EXPECT_EQ(server.startedActions.size(), invalidActionsBefore);
	EXPECT_TRUE(server.castsOf(soulChainSpell()).empty());

	spells::BattleCast castState(battle(), attackerSideHero, spells::Mode::HERO, soulChainSpell().toSpell());
	const auto mechanics = soulChainSpell().toSpell()->battleMechanics(&castState);
	const int coefficient = mechanics->getSpellPowerCoefficientBasisPoints();
	const int32_t expectedEcho = newHorizonsSoulChain::echoPercentBasisPoints(100, coefficient, true);
	ASSERT_TRUE(cast(BattleSide::ATTACKER, {primary, secondary, otherSecondary}));

	EXPECT_EQ(status(primary), nullptr);
	ASSERT_NE(status(secondary), nullptr);
	ASSERT_NE(status(otherSecondary), nullptr);
	EXPECT_EQ(status(secondary)->val, expectedEcho);
	EXPECT_EQ(status(otherSecondary)->val, expectedEcho);
	EXPECT_EQ(status(secondary)->turnsRemain, 2);
	const auto link = newHorizonsSoulChain::linkFor(secondary);
	ASSERT_TRUE(link.has_value());
	EXPECT_EQ(link->primaryUnitId, primary->unitId());
	EXPECT_EQ(link->casterSide, BattleSide::ATTACKER);

	CMemorySerializer wire;
	wire.oser.version = ESerializationVersion::CURRENT;
	wire.iser.version = ESerializationVersion::CURRENT;
	Bonus saved(*status(secondary));
	wire.oser & saved;
	Bonus restored;
	wire.iser & restored;
	ASSERT_NE(restored.parameters, nullptr);
	EXPECT_EQ(restored.val, status(secondary)->val);
	EXPECT_EQ(restored.turnsRemain, status(secondary)->turnsRemain);
	EXPECT_EQ(restored.parameters->toCustom<JsonNode>(), status(secondary)->parameters->toCustom<JsonNode>());

	ASSERT_TRUE(castOn(defenderSideHero, SpellID::DISPEL, secondary));
	EXPECT_EQ(status(secondary), nullptr);
}

TEST_F(NewHorizonsSoulChainTest, SingleTargetCastIsLegalButCreatesNoLink)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(cast(BattleSide::ATTACKER, {primary}));
	EXPECT_EQ(status(primary), nullptr);
	EXPECT_EQ(status(secondary), nullptr);
	ASSERT_EQ(server.castsOf(soulChainSpell()).size(), 1u);
}

TEST_F(NewHorizonsSoulChainTest, IndirectDamageEchoUsesActualDamageAndTaggedEchoCannotRecurse)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(cast(BattleSide::ATTACKER, {primary, secondary}));
	ASSERT_NE(status(secondary), nullptr);
	// Model a second active link on the primary. The first echo must be tagged and
	// must not itself trigger this otherwise valid reverse relationship.
	addReciprocalMarker(primary, secondary);
	const auto primaryBefore = primary->getAvailableHealth();
	const auto secondaryBefore = secondary->getAvailableHealth();
	const auto firstInjury = server.injuries.size();
	const int64_t expectedEcho = newHorizonsSoulChain::echoDamage(500, status(secondary)->val);

	injure(secondary, 500, attacker);

	EXPECT_EQ(secondaryBefore - secondary->getAvailableHealth(), 500);
	EXPECT_EQ(primaryBefore - primary->getAvailableHealth(), expectedEcho);
	ASSERT_EQ(server.injuries.size(), firstInjury + 2);
	ASSERT_EQ(server.injuries[firstInjury].stacks.size(), 1u);
	ASSERT_EQ(server.injuries[firstInjury + 1].stacks.size(), 1u);
	const auto & echo = server.injuries[firstInjury + 1].stacks.front();
	EXPECT_EQ(echo.stackAttacked, primary->unitId());
	EXPECT_TRUE(newHorizonsSoulChain::isEchoHit(echo));
	const auto expectedLog = "Soul Chain echoes " + std::to_string(expectedEcho) + " Shadow damage onto";
	EXPECT_TRUE(std::ranges::any_of(server.battleLogLines, [&](const std::string & line)
	{
		return line.find(expectedLog) != std::string::npos;
	})) << ::testing::PrintToString(server.battleLogLines);
}

TEST_F(NewHorizonsSoulChainTest, BattleAttackPacketsAlsoTriggerTheEcho)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(cast(BattleSide::ATTACKER, {primary, secondary}));
	const auto primaryBefore = primary->getAvailableHealth();
	const auto firstInjury = server.injuries.size();

	BattleAttack attack;
	attack.battleID = BattleID(0);
	attack.stackAttacking = attacker->unitId();
	attack.attackerChanges.battleID = BattleID(0);
	UnitChanges attackerUpdate(attacker->unitId(), UnitChanges::EOperation::UPDATE);
	attackerUpdate.data = attacker->acquireState()->save();
	attack.attackerChanges.changedStacks.push_back(std::move(attackerUpdate));
	BattleStackAttacked hit;
	hit.attackerID = attacker->unitId();
	hit.stackAttacked = secondary->unitId();
	hit.damageAmount = 500;
	CStack::prepareAttacked(hit, gameHandler->getRandomGenerator(), secondary->acquireState());
	attack.bsa.push_back(hit);
	gameHandler->sendAndApply(attack);

	ASSERT_EQ(server.attacks.size(), 1u);
	const auto attackHit = std::ranges::find(server.attacks.back().bsa, secondary->unitId(),
		&BattleStackAttacked::stackAttacked);
	ASSERT_NE(attackHit, server.attacks.back().bsa.end());
	const int64_t expectedEcho = newHorizonsSoulChain::echoDamage(attackHit->damageAmount, status(secondary)->val);
	EXPECT_GT(expectedEcho, 0);
	EXPECT_EQ(primaryBefore - primary->getAvailableHealth(), expectedEcho);
	ASSERT_EQ(server.injuries.size(), firstInjury + 1);
	ASSERT_EQ(server.injuries.back().stacks.size(), 1u);
	EXPECT_TRUE(newHorizonsSoulChain::isEchoHit(server.injuries.back().stacks.front()));
}
