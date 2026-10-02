/*
 * NewHorizonsEarthquakeTest.cpp, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"

#include "HeroCommandFixture.h"
#include "../../hero/NewHorizonsHeroRulesFixture.h"

#include "../../../AI/BattleAI/StackWithBonuses.h"
#include "../../../lib/GameConstants.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/bonuses/BonusSelector.h"
#include "../../../lib/battle/CObstacleInstance.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/battle/ReachabilityInfo.h"
#include "../../../lib/gameState/CGameState.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/mapObjects/CGTownInstance.h"
#include "../../../lib/mapping/CMap.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/spells/BattleSpellMechanics.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/ISpellMechanics.h"
#include "../../../lib/spells/NewHorizonsMagic.h"
#include "../../../server/CGameHandler.h"
#include "../../../server/battles/BattleProcessor.h"
#include <vcmi/Environment.h>

#include <algorithm>
#include <cstdint>
#include <map>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
constexpr std::string_view NATURE_MAGIC = "new-horizons:natureMagic";
constexpr std::string_view NATURE_BASIC_PERK = "new-horizons:natureMagic.rootcaller";
constexpr std::string_view GEOMANCER = "new-horizons:natureMagic.geomancer";

JsonNode magicRulesForFixture(const bool savedV2, const bool removeEarthquakeRow)
{
	JsonNode rules(JsonPath::builtin("config/newHorizonsMagic"));
	if(savedV2)
	{
		rules["rulesetVersion"].Integer() = newHorizonsMagic::DIRECT_DAMAGE_RULESET_VERSION;
		rules.Struct().erase("schoolRankPowerCoefficientPercent");
		rules.Struct().erase("spellcraftEfficiencyPercent");
		for(auto & [name, spell] : rules["spells"].Struct())
		{
			(void)name;
			spell.Struct().erase("earthquake");
			spell.Struct().erase("structures");
			spell.Struct().erase("selectedPlacement");
			if(spell.Struct().contains("variant"))
			{
				spell.Struct().erase("variant");
				spell["active"].Bool() = false;
			}
		}
		newHorizonsMagic::validateRules(rules);
	}
	else if(removeEarthquakeRow)
	{
		rules["spells"]["core:earthquake"].Struct().erase("earthquake");
		newHorizonsMagic::validateRules(rules);
	}
	return rules;
}

bool activateGeomancer(JsonNode & rules)
{
	auto & perks = rules["skills"][std::string(NATURE_MAGIC)]["perks"].Vector();
	const auto found = std::find_if(perks.begin(), perks.end(), [](const JsonNode & perk)
	{
		return perk["id"].String() == GEOMANCER;
	});
	if(found == perks.end())
		return false;

	(*found)["effect"]["status"].String() = "active";
	return true;
}

class EarthquakePredictionEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;

public:
	explicit EarthquakePredictionEnvironment(std::shared_ptr<CGameState> value)
		: state(std::move(value))
	{}

	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class NewHorizonsEarthquakeTest : public HeroCommandFixture
{
protected:
	bool useSavedV2 = false;
	bool removeEarthquakeRow = false;
	bool allowGeomancer = false;
	CStack * moverOutsideField = nullptr;
	CStack * friendlyGround = nullptr;
	CStack * hostileGround = nullptr;
	CStack * hostileFlyer = nullptr;
	CStack * friendlyImmune = nullptr;
	CStack * distant = nullptr;
	const BattleHex fieldCenter{8, 5};

	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";
	}

	void mapLoaded(CMap * map) override
	{
		HeroCommandFixture::mapLoaded(map);
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, testHeroRules());
		map->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS,
			magicRulesForFixture(useSavedV2, removeEarthquakeRow));

		JsonNode perks(JsonPath::builtin("config/newHorizonsPerks"));
		if(allowGeomancer && !activateGeomancer(perks))
			throw std::runtime_error("Missing Geomancer from the New Horizons perk registry");
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, std::move(perks));
	}

	SecondarySkill natureMagic() const
	{
		const int decoded = SecondarySkill::decode(std::string(NATURE_MAGIC));
		EXPECT_GE(decoded, 0);
		return SecondarySkill(decoded);
	}

	void prepareNatureRank(const bool geomancer)
	{
		const auto nature = natureMagic();
		attackerSideHero->setSecSkillLevel(nature, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		if(!geomancer)
			return;

		attackerSideHero->applyPerkSelection({std::string(NATURE_MAGIC), std::string(NATURE_BASIC_PERK)});
		ASSERT_TRUE(attackerSideHero->hasActivePerk(std::string(NATURE_MAGIC), std::string(NATURE_BASIC_PERK)));
		attackerSideHero->setSecSkillLevel(nature, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
		attackerSideHero->applyPerkSelection({std::string(NATURE_MAGIC), std::string(GEOMANCER)});
		ASSERT_TRUE(attackerSideHero->hasActivePerk(std::string(NATURE_MAGIC), std::string(GEOMANCER)));
	}

	void prepareField(const int spellPower = 10, const bool geomancer = false)
	{
		allowGeomancer = geomancer;
		startGame();
		prepareNatureRank(geomancer);

		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		attackerSideHero->removeAllSpells();
		attackerSideHero->addSpellToSpellbook(SpellID::EARTHQUAKE);
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, spellPower, ChangeValueMode::ABSOLUTE);
		attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
		setTestSpellPointTotal(attackerSideHero, 1000);

		startBattle();
		removeDeployedUnits();
		moverOutsideField = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(5, 5), 200);
		friendlyGround = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(7, 5), 200);
		hostileGround = addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), BattleHex(10, 5), 200);
		hostileFlyer = addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), BattleHex(8, 7), 200);
		friendlyImmune = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(8, 3), 200);
		distant = addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), BattleHex(12, 5), 200);
		ASSERT_NE(moverOutsideField, nullptr);
		ASSERT_NE(friendlyGround, nullptr);
		ASSERT_NE(hostileGround, nullptr);
		ASSERT_NE(hostileFlyer, nullptr);
		ASSERT_NE(friendlyImmune, nullptr);
		ASSERT_NE(distant, nullptr);
		hostileFlyer->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
			BonusType::FLYING, BonusSource::OTHER, 0, BonusSourceID()));
		friendlyImmune->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
			BonusType::LEVEL_SPELL_IMMUNITY, BonusSource::OTHER, 3, BonusSourceID()));
		beginCombat();
	}

	void prepareSiege(const int spellPower, const bool geomancer)
	{
		allowGeomancer = geomancer;
		startGame(true);
		prepareNatureRank(geomancer);
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		attackerSideHero->removeAllSpells();
		attackerSideHero->addSpellToSpellbook(SpellID::EARTHQUAKE);
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, spellPower, ChangeValueMode::ABSOLUTE);
		attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
		setTestSpellPointTotal(attackerSideHero, 1000);

		const auto towns = gameState()->getPlayerState(PlayerColor(1))->getTowns();
		ASSERT_EQ(towns.size(), 1u);
		ASSERT_GT(towns.front()->fortificationsLevel().wallsHealth, 0);
		startBattle(towns.front());
		beginCombat();
	}

	void leaveOnlyGateWithOneStructuralHP()
	{
		ASSERT_TRUE(battle()->si.canonicalStructuralHP);
		for(int index = 0; index < static_cast<int>(EWallPart::PARTS_COUNT); ++index)
		{
			const auto part = static_cast<EWallPart>(index);
			if(part != EWallPart::GATE && battle()->isWallPartAttackable(part))
				battle()->setWallState(part, EWallState::DESTROYED);
		}
		battle()->setWallStructuralHP(EWallPart::GATE, 1);
		battle()->si.gateState = EGateState::CLOSED;
		ASSERT_TRUE(battle()->isWallPartAttackable(EWallPart::GATE));
		ASSERT_EQ(battle()->getWallStructuralHP(EWallPart::GATE), 1);
	}

	void removeDeployedUnits()
	{
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		if(!remove.changedStacks.empty())
			gameHandler->sendAndApply(remove);
	}

	BattleAction fieldAction(const BattleHex & center) const
	{
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = BattleSide::ATTACKER;
		action.spell = SpellID::EARTHQUAKE;
		action.aimToHex(center);
		return action;
	}

	bool castField(const BattleHex & center = BattleHex(8, 5))
	{
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), fieldAction(center));
	}

	std::vector<const SpellCreatedObstacle *> earthquakeTerrain() const
	{
		std::vector<const SpellCreatedObstacle *> result;
		for(const auto & obstacle : battle()->obstacles)
		{
			const auto * spellObstacle = dynamic_cast<const SpellCreatedObstacle *>(obstacle.get());
			if(spellObstacle && spellObstacle->ID == SpellID::EARTHQUAKE)
				result.push_back(spellObstacle);
		}
		return result;
	}

	void advanceToAttackerAction()
	{
		for(int index = 0; index < 100; ++index)
		{
			const auto * active = battle()->battleActiveUnit();
			ASSERT_NE(active, nullptr);
			const auto owner = battle()->battleGetOwner(active);
			if(owner == PlayerColor(0))
				return;
			ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), owner,
				BattleAction::makeDefend(active)));
		}
		FAIL() << "No legal attacker-side siege activation became available";
	}

	BattleInfo * battle() const
	{
		return HeroCommandFixture::battle();
	}
};
}

TEST_F(NewHorizonsEarthquakeTest, ActualFieldCastHitsGroundUnitsCreatesTerrainAndChargesMovement)
{
	prepareField();
	ASSERT_EQ(battle()->getMagicRules()["rulesetVersion"].Integer(), newHorizonsMagic::CURRENT_RULESET_VERSION);
	ASSERT_TRUE(newHorizonsMagic::earthquakeRulesEnabled(battle()->getMagicRules(), SpellID::EARTHQUAKE));
	ASSERT_EQ(BattleHex::getDistance(fieldCenter, moverOutsideField->getPosition()), 3);
	ASSERT_EQ(BattleHex::getDistance(fieldCenter, friendlyGround->getPosition()), 1);
	ASSERT_EQ(BattleHex::getDistance(fieldCenter, hostileGround->getPosition()), 2);
	ASSERT_EQ(BattleHex::getDistance(fieldCenter, hostileFlyer->getPosition()), 2);
	ASSERT_EQ(BattleHex::getDistance(fieldCenter, friendlyImmune->getPosition()), 2);
	ASSERT_GT(BattleHex::getDistance(fieldCenter, distant->getPosition()), 2);
	const auto * spell = SpellID(SpellID::EARTHQUAKE).toSpell();
	ASSERT_NE(spell, nullptr);
	ASSERT_EQ(battle()->battleGetSpellLevel(spell->getId()), 3);
	const auto immunityBonuses = friendlyImmune->getBonusesOfType(BonusType::LEVEL_SPELL_IMMUNITY);
	ASSERT_EQ(immunityBonuses->size(), 1u);
	ASSERT_EQ(immunityBonuses->front()->val, 3);
	ASSERT_EQ(immunityBonuses->totalValue(), 3);
	spells::BattleCast immunityCast(battle(), attackerSideHero, spells::Mode::HERO, spell);
	const auto immunityMechanics = spell->battleMechanics(&immunityCast);
	ASSERT_FALSE(immunityMechanics->isReceptive(friendlyImmune));

	const auto friendlyHealth = friendlyGround->getAvailableHealth();
	const auto hostileHealth = hostileGround->getAvailableHealth();
	const auto flyingHealth = hostileFlyer->getAvailableHealth();
	const auto immuneHealth = friendlyImmune->getAvailableHealth();
	const auto distantHealth = distant->getAvailableHealth();
	const auto friendlyMorale = friendlyGround->valOfBonuses(BonusType::MORALE);
	const auto friendlySpeed = friendlyGround->valOfBonuses(BonusType::STACKS_SPEED);
	const auto friendlyAttack = friendlyGround->valOfBonuses(BonusType::PRIMARY_SKILL,
		BonusSubtypeID(PrimarySkill::ATTACK));
	const auto friendlyDefense = friendlyGround->valOfBonuses(BonusType::PRIMARY_SKILL,
		BonusSubtypeID(PrimarySkill::DEFENSE));
	const auto mana = attackerSideHero->getManaAvailable();
	const auto cost = battle()->battleGetSpellCost(spell, attackerSideHero);
	const auto active = battle()->battleActiveUnit();

	ASSERT_TRUE(castField(fieldCenter));
	EXPECT_EQ(friendlyGround->getAvailableHealth(), friendlyHealth - 39); // 30 + floor(0.8 * 10 SP * 1.15 Basic Nature)
	EXPECT_EQ(hostileGround->getAvailableHealth(), hostileHealth - 39);
	EXPECT_EQ(hostileFlyer->getAvailableHealth(), flyingHealth);
	EXPECT_EQ(friendlyImmune->getAvailableHealth(), immuneHealth);
	EXPECT_EQ(distant->getAvailableHealth(), distantHealth);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana - 12);
	EXPECT_EQ(cost, 12);
	EXPECT_EQ(battle()->battleActiveUnit(), active);
	EXPECT_EQ(battle()->battleCastSpells(BattleSide::ATTACKER), 1);
	EXPECT_FALSE(battle()->battleCanUseHeroCommand(BattleSide::ATTACKER, HeroCommand::CHARGE));
	EXPECT_EQ(friendlyGround->valOfBonuses(BonusType::MORALE), friendlyMorale);
	EXPECT_EQ(friendlyGround->valOfBonuses(BonusType::STACKS_SPEED), friendlySpeed);
	EXPECT_EQ(friendlyGround->valOfBonuses(BonusType::PRIMARY_SKILL,
		BonusSubtypeID(PrimarySkill::ATTACK)), friendlyAttack);
	EXPECT_EQ(friendlyGround->valOfBonuses(BonusType::PRIMARY_SKILL,
		BonusSubtypeID(PrimarySkill::DEFENSE)), friendlyDefense);

	const auto terrain = earthquakeTerrain();
	ASSERT_EQ(terrain.size(), 19u); // the complete in-bounds radius-two field, including empty/immune hexes
	for(const auto * patch : terrain)
	{
		ASSERT_TRUE(patch->passable);
		EXPECT_EQ(patch->turnsRemaining, 3);
		EXPECT_EQ(patch->movementCost, 1);
		EXPECT_EQ(patch->customSize.size(), 1u);
		EXPECT_LE(BattleHex::getDistance(fieldCenter, patch->pos), 2);
	}
	EXPECT_TRUE(std::any_of(terrain.begin(), terrain.end(), [this](const SpellCreatedObstacle * patch)
	{
		return patch->pos == fieldCenter;
	}));
	EXPECT_TRUE(std::any_of(terrain.begin(), terrain.end(), [this](const SpellCreatedObstacle * patch)
	{
		return patch->pos == friendlyImmune->getPosition();
	}));

	const ReachabilityInfo movement = battle()->getReachability(
		ReachabilityInfo::Parameters(moverOutsideField, moverOutsideField->getPosition()));
	const BattleHex firstFieldHex(6, 5);
	ASSERT_TRUE(movement.isReachable(firstFieldHex));
	EXPECT_EQ(movement.distances[firstFieldHex.toInt()], 2u)
		<< "Entering a newly created Fractured Ground hex costs one extra movement point";
}

TEST_F(NewHorizonsEarthquakeTest, DetachedCastEvalMatchesDamageAndTerrainWithoutTouchingTheLiveBattle)
{
	prepareField();
	const auto liveHealth = friendlyGround->getAvailableHealth();
	const auto liveMana = attackerSideHero->getManaAvailable();
	const auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	EarthquakePredictionEnvironment environment(gameState());
	HypotheticBattle projected(&environment, callback);

	const Bonus materialization(BonusDuration::ONE_BATTLE, BonusType::MORALE,
		BonusSource::OTHER, 0, BonusSourceID());
	projected.addUnitBonus(friendlyGround->unitId(), {materialization});
	const auto * projectedTarget = projected.battleGetUnitByID(friendlyGround->unitId());
	ASSERT_NE(projectedTarget, nullptr);
	const auto projectedHealth = projectedTarget->getAvailableHealth();

	const auto * spell = SpellID(SpellID::EARTHQUAKE).toSpell();
	ASSERT_NE(spell, nullptr);
	spells::BattleCast castEvent(&projected, attackerSideHero, spells::Mode::HERO, spell);
	const auto mechanics = spell->battleMechanics(&castEvent);
	const spells::Target aim{spells::Destination(fieldCenter)};
	mechanics->castEval(projected.getServerCallback(), aim);

	const auto * projectedAfter = projected.battleGetUnitByID(friendlyGround->unitId());
	ASSERT_NE(projectedAfter, nullptr);
	EXPECT_EQ(projectedAfter->getAvailableHealth(), projectedHealth - 39); // Basic Nature's 115% applies only to 0.8 * SP
	EXPECT_EQ(friendlyGround->getAvailableHealth(), liveHealth);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), liveMana);
	EXPECT_TRUE(earthquakeTerrain().empty());
	EXPECT_EQ(projected.battleGetAllObstacles().size(), 19u);
}

TEST_F(NewHorizonsEarthquakeTest, AdvancedGeomancerExtendsFracturedGroundByOneRound)
{
	prepareField(0, true);
	ASSERT_TRUE(attackerSideHero->hasActivePerk(std::string(NATURE_MAGIC), std::string(GEOMANCER)));
	ASSERT_TRUE(castField(fieldCenter));
	const auto terrain = earthquakeTerrain();
	ASSERT_EQ(terrain.size(), 19u);
	for(const auto * patch : terrain)
		EXPECT_EQ(patch->turnsRemaining, 4);
}

TEST_F(NewHorizonsEarthquakeTest, SiegeCastSelectsNearestDistinctSectionsAndUsesConfiguredStructuralDamage)
{
	prepareSiege(160, false);
	ASSERT_TRUE(battle()->hasFortifications());
	ASSERT_TRUE(battle()->si.canonicalStructuralHP);
	const auto selectedHex = battle()->wallPartToBattleHex(EWallPart::GATE);
	ASSERT_TRUE(selectedHex.isValid());

	std::map<EWallPart, int64_t> before;
	for(int index = 0; index < static_cast<int>(EWallPart::PARTS_COUNT); ++index)
	{
		const auto part = static_cast<EWallPart>(index);
		if(battle()->isWallPartAttackable(part))
			before.emplace(part, battle()->getWallStructuralHP(part));
	}
	ASSERT_GE(before.size(), 4u);
	const auto mana = attackerSideHero->getManaAvailable();
	const auto * spell = SpellID(SpellID::EARTHQUAKE).toSpell();
	ASSERT_NE(spell, nullptr);
	const auto cost = battle()->battleGetSpellCost(spell, attackerSideHero);
	ASSERT_EQ(cost, 12);
	advanceToAttackerAction();
	ASSERT_TRUE(castField(selectedHex));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana - 12);

	int32_t damagedSections = 0;
	for(const auto & [part, hpBefore] : before)
	{
		const auto damage = hpBefore - battle()->getWallStructuralHP(part);
		if(damage > 0)
		{
			++damagedSections;
			EXPECT_EQ(damage, 100) << "Each selected section uses configured base structural damage";
		}
	}
	EXPECT_EQ(damagedSections, 4) << "160 raw Spell Power selects the configured maximum of four distinct sections";
	EXPECT_GT(before.at(EWallPart::GATE), battle()->getWallStructuralHP(EWallPart::GATE))
		<< "The selected section is included in the nearest-section selection";
}

TEST_F(NewHorizonsEarthquakeTest, GeomancerDealsTwentyFivePercentMoreDamageToTheSelectedSiegeSection)
{
	prepareSiege(0, true);
	ASSERT_TRUE(battle()->si.canonicalStructuralHP);
	for(int index = 0; index < static_cast<int>(EWallPart::PARTS_COUNT); ++index)
	{
		const auto part = static_cast<EWallPart>(index);
		if(part != EWallPart::GATE && battle()->isWallPartAttackable(part))
			battle()->setWallState(part, EWallState::DESTROYED);
	}
	battle()->setWallStructuralHP(EWallPart::GATE, 200);
	ASSERT_TRUE(battle()->isWallPartAttackable(EWallPart::GATE));
	ASSERT_EQ(battle()->getWallStructuralHP(EWallPart::GATE), 200);

	const auto selectedHex = battle()->wallPartToBattleHex(EWallPart::GATE);
	ASSERT_TRUE(selectedHex.isValid());
	advanceToAttackerAction();
	ASSERT_TRUE(castField(selectedHex));
	EXPECT_EQ(battle()->getWallStructuralHP(EWallPart::GATE), 75);
}

TEST_F(NewHorizonsEarthquakeTest, SavedV3WithoutEarthquakeRowFallsBackToLegacyNoTargetSiegeMode)
{
	removeEarthquakeRow = true;
	prepareSiege(10, false);
	ASSERT_EQ(battle()->getMagicRules()["rulesetVersion"].Integer(), newHorizonsMagic::CURRENT_RULESET_VERSION);
	ASSERT_FALSE(newHorizonsMagic::earthquakeRulesEnabled(battle()->getMagicRules(), SpellID::EARTHQUAKE));
	leaveOnlyGateWithOneStructuralHP();
	advanceToAttackerAction();

	ASSERT_TRUE(castField(BattleHex::INVALID));
	EXPECT_TRUE(earthquakeTerrain().empty());
	EXPECT_EQ(battle()->getWallStructuralHP(EWallPart::GATE), 0)
		<< "Without the saved-v3 field row, the old no-target siege Earthquake still resolves";
}

TEST_F(NewHorizonsEarthquakeTest, SavedV2EarthquakeAlsoRetainsLegacyNoTargetSiegeMode)
{
	useSavedV2 = true;
	prepareSiege(10, false);
	ASSERT_EQ(battle()->getMagicRules()["rulesetVersion"].Integer(), newHorizonsMagic::DIRECT_DAMAGE_RULESET_VERSION);
	ASSERT_FALSE(newHorizonsMagic::earthquakeRulesEnabled(battle()->getMagicRules(), SpellID::EARTHQUAKE));
	leaveOnlyGateWithOneStructuralHP();
	advanceToAttackerAction();

	ASSERT_TRUE(castField(BattleHex::INVALID));
	EXPECT_TRUE(earthquakeTerrain().empty());
	EXPECT_EQ(battle()->getWallStructuralHP(EWallPart::GATE), 0);
}
