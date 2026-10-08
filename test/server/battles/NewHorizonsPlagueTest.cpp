/*
 * NewHorizonsPlagueTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"

#include "HeroCommandFixture.h"

#include "../../../lib/GameSettings.h"
#include "../../../lib/battle/NewHorizonsPlague.h"
#include "../../../lib/spells/MagicalDamageReduction.h"
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/bonuses/BonusParameters.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../lib/spells/NewHorizonsMagic.h"

namespace
{
constexpr std::string_view PLAGUE_KEY = "new-horizons:plague";

SpellID plagueSpell()
{
	return SpellID(SpellID::decode(std::string(PLAGUE_KEY)));
}

JsonNode certainMorale()
{
	JsonNode result;
	for(int index = 0; index < 10; ++index)
		result.Vector().emplace_back(100);
	return result;
}

class NewHorizonsPlagueTest : public HeroCommandFixture
{
protected:
	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";
	}

	void mapLoaded(CMap * map) override
	{
		HeroCommandFixture::mapLoaded(map);
		map->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
		map->overrideGameSetting(EGameSettings::COMBAT_GOOD_MORALE_CHANCE, certainMorale());
		map->overrideGameSetting(EGameSettings::COMBAT_MORALE_DICE_SIZE, JsonNode(100));
	}

	void configureCaster(CGHeroInstance * hero)
	{
		ASSERT_NE(hero, nullptr);
		const auto spell = plagueSpell();
		ASSERT_NE(spell, SpellID::NONE);
		const auto shadowMagic = SecondarySkill::decode("new-horizons:shadowMagic");
		ASSERT_GE(shadowMagic, 0);
		hero->setSecSkillLevel(SecondarySkill(shadowMagic), MasteryLevel::BASIC,
			ChangeValueMode::ABSOLUTE);
		hero->setPrimarySkill(PrimarySkill::SPELL_POWER, 100, ChangeValueMode::ABSOLUTE);
		hero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
		giveArtifact(hero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		hero->addSpellToSpellbook(spell);
		hero->addSpellToSpellbook(SpellID::DISPEL);
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

	void prepare(bool adjacentStacks = false, bool targetHasMorale = false)
	{
		startGame();
		configureCaster(attackerSideHero);
		configureCaster(defenderSideHero);
		startBattle();
		removeDeployedUnits();

		if(adjacentStacks)
		{
			afflicted = addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), BattleHex(8, 5), 1000);
			ASSERT_NE(afflicted, nullptr);
		auto surrounding = afflicted->getSurroundingHexes().toVector();
			std::sort(surrounding.begin(), surrounding.end(), [](const BattleHex & lhs, const BattleHex & rhs)
			{
				return lhs.toInt() < rhs.toInt();
			});
			for(const auto & hex : surrounding)
			{
				if(!hex.isValid() || battle()->battleGetUnitByPos(hex, false))
					continue;
				if(!friendlyVictim)
					friendlyVictim = addStack(BattleSide::ATTACKER, creatureByName("core:peasant"), hex, 1000);
				else
				{
					enemyVictim = addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), hex, 1000);
					break;
				}
			}
			ASSERT_NE(friendlyVictim, nullptr);
			ASSERT_NE(enemyVictim, nullptr);
		}
		else
		{
			attacker = addStack(BattleSide::ATTACKER, creatureByName("core:peasant"), BattleHex(3, 5), 1000);
			afflicted = addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), BattleHex(12, 5), 1000);
			ASSERT_NE(attacker, nullptr);
			ASSERT_NE(afflicted, nullptr);
		}
		if(targetHasMorale)
			afflicted->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
				BonusType::MORALE, BonusSource::OTHER, 1, BonusSourceID()));
		beginCombat();
	}

	std::shared_ptr<const Bonus> plagueStatus(const CStack * stack) const
	{
		if(!stack)
			return {};
		const auto bonuses = stack->getBonuses(Selector::source(BonusSource::SPELL_EFFECT,
			BonusSourceID(plagueSpell())).And(Selector::type()(BonusType::COMBAT_EVENT_TRIGGER)));
		return bonuses->empty() ? std::shared_ptr<const Bonus>{} : bonuses->front();
	}

	bool issue(const battle::Unit * stack, const BattleAction & action)
	{
		return stack && gameHandler->battles->makePlayerBattleAction(BattleID(0),
			battle()->sideToPlayer(stack->unitSide()), action);
	}

	bool activateTarget(CStack * stack)
	{
		const auto maximumActions = battle()->stacks.size() * 8 + 8;
		for(size_t actionIndex = 0; actionIndex < maximumActions; ++actionIndex)
		{
			const auto * active = battle()->battleActiveUnit();
			if(!active)
				return false;
			if(active->unitId() == stack->unitId())
				return true;
			if(!issue(active, BattleAction::makeDefend(active)))
				return false;
		}
		return false;
	}

	bool finishTargetTurn(CStack * stack, bool waitFirst = false, int32_t * completedRound = nullptr)
	{
		const auto before = plagueStatus(stack);
		if(!before)
			return false;
		const auto roundsBefore = before->turnsRemain;
		const auto maximumActions = battle()->stacks.size() * 12 + 12;
		bool hasWaited = false;
		for(size_t actionIndex = 0; actionIndex < maximumActions; ++actionIndex)
		{
			const auto * active = battle()->battleActiveUnit();
			if(!active)
				return false;
			if(active->unitId() == stack->unitId() && waitFirst && !hasWaited)
			{
				hasWaited = true;
				if(!issue(active, BattleAction::makeWait(active)))
					return false;
				const auto afterWait = plagueStatus(stack);
				if(!afterWait || afterWait->turnsRemain != roundsBefore)
					return false;
				continue;
			}
			const auto actionRound = battle()->battleGetRound();
			if(!issue(active, BattleAction::makeDefend(active)))
				return false;
			const auto afterAction = plagueStatus(stack);
			if(!afterAction || afterAction->turnsRemain < roundsBefore)
			{
				if(completedRound)
					*completedRound = actionRound;
				return true;
			}
		}
		return false;
	}

	int64_t damageTo(uint32_t unitId, size_t firstInjury = 0) const
	{
		int64_t total = 0;
		for(size_t injuryIndex = firstInjury; injuryIndex < server.injuries.size(); ++injuryIndex)
			for(const auto & hit : server.injuries[injuryIndex].stacks)
				if(hit.stackAttacked == unitId)
					total += hit.damageAmount;
		return total;
	}

	int64_t expectedLegalCastDamage() const
	{
		const auto coefficient = newHorizonsMagic::spellPowerCoefficientBasisPoints(
			battle()->getMagicRules(), attackerSideHero, plagueSpell());
		return newHorizonsPlague::rawTickDamage(100, coefficient);
	}

	CStack * attacker = nullptr;
	CStack * afflicted = nullptr;
	CStack * friendlyVictim = nullptr;
	CStack * enemyVictim = nullptr;
};
}

TEST_F(NewHorizonsPlagueTest, CombatCastingIsSavedOnMarkerAndRetainedAfterReadinessConsumption)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto warcasting = SecondarySkill(SecondarySkill::decode("new-horizons:warcasting"));
	ASSERT_TRUE(warcasting.hasValue());
	attackerSideHero->setSecSkillLevel(warcasting, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
	attackerSideHero->applyPerkSelection({"new-horizons:warcasting", "new-horizons:warcasting.martialChanneling"});
	attackerSideHero->applyPerkSelection({"new-horizons:warcasting", "new-horizons:warcasting.combatCasting"});
	afflicted->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::SPELL_DAMAGE_REDUCTION, BonusSource::CREATURE_ABILITY, 50,
		BonusSourceID(), BonusSubtypeID(SpellSchool::ANY)));
	battle()->getSide(BattleSide::ATTACKER).warcastingState.recordAcceptedAction(
		AlternatingHeroActionState::Action::ORDER, battle()->getRound(), 20);
	ASSERT_TRUE(castOn(attackerSideHero, plagueSpell(), afflicted));
	const auto marker = plagueStatus(afflicted);
	ASSERT_NE(marker, nullptr);
	ASSERT_NE(marker->parameters, nullptr);
	const auto parameters = marker->parameters->toCustom<JsonNode>();
	const auto & captured = parameters["mdrPenetration"];
	EXPECT_EQ(spells::capturedMdrPenetrations(captured, afflicted->unitId()), (std::vector<int>{15}));
	EXPECT_EQ(battle()->getSide(BattleSide::ATTACKER).warcastingState.bonusFor(
		AlternatingHeroActionState::Action::SPELL, battle()->getRound()), 0);
	const auto expected = spells::calculateMagicalDamageReduction(marker->val, {50}, std::vector<int>{15}).damageWithPenetration;
	EXPECT_EQ(newHorizonsPlague::adjustedTickDamage(*battle(), BattleSide::ATTACKER,
		afflicted, marker->val, &captured), expected);
	const auto firstInjury = server.injuries.size();
	ASSERT_TRUE(finishTargetTurn(afflicted));
	EXPECT_EQ(damageTo(afflicted->unitId(), firstInjury), expected);
	CMemorySerializer wire;
	wire.oser.version = ESerializationVersion::CURRENT;
	wire.iser.version = ESerializationVersion::CURRENT;
	Bonus saved(*marker);
	wire.oser & saved;
	Bonus restored;
	wire.iser & restored;
	ASSERT_NE(restored.parameters, nullptr);
	EXPECT_EQ(restored.parameters->toCustom<JsonNode>()["mdrPenetration"], captured);
}

TEST_F(NewHorizonsPlagueTest, CanonicalShadowRosterAndRawDamageAreRegistered)
{
	const JsonNode rules(JsonPath::builtin("config/newHorizonsMagic"));
	ASSERT_NO_THROW(newHorizonsMagic::validateRules(rules));
	const auto spell = plagueSpell();
	ASSERT_NE(spell, SpellID::NONE);
	EXPECT_FALSE(rules["spells"][std::string(PLAGUE_KEY)].isNull());
	EXPECT_EQ(newHorizonsMagic::spellLevel(rules, spell), 3);
	for(int rank = 0; rank < 4; ++rank)
		EXPECT_EQ(newHorizonsMagic::spellCost(rules, spell, rank), 11);
	EXPECT_EQ(newHorizonsPlague::rawTickDamage(100, 10000), 105);
	EXPECT_EQ(newHorizonsPlague::rawTickDamage(100, 12650), 126);
	EXPECT_NE(spell.toSpell(), nullptr);
}

TEST_F(NewHorizonsPlagueTest, LuaCastWaitDoesNotTickThenStatusTicksOnceInEachOfThreeRounds)
{
	prepare();
	const auto initialHealth = afflicted->getAvailableHealth();
	const auto expectedTick = expectedLegalCastDamage();
	ASSERT_TRUE(castOn(attackerSideHero, plagueSpell(), afflicted));
	ASSERT_TRUE(newHorizonsPlague::hasPlague(afflicted));
	auto marker = plagueStatus(afflicted);
	ASSERT_NE(marker, nullptr);
	EXPECT_EQ(marker->val, expectedTick);
	EXPECT_EQ(marker->turnsRemain, 3);
	EXPECT_EQ(afflicted->getAvailableHealth(), initialHealth);
	ASSERT_NE(marker->parameters, nullptr);
	const auto castParameters = marker->parameters->toCustom<JsonNode>();
	EXPECT_EQ(castParameters["lastProcessedRound"].Integer(), battle()->battleGetRound() - 1);

	// The first WAIT merely defers the creature's action; it must not apply a tick.
	std::vector<int32_t> tickRounds;
	int32_t completedRound = -1;
	ASSERT_TRUE(finishTargetTurn(afflicted, true, &completedRound));
	tickRounds.push_back(completedRound);
	marker = plagueStatus(afflicted);
	ASSERT_NE(marker, nullptr);
	EXPECT_EQ(marker->turnsRemain, 2);
	EXPECT_EQ(damageTo(afflicted->unitId()), expectedTick);

	for(int expectedRemaining : {1, 0})
	{
		ASSERT_TRUE(finishTargetTurn(afflicted, false, &completedRound));
		tickRounds.push_back(completedRound);
		marker = plagueStatus(afflicted);
		if(expectedRemaining == 0)
		{
			EXPECT_EQ(marker, nullptr);
			break;
		}
		ASSERT_NE(marker, nullptr);
		EXPECT_EQ(marker->turnsRemain, expectedRemaining);
	}
	ASSERT_EQ(tickRounds.size(), 3u);
	EXPECT_LT(tickRounds[0], tickRounds[1]);
	EXPECT_LT(tickRounds[1], tickRounds[2]);
	EXPECT_EQ(damageTo(afflicted->unitId()), expectedTick * 3);
	EXPECT_EQ(afflicted->getAvailableHealth(), initialHealth - expectedTick * 3);
}

TEST_F(NewHorizonsPlagueTest, MoraleExtraActivationCannotTickOrExpireTwiceInOneRound)
{
	prepare(false, true);
	const auto expectedTick = expectedLegalCastDamage();
	ASSERT_TRUE(castOn(attackerSideHero, plagueSpell(), afflicted));
	ASSERT_TRUE(activateTarget(afflicted));
	const auto firstInjury = server.injuries.size();
	ASSERT_TRUE(issue(afflicted, BattleAction::makeMove(afflicted, BattleHex(13, 5))));
	ASSERT_TRUE(afflicted->hadMorale);
	auto marker = plagueStatus(afflicted);
	ASSERT_NE(marker, nullptr);
	ASSERT_EQ(marker->turnsRemain, 2);
	const auto firstRound = marker->parameters->toCustom<JsonNode>()["lastProcessedRound"].Integer();
	EXPECT_EQ(damageTo(afflicted->unitId(), firstInjury), expectedTick);

	// The morale-granted activation is a second real action in the same round.
	ASSERT_EQ(battle()->battleActiveUnit()->unitId(), afflicted->unitId());
	ASSERT_TRUE(issue(afflicted, BattleAction::makeDefend(afflicted)));
	marker = plagueStatus(afflicted);
	ASSERT_NE(marker, nullptr);
	EXPECT_EQ(marker->turnsRemain, 2);
	EXPECT_EQ(marker->parameters->toCustom<JsonNode>()["lastProcessedRound"].Integer(), firstRound);
	EXPECT_EQ(damageTo(afflicted->unitId(), firstInjury), expectedTick);
}

TEST_F(NewHorizonsPlagueTest, SpreadSelectsLowestAdjacentReceptiveStackIncludingCasterFriendlyFire)
{
	prepare(true);
	ASSERT_TRUE(castOn(attackerSideHero, plagueSpell(), afflicted));
	ASSERT_TRUE(newHorizonsPlague::hasPlague(afflicted));
	const auto selected = newHorizonsPlague::selectNextSpreadTarget(*battle(), afflicted,
		[this](const battle::Unit * candidate)
		{
			return newHorizonsPlague::isSpreadRecipientReceptive(*battle(), BattleSide::ATTACKER, candidate);
		});
	ASSERT_TRUE(selected.has_value());
	EXPECT_EQ(*selected, friendlyVictim->unitId());
	ASSERT_TRUE(finishTargetTurn(afflicted));
	EXPECT_TRUE(newHorizonsPlague::hasPlague(friendlyVictim));
	EXPECT_FALSE(newHorizonsPlague::hasPlague(enemyVictim));
	EXPECT_TRUE(std::ranges::any_of(server.battleLogLines, [](const std::string & line)
	{
		return line.find("Plague spreads to a friendly stack") != std::string::npos;
	}));
}

TEST_F(NewHorizonsPlagueTest, MarkerSaveRoundTripAndOrdinaryDispelRemoveTheStatus)
{
	prepare();
	ASSERT_TRUE(castOn(attackerSideHero, plagueSpell(), afflicted));
	auto marker = plagueStatus(afflicted);
	ASSERT_NE(marker, nullptr);

	CMemorySerializer wire;
	wire.oser.version = ESerializationVersion::CURRENT;
	wire.iser.version = ESerializationVersion::CURRENT;
	Bonus saved(*marker);
	wire.oser & saved;
	Bonus restored;
	wire.iser & restored;
	EXPECT_EQ(restored.val, marker->val);
	EXPECT_EQ(restored.turnsRemain, marker->turnsRemain);
	ASSERT_NE(restored.parameters, nullptr);
	EXPECT_EQ(restored.parameters->toCustom<JsonNode>(), marker->parameters->toCustom<JsonNode>());

	ASSERT_TRUE(castOn(defenderSideHero, SpellID::DISPEL, afflicted));
	EXPECT_FALSE(newHorizonsPlague::hasPlague(afflicted));
}

TEST_F(NewHorizonsPlagueTest, IndependentMagicalReductionSourcesComposeMultiplicatively)
{
	prepare();
	afflicted->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::SPELL_DAMAGE_REDUCTION, BonusSource::OTHER, 50, BonusSourceID(),
		BonusSubtypeID(newHorizonsMagic::spellSchools(battle()->getMagicRules(), plagueSpell()).front())));
	afflicted->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::SPELL_DAMAGE_REDUCTION, BonusSource::OTHER, 50, BonusSourceID(),
		BonusSubtypeID(SpellSchool::ANY)));
	EXPECT_EQ(newHorizonsPlague::adjustedTickDamage(*battle(), BattleSide::ATTACKER, afflicted, 105), 26);
}
