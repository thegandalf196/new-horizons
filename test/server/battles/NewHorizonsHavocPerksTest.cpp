/*
 * NewHorizonsHavocPerksTest.cpp, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"

#include "HeroCommandFixture.h"
#include "../../hero/NewHorizonsHeroRulesFixture.h"

#include "../../../AI/BattleAI/StackWithBonuses.h"
#include "../../../lib/GameConstants.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/ObstacleHandler.h"
#include "../../../lib/battle/BattleHexArray.h"
#include "../../../lib/battle/CObstacleInstance.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/gameState/CGameState.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/mapObjects/CGTownInstance.h"
#include "../../../lib/mapping/CMap.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/ISpellMechanics.h"
#include "../../../lib/spells/NewHorizonsMagic.h"
#include "../../../lib/spells/Problem.h"
#include "../../../server/CGameHandler.h"
#include "../../../server/battles/BattleProcessor.h"
#include "../../SpellPointTestUtils.h"
#include <vcmi/Environment.h>

#include <algorithm>
#include <cstdint>
#include <map>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
constexpr std::string_view meteorShowerKey = "core:meteorShower";
constexpr std::string_view armageddonKey = "core:armageddon";
const BattleHex meteorCenter(8, 5);

SpellID spellNamed(const std::string_view name)
{
	return SpellID(SpellID::decode(std::string(name)));
}

int findObstacleID(const std::string_view key)
{
	int result = -1;
	LIBRARY->obstacles()->forEach([&](const ObstacleInfo * obstacle, bool & stop)
	{
		if(obstacle->getJsonKey() == key)
		{
			result = obstacle->getIndex();
			stop = true;
		}
	});
	return result;
}

JsonNode magicRulesForFixture(const bool omitStructures)
{
	JsonNode rules(JsonPath::builtin("config/newHorizonsMagic"));
	if(omitStructures)
	{
		// A saved-v3 profile predating the structural producer has no `structures`
		// payload. Keep the direct-damage rows and version intact to exercise that
		// compatibility case rather than dropping back to the pre-v2 spell rules.
		rules[std::string("spells")][std::string(meteorShowerKey)].Struct().erase("structures");
		rules[std::string("spells")][std::string(armageddonKey)].Struct().erase("structures");
		newHorizonsMagic::validateRules(rules);
	}
	return rules;
}

class HavocPredictionEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;

public:
	explicit HavocPredictionEnvironment(std::shared_ptr<CGameState> value)
		: state(std::move(value))
	{}

	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class NewHorizonsHavocPerksTest : public HeroCommandFixture
{
protected:
	bool omitStructures = false;
	const CSpell * activeSpell = nullptr;

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
		map->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, magicRulesForFixture(omitStructures));
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
	}

	void select(std::string_view perk, MasteryLevel::Type rank)
	{
		attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode(std::string(newHorizonsMagic::HAVOC_MAGIC_SKILL))),
			rank, ChangeValueMode::ABSOLUTE);
		const auto lookup = [this](const std::string & id) { return attackerSideHero->getPerkSkillRank(id); };
		for(uint64_t seed = 0; seed < 4096; ++seed)
		{
			const auto offers = attackerSideHero->getPerkState().prepareOffer(lookup, seed);
			for(size_t choice = 0; choice < offers.size(); ++choice)
				if(offers[choice].selection.skillId == newHorizonsMagic::HAVOC_MAGIC_SKILL
					&& offers[choice].selection.perkId == perk)
				{
					gameHandler->levelUpHero(attackerSideHero, offers, choice, seed, false);
					ASSERT_TRUE(attackerSideHero->hasActivePerk(std::string(newHorizonsMagic::HAVOC_MAGIC_SKILL), std::string(perk)));
					return;
				}
		}
		FAIL() << "No legal shipped perk offer for " << perk;
	}

	void meteorStructuralScenario(bool demolitionist, bool meteorologist, int expected)
	{
		prepare(meteorShowerKey, true, false, demolitionist, meteorologist);
		const auto gateHex = battle()->wallPartToBattleHex(EWallPart::GATE);
		ASSERT_TRUE(gateHex.isValid());
		auto * friendly = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(5, 5), 1000);
		auto * hostile = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(12, 5), 1000);
		beginScenario();
		const auto before = structuralHP();
		std::map<EWallPart, bool> attackableBefore;
		for(const auto & [part, hp] : before)
			attackableBefore.emplace(part, battle()->isWallPartAttackable(part));
		const auto healthBefore = hostile->getAvailableHealth();
		const auto manaBefore = attackerSideHero->getManaAvailable();
		spells::BattleCast event(battle(), attackerSideHero, spells::Mode::HERO, activeSpell);
		const auto mechanics = activeSpell->battleMechanics(&event);
		EXPECT_EQ(mechanics->getEffectValue(), 110);
		EXPECT_EQ(mechanics->getNewHorizonsHavocStructuralDamage(), expected);
		ASSERT_TRUE(castAtHex(gateHex));
		EXPECT_EQ(manaBefore - attackerSideHero->getManaAvailable(), battle()->battleGetSpellCost(activeSpell, attackerSideHero));
		EXPECT_EQ(healthBefore - hostile->getAvailableHealth(), 110);
		EXPECT_TRUE(friendly->alive());
		for(const auto & [part, hp] : before)
		{
			const auto hex = battle()->wallPartToBattleHex(part);
			const auto expectedDamage = BattleHex::getDistance(gateHex, hex) <= 1
				&& attackableBefore.at(part) ? std::min(hp, expected) : 0;
			EXPECT_EQ(hp - battle()->getWallStructuralHP(part), expectedDamage);
		}
	}

	void prepare(const std::string_view spellKey, const bool siege = false, const bool legacy = false,
		bool demolitionist = false, bool meteorologist = false, bool cataclysm = false)
	{
		omitStructures = legacy;
		startGame(siege);
		if(demolitionist || meteorologist || cataclysm)
			select(demolitionist ? newHorizonsMagic::HAVOC_DEMOLITIONIST : newHorizonsMagic::HAVOC_PYROMANCER, MasteryLevel::BASIC);
		if(meteorologist || cataclysm)
			select(meteorologist ? newHorizonsMagic::HAVOC_METEOROLOGIST : std::string_view("new-horizons:havocMagic.mineLayer"), MasteryLevel::ADVANCED);
		if(cataclysm)
			select(newHorizonsMagic::HAVOC_CATACLYSM, MasteryLevel::EXPERT);
		activeSpell = spellNamed(spellKey).toSpell();
		ASSERT_NE(activeSpell, nullptr);

		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		attackerSideHero->removeAllSpells();
		attackerSideHero->addSpellToSpellbook(activeSpell->getId());
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 0, ChangeValueMode::ABSOLUTE);
		attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
		setTestSpellPointTotal(attackerSideHero, 1000);

		if(siege)
		{
			const auto towns = gameState()->getPlayerState(PlayerColor(1))->getTowns();
			ASSERT_EQ(towns.size(), 1u);
			ASSERT_GT(towns.front()->fortificationsLevel().wallsHealth, 0);
			startBattle(towns.front());
		}
		else
			startBattle();

		removeDeployedUnits();
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

	void beginScenario()
	{
		beginCombat();
		advanceToAttackerAction();
	}

	void advanceToAttackerAction()
	{
		for(int index = 0; index < 100; ++index)
		{
			const auto * active = battle()->battleActiveUnit();
			ASSERT_NE(active, nullptr);
			const auto owner = battle()->battleGetOwner(active);
			if(owner == attackerSideHero->getOwner())
				return;
			ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), owner,
				BattleAction::makeDefend(active)));
		}
		FAIL() << "No legal attacker-side action became available";
	}

	bool castAtHex(const BattleHex & target)
	{
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = BattleSide::ATTACKER;
		action.spell = activeSpell->getId();
		action.aimToHex(target);
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), attackerSideHero->getOwner(), action);
	}

	bool castGlobally()
	{
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = BattleSide::ATTACKER;
		action.spell = activeSpell->getId();
		action.stackNumber = -1;
		action.aimToHex(BattleHex::INVALID);
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), attackerSideHero->getOwner(), action);
	}

	std::shared_ptr<CObstacleInstance> addPhysicalObstacle(const int uniqueID, const BattleHex & position)
	{
		auto obstacle = std::make_shared<CObstacleInstance>();
		obstacle->uniqueID = uniqueID;
		obstacle->ID = findObstacleID("core:0");
		obstacle->pos = position;
		obstacle->obstacleType = CObstacleInstance::USUAL;
		EXPECT_GE(obstacle->ID, 0);
		battle()->obstacles.push_back(obstacle);
		return obstacle;
	}

	std::shared_ptr<CObstacleInstance> addAbsoluteObstacle(const int uniqueID)
	{
		auto obstacle = std::make_shared<CObstacleInstance>();
		obstacle->uniqueID = uniqueID;
		obstacle->ID = findObstacleID("core:101");
		obstacle->pos = BattleHex::INVALID;
		obstacle->obstacleType = CObstacleInstance::ABSOLUTE_OBSTACLE;
		EXPECT_GE(obstacle->ID, 0);
		battle()->obstacles.push_back(obstacle);
		return obstacle;
	}

	std::shared_ptr<SpellCreatedObstacle> addMoat(const int uniqueID, const BattleHex & position)
	{
		// Non-dispellable town moats are still represented by a spell-obstacle
		// descriptor with MOAT behavior and an explicit hex footprint.
		auto obstacle = std::make_shared<SpellCreatedObstacle>();
		obstacle->uniqueID = uniqueID;
		obstacle->ID = SpellID::decode("core:castleMoat");
		obstacle->trigger = SpellID(SpellID::decode("core:castleMoatTrigger"));
		obstacle->pos = position;
		obstacle->obstacleType = CObstacleInstance::MOAT;
		obstacle->casterSide = BattleSide::DEFENDER;
		obstacle->minimalDamage = 70;
		obstacle->passable = true;
		obstacle->trap = true;
		obstacle->customSize.insert(position);
		battle()->obstacles.push_back(obstacle);
		return obstacle;
	}

	std::shared_ptr<SpellCreatedObstacle> addMagicObstacle(const int uniqueID, const BattleHex & position)
	{
		auto obstacle = std::make_shared<SpellCreatedObstacle>();
		obstacle->uniqueID = uniqueID;
		obstacle->ID = SpellID::QUICKSAND;
		obstacle->pos = position;
		obstacle->passable = true;
		obstacle->customSize.insert(position);
		battle()->obstacles.push_back(obstacle);
		return obstacle;
	}

	std::map<EWallPart, int32_t> structuralHP() const
	{
		std::map<EWallPart, int32_t> result;
		for(int index = 0; index < static_cast<int>(EWallPart::PARTS_COUNT); ++index)
		{
			const auto part = static_cast<EWallPart>(index);
			const auto hp = battle()->getWallStructuralHP(part);
			if(hp > 0)
				result.emplace(part, hp);
		}
		return result;
	}

	template <typename Obstacles>
	static bool containsObstacle(const Obstacles & obstacles, const int uniqueID)
	{
		return std::any_of(obstacles.begin(), obstacles.end(), [uniqueID](const auto & obstacle)
		{
			return obstacle && obstacle->uniqueID == uniqueID;
		});
	}

	BattleInfo * battle() const
	{
		return HeroCommandFixture::battle();
	}
};
}

TEST_F(NewHorizonsHavocPerksTest, SelectedDemolitionistPaidMeteorChangesOnlyStructuralDamage)
{
	meteorStructuralScenario(true, false, 82);
}

TEST_F(NewHorizonsHavocPerksTest, SelectedMeteorologistPaidMeteorChangesOnlyStructuralDamage)
{
	meteorStructuralScenario(false, true, 68);
}

TEST_F(NewHorizonsHavocPerksTest, BothPaidMeteorStructuralBonusesAddBeforeOneFloor)
{
	meteorStructuralScenario(true, true, 96);
}

TEST_F(NewHorizonsHavocPerksTest, UnselectedMeteorRetainsAcceptedStructuralAndUnitBaseline)
{
	meteorStructuralScenario(false, false, 55);
}

TEST_F(NewHorizonsHavocPerksTest, SelectedCataclysmAtZeroPowerPreservesFlat150AndDemoInheritance)
{
	prepare(armageddonKey, true, false, true, false, true);
	auto * friendly = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(5, 5), 1000);
	auto * hostile = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(12, 5), 1000);
	beginScenario();
	spells::BattleCast event(battle(), attackerSideHero, spells::Mode::HERO, activeSpell);
	const auto mechanics = activeSpell->battleMechanics(&event);
	EXPECT_EQ(mechanics->getEffectValue(), 150);
	EXPECT_EQ(mechanics->getNewHorizonsHavocStructuralDamage(), 225);
	const auto before = structuralHP();
	const auto friendlyHP = friendly->getAvailableHealth();
	const auto hostileHP = hostile->getAvailableHealth();
	ASSERT_TRUE(castGlobally());
	EXPECT_EQ(friendlyHP - friendly->getAvailableHealth(), 150);
	EXPECT_EQ(hostileHP - hostile->getAvailableHealth(), 150);
	for(const auto & [part, hp] : before)
		EXPECT_EQ(hp - battle()->getWallStructuralHP(part), std::min(hp, 225));
}

TEST_F(NewHorizonsHavocPerksTest, CataclysmPositivePowerBoostsOnlySPOnceAndMatchesPaidForecast)
{
	prepare(armageddonKey, true, false, true, false, true);
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 100, ChangeValueMode::ABSOLUTE);
	auto * friendly = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(5, 5), 1000);
	auto * hostile = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(12, 5), 1000);
	beginScenario();
	spells::BattleCast event(battle(), attackerSideHero, spells::Mode::HERO, activeSpell);
	const auto mechanics = activeSpell->battleMechanics(&event);
	const auto coefficient = newHorizonsMagic::spellPowerCoefficientBasisPoints(battle()->getMagicRules(), attackerSideHero, activeSpell->getId());
	const auto expected = 150 + spells::scaleSpellPowerComponentWithCoefficientBasisPoints(
		1800, mechanics->getEffectPowerDivisor(), coefficient * 120 / 100);
	EXPECT_EQ(mechanics->getEffectValue(), expected);
	EXPECT_EQ(newHorizonsMagic::spellPowerDamagePerkBonusPercent(battle()->getMagicRules(), attackerSideHero, activeSpell), 20);
	EXPECT_EQ(mechanics->getNewHorizonsHavocStructuralDamage(), expected * 150 / 100);
	const auto friendlyHP = friendly->getAvailableHealth();
	const auto hostileHP = hostile->getAvailableHealth();
	ASSERT_TRUE(castGlobally());
	EXPECT_EQ(friendlyHP - friendly->getAvailableHealth(), expected);
	EXPECT_EQ(hostileHP - hostile->getAvailableHealth(), expected);
}

TEST_F(NewHorizonsHavocPerksTest, CataclysmRemovesBothSidesOrdinaryMagicalCreatorsButKeepsRealTowerMoatAndLandmarks)
{
	prepare(armageddonKey, false, false, false, false, true);
	const std::vector<std::string> creators{"core:fireWall", "core:forceField", "core:landMine", "core:quicksand", "core:earthquake"};
	for(size_t index = 0; index < creators.size(); ++index)
	{
		auto obstacle = addMagicObstacle(400 + static_cast<int>(index), BattleHex(8 + static_cast<int>(index), 2));
		obstacle->ID = SpellID::decode(creators[index]);
		obstacle->casterSide = index % 2 ? BattleSide::ATTACKER : BattleSide::DEFENDER;
	}
	auto tower = addMagicObstacle(410, BattleHex(12, 6));
	tower->ID = SpellID::decode("core:towerMoat");
	tower->trigger = spellNamed("core:landMineTrigger");
	ASSERT_TRUE(spellNamed("core:towerMoat").toSpell()->isCreatureAbility());
	ASSERT_EQ(tower->obstacleType, CObstacleInstance::SPELL_CREATED);
	addMoat(411, BattleHex(13, 6));
	addAbsoluteObstacle(412);
	addPhysicalObstacle(413, BattleHex(9, 6));
	addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(5, 5), 1000);
	addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(12, 5), 1000);
	beginScenario();
	ASSERT_TRUE(castGlobally());
	for(int id = 400; id < 405; ++id)
		EXPECT_FALSE(containsObstacle(battle()->obstacles, id));
	for(int id : {410, 411, 412})
		EXPECT_TRUE(containsObstacle(battle()->obstacles, id));
	EXPECT_FALSE(containsObstacle(battle()->obstacles, 413));
}

TEST_F(NewHorizonsHavocPerksTest, UnselectedArmageddonDoesNotAcquireMagicalCleanup)
{
	prepare(armageddonKey);
	addMagicObstacle(420, BattleHex(8, 2));
	addPhysicalObstacle(421, BattleHex(9, 6));
	addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(5, 5), 1000);
	addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(12, 5), 1000);
	beginScenario();
	ASSERT_TRUE(castGlobally());
	EXPECT_TRUE(containsObstacle(battle()->obstacles, 420));
	EXPECT_FALSE(containsObstacle(battle()->obstacles, 421));
}

TEST_F(NewHorizonsHavocPerksTest, MagicalOnlyCleanupKeepsPaidCastLegalWhenEveryArmyIsImmune)
{
	prepare(armageddonKey, false, false, false, false, true);
	addMagicObstacle(425, BattleHex(8, 2));
	auto * friendly = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(5, 5), 1000);
	auto * hostile = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(12, 5), 1000);
	for(auto * unit : {friendly, hostile})
	{
		auto immunity = std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::SPELL_IMMUNITY,
			BonusSource::OTHER, 1, BonusSourceID());
		immunity->subtype = BonusSubtypeID(activeSpell->getId());
		unit->addNewBonus(std::move(immunity));
	}
	beginScenario();
	const auto friendlyHP = friendly->getAvailableHealth();
	const auto hostileHP = hostile->getAvailableHealth();
	const auto mana = attackerSideHero->getManaAvailable();
	ASSERT_TRUE(castGlobally());
	EXPECT_EQ(friendly->getAvailableHealth(), friendlyHP);
	EXPECT_EQ(hostile->getAvailableHealth(), hostileHP);
	EXPECT_FALSE(containsObstacle(battle()->obstacles, 425));
	EXPECT_EQ(mana - attackerSideHero->getManaAvailable(), battle()->battleGetSpellCost(activeSpell, attackerSideHero));
}

TEST_F(NewHorizonsHavocPerksTest, SelectedMeteorStillDoesNotCleanMagicalObstacleOrWidenImpact)
{
	prepare(meteorShowerKey, false, false, true, true, true);
	addMagicObstacle(430, meteorCenter);
	addPhysicalObstacle(431, BattleHex(9, 5));
	addPhysicalObstacle(432, BattleHex(4, 0));
	addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(5, 5), 1000);
	addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(12, 5), 1000);
	beginScenario();
	ASSERT_TRUE(castAtHex(meteorCenter));
	EXPECT_TRUE(containsObstacle(battle()->obstacles, 430));
	EXPECT_FALSE(containsObstacle(battle()->obstacles, 431));
	EXPECT_TRUE(containsObstacle(battle()->obstacles, 432));
}

TEST_F(NewHorizonsHavocPerksTest, SavedV3MissingStructuralOptInRetainsOrdinaryLegacyCleanup)
{
	prepare(armageddonKey, true, true, true, true, true);
	addMagicObstacle(440, BattleHex(8, 2));
	addPhysicalObstacle(441, BattleHex(9, 6));
	addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(5, 5), 1000);
	addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(12, 5), 1000);
	beginScenario();
	const auto before = structuralHP();
	ASSERT_TRUE(castGlobally());
	EXPECT_TRUE(containsObstacle(battle()->obstacles, 440));
	EXPECT_TRUE(containsObstacle(battle()->obstacles, 441));
	EXPECT_EQ(structuralHP(), before);
}

TEST_F(NewHorizonsHavocPerksTest, DetachedCataclysmCleanupAndDamageMatchPaidCastWithoutLiveMutation)
{
	prepare(armageddonKey, true, false, true, false, true);
	addMagicObstacle(450, BattleHex(8, 2));
	auto tower = addMagicObstacle(451, BattleHex(12, 6));
	tower->ID = SpellID::decode("core:towerMoat");
	auto hidden = addMagicObstacle(452, BattleHex(10, 2));
	hidden->ID = SpellID::LAND_MINE;
	hidden->casterSide = BattleSide::DEFENDER;
	hidden->hidden = true;
	hidden->nativeVisible = false;
	addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(5, 5), 1000);
	auto * hostile = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(12, 5), 1000);
	beginScenario();
	const auto hp = hostile->getAvailableHealth();
	const auto before = structuralHP();
	const auto callback = std::make_shared<CPlayerBattleCallback>(battle(), attackerSideHero->getOwner());
	HavocPredictionEnvironment environment(gameState());
	HypotheticBattle projected(&environment, callback);
	EXPECT_FALSE(containsObstacle(projected.battleGetAllObstacles(), 452))
		<< "Detached prediction must not discover an unrevealed hostile mine";
	spells::BattleCast event(&projected, attackerSideHero, spells::Mode::HERO, activeSpell);
	const auto mechanics = activeSpell->battleMechanics(&event);
	spells::detail::ProblemImpl problem;
	const spells::Target aim{spells::Destination(BattleHex::INVALID)};
	ASSERT_TRUE(mechanics->canBeCast(problem));
	ASSERT_TRUE(mechanics->canBeCastAt(aim, problem));
	mechanics->castEval(projected.getServerCallback(), aim);
	EXPECT_FALSE(containsObstacle(projected.battleGetAllObstacles(), 450));
	EXPECT_TRUE(containsObstacle(projected.battleGetAllObstacles(), 451));
	EXPECT_TRUE(containsObstacle(battle()->obstacles, 450));
	EXPECT_TRUE(containsObstacle(battle()->obstacles, 452));
	EXPECT_EQ(hostile->getAvailableHealth(), hp);
	EXPECT_EQ(structuralHP(), before);
	ASSERT_TRUE(castGlobally());
	EXPECT_EQ(hostile->getAvailableHealth(), projected.battleGetUnitByID(hostile->unitId())->getAvailableHealth());
	for(const auto & [part, oldHP] : before)
		EXPECT_EQ(battle()->getWallStructuralHP(part), projected.getWallStructuralHP(part));
	EXPECT_FALSE(containsObstacle(battle()->obstacles, 450));
	EXPECT_FALSE(containsObstacle(battle()->obstacles, 452));
	EXPECT_TRUE(containsObstacle(battle()->obstacles, 451));
}

TEST(NewHorizonsHavocPerkMathTest, SingleFloorAdditiveCategoryAndSaturationAreExact)
{
	EXPECT_EQ(newHorizonsMagic::scaleHavocStructuralDamage(111, 50, 75), 97);
	EXPECT_EQ(newHorizonsMagic::scaleHavocStructuralDamage(110, 50, 50), 82);
	EXPECT_EQ(newHorizonsMagic::scaleHavocStructuralDamage(110, 50, 25), 68);
	EXPECT_EQ(newHorizonsMagic::scaleHavocStructuralDamage(110, 50, 75), 96);
	EXPECT_EQ(newHorizonsMagic::scaleHavocStructuralDamage(150, 100, 50), 225);
	EXPECT_EQ(newHorizonsMagic::scaleHavocStructuralDamage(std::numeric_limits<int64_t>::max(), 10000, 75), 65535);
	EXPECT_EQ(newHorizonsMagic::scaleHavocStructuralDamage(0, 50, 75), 0);
	EXPECT_EQ(newHorizonsMagic::scaleHavocStructuralDamage(-1, 50, 75), 0);
	EXPECT_THROW(newHorizonsMagic::scaleHavocStructuralDamage(10, 50, 100), std::invalid_argument);
}
