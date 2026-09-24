/*
 * TransfigureMatterPerkTest.cpp, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"

#include "HeroCommandFixture.h"

#include "../../../lib/GameLibrary.h"
#include "../../../lib/ObstacleHandler.h"
#include "../../../lib/battle/CObstacleInstance.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/effects/Effect.h"

namespace
{
constexpr auto spellKey = "new-horizons:transfigureMatter";
constexpr auto sorcerySkill = "new-horizons:sorceryMagic";
constexpr auto matterShaperPerk = "new-horizons:sorceryMagic.matterShaper";

SpellID transfigureMatter()
{
	return SpellID(SpellID::decode(spellKey));
}

int obstacleID(std::string_view key)
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
}

class TransfigureMatterPerkTest : public HeroCommandFixture
{
protected:
	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons module";
		useCommands = false;
	}

	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
	}

	void prepare(bool selectMatterShaper, int spellPower, bool healthArtifact = false)
	{
		startGame();

		const auto spell = transfigureMatter();
		ASSERT_GE(spell.getNum(), 0);
		const auto sorcery = SecondarySkill::decode(sorcerySkill);
		ASSERT_GE(sorcery, 0);
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, spellPower, ChangeValueMode::ABSOLUTE);
		attackerSideHero->setSecSkillLevel(SecondarySkill(sorcery), 1, ChangeValueMode::ABSOLUTE);
		if(selectMatterShaper)
			attackerSideHero->applyPerkSelection({sorcerySkill, matterShaperPerk});
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		if(healthArtifact)
			giveArtifact(attackerSideHero, ArtifactID(ArtifactID::decode("core:ringOfVitality")), ArtifactPosition::MISC1);
		attackerSideHero->addSpellToSpellbook(spell);
		setTestSpellPointTotal(attackerSideHero, 100);

		startBattle();
		beginCombat();

		active = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(2, 5), 1);
		enemy = addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), BattleHex(14, 5), 1);
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			if(unit != active && unit != enemy)
				remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);

		BattleSetActiveStack activate;
		activate.battleID = BattleID(0);
		activate.stack = active->unitId();
		activate.reason = BattleUnitTurnReason::TURN_QUEUE;
		gameHandler->sendAndApply(activate);
	}

	std::shared_ptr<CObstacleInstance> addPhysicalObstacle(int uniqueID, BattleHex position, std::string_view obstacleKey = "core:0")
	{
		auto obstacle = std::make_shared<CObstacleInstance>();
		obstacle->uniqueID = uniqueID;
		obstacle->ID = obstacleID(obstacleKey);
		EXPECT_GE(obstacle->ID, 0);
		obstacle->pos = position;
		obstacle->obstacleType = CObstacleInstance::USUAL;
		battle()->obstacles.push_back(obstacle);
		return obstacle;
	}

	std::shared_ptr<CObstacleInstance> addRejectedObstacle(CObstacleInstance::EObstacleType type, int id, BattleHex position)
	{
		auto obstacle = std::make_shared<CObstacleInstance>();
		obstacle->uniqueID = id;
		obstacle->ID = 0;
		obstacle->pos = position;
		obstacle->obstacleType = type;
		battle()->obstacles.push_back(obstacle);
		return obstacle;
	}

	bool castAt(BattleHex position)
	{
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = BattleSide::ATTACKER;
		action.spell = transfigureMatter();
		action.aimToHex(position);
		const auto accepted = gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action);
		return accepted;
	}

	std::vector<const battle::Unit *> diamondGolems() const
	{
		std::vector<const battle::Unit *> result;
		for(const auto * unit : battle()->battleGetAllUnits(false))
			if(unit->unitType() && unit->unitType()->getJsonKey() == "core:diamondGolem")
				result.push_back(unit);
		return result;
	}

	CStack * active = nullptr;
	CStack * enemy = nullptr;
};

TEST_F(TransfigureMatterPerkTest, CanonicalManaCostIsEightAtEverySorceryRank)
{
	startGame();
	const auto spell = transfigureMatter();
	ASSERT_NE(spell, SpellID::NONE);
	for(int mastery = 0; mastery < 4; ++mastery)
		EXPECT_EQ(newHorizonsMagic::spellCost(gameState()->getMagicRules(), spell, mastery), 8);
}

TEST_F(TransfigureMatterPerkTest, ConvertsPhysicalFootprintIntoExactTemporaryGolemHealth)
{
	constexpr int spellPower = 40;
	prepare(false, spellPower);
	const auto obstacle = addPhysicalObstacle(20, BattleHex(8, 5));
	const auto footprint = static_cast<int64_t>(obstacle->getAffectedTiles().size());
	const auto spell = transfigureMatter();
	ASSERT_EQ(attackerSideHero->getEffectPower(spell.toSpell()), spellPower);
	const auto expectedHealth = 80LL + 2LL * spellPower + 50LL * footprint;
	const auto maxHealth = static_cast<int64_t>(creatureByName("core:diamondGolem").toEntity(LIBRARY)->getMaxHealth());
	const auto expectedCount = (expectedHealth + maxHealth - 1) / maxHealth;
	const auto manaBefore = attackerSideHero->getManaAvailable();

	ASSERT_GT(footprint, 1);
	ASSERT_TRUE(castAt(obstacle->pos));

	const auto golems = diamondGolems();
	ASSERT_EQ(golems.size(), 1u);
	EXPECT_EQ(golems.front()->getCount(), expectedCount);
	EXPECT_EQ(golems.front()->getTotalHealth(), expectedCount * maxHealth);
	EXPECT_EQ(golems.front()->getAvailableHealth(), expectedHealth);
	EXPECT_NE(expectedHealth % maxHealth, 0);
	EXPECT_TRUE(golems.front()->isSummoned());
	EXPECT_EQ(golems.front()->getPosition(), obstacle->pos);
	EXPECT_TRUE(battle()->obstacles.empty());
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore - 8);
}

TEST_F(TransfigureMatterPerkTest, MatterShaperAddsTwentyFivePercentWithIntegralRounding)
{
	constexpr int spellPower = 41;
	prepare(true, spellPower);
	const auto obstacle = addPhysicalObstacle(20, BattleHex(8, 5));
	const auto footprint = static_cast<int64_t>(obstacle->getAffectedTiles().size());
	const auto baseHealth = 80LL + 2LL * spellPower + 50LL * footprint;
	const auto expectedHealth = baseHealth * 5 / 4; // integer health uses floor rounding

	ASSERT_TRUE(attackerSideHero->hasActivePerk(sorcerySkill, matterShaperPerk));
	ASSERT_TRUE(castAt(obstacle->pos));

	const auto golems = diamondGolems();
	ASSERT_EQ(golems.size(), 1u);
	EXPECT_EQ(golems.front()->getAvailableHealth(), expectedHealth);
	EXPECT_TRUE(battle()->obstacles.empty());
}

TEST_F(TransfigureMatterPerkTest, HealthBonusesDoNotInflateCanonicalAggregatePool)
{
	constexpr int spellPower = 40;
	prepare(false, spellPower, true);
	const auto obstacle = addPhysicalObstacle(20, BattleHex(8, 5));
	const auto expectedHealth = 80LL + 2LL * spellPower
		+ 50LL * static_cast<int64_t>(obstacle->getAffectedTiles().size());
	const auto * spell = transfigureMatter().toSpell();
	const auto preview = battle()->getSpellEffectValue(spell, attackerSideHero, spells::Mode::HERO,
		obstacle->getAffectedTiles().front());
	const auto effectiveMaxHealth = creatureByName("core:diamondGolem").toEntity(LIBRARY)->getMaxHealth() + 1;
	const auto expectedCount = (expectedHealth + effectiveMaxHealth - 1) / effectiveMaxHealth;
	ASSERT_NE(preview, nullptr);
	EXPECT_EQ(preview->hpDelta, expectedHealth);
	EXPECT_EQ(preview->unitsDelta, expectedCount);

	ASSERT_TRUE(castAt(obstacle->getAffectedTiles().front()));

	const auto golems = diamondGolems();
	ASSERT_EQ(golems.size(), 1u);
	EXPECT_GT(golems.front()->getMaxHealth(), creatureByName("core:diamondGolem").toEntity(LIBRARY)->getMaxHealth());
	EXPECT_EQ(golems.front()->getCount(), expectedCount);
	EXPECT_EQ(golems.front()->getAvailableHealth(), expectedHealth);
}

TEST_F(TransfigureMatterPerkTest, AnchorExcludingObstacleUsesItsRealFootprintForTargetAndPlacement)
{
	prepare(false, 40);
	const auto obstacle = addPhysicalObstacle(20, BattleHex(8, 6), "core:5"); // offsets 1, 2, 3; anchor is not blocked
	const auto footprint = obstacle->getAffectedTiles();
	ASSERT_EQ(footprint.size(), 3u);
	ASSERT_FALSE(footprint.contains(obstacle->pos));
	const auto expectedHealth = 80LL + 2LL * 40 + 50LL * static_cast<int64_t>(footprint.size());

	ASSERT_TRUE(castAt(footprint.back()));

	const auto golems = diamondGolems();
	ASSERT_EQ(golems.size(), 1u);
	EXPECT_TRUE(footprint.contains(golems.front()->getPosition()));
	EXPECT_EQ(golems.front()->getAvailableHealth(), expectedHealth);
	EXPECT_TRUE(battle()->obstacles.empty());
}

TEST_F(TransfigureMatterPerkTest, OneHexAnchorExcludingObstacleRejectsOccupiedPlacementAtomically)
{
	prepare(false, 40);
	const auto obstacle = addPhysicalObstacle(20, BattleHex(9, 6), "core:8"); // offset -16; anchor is not blocked
	const auto footprint = obstacle->getAffectedTiles();
	ASSERT_EQ(footprint.size(), 1u);
	ASSERT_FALSE(footprint.contains(obstacle->pos));
	const auto target = footprint.front();
	addStack(BattleSide::ATTACKER, creatureByName("core:peasant"), target, 1);
	const auto manaBefore = attackerSideHero->getManaAvailable();
	const auto obstacleCountBefore = battle()->obstacles.size();
	const auto unitCountBefore = battle()->battleGetAllUnits(false).size();

	EXPECT_FALSE(castAt(target));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore);
	EXPECT_EQ(battle()->obstacles.size(), obstacleCountBefore);
	EXPECT_EQ(battle()->battleGetAllUnits(false).size(), unitCountBefore);
	EXPECT_TRUE(diamondGolems().empty());
}

TEST_F(TransfigureMatterPerkTest, InvalidObstacleCategoriesAreRejectedAtomically)
{
	prepare(false, 40);
	const BattleHex absolutePosition(8, 5);
	const BattleHex moatPosition(11, 5);
	const BattleHex magicalPosition(13, 5);
	addRejectedObstacle(CObstacleInstance::ABSOLUTE_OBSTACLE, 20, absolutePosition);
	addRejectedObstacle(CObstacleInstance::MOAT, 21, moatPosition);
	auto magical = std::make_shared<SpellCreatedObstacle>();
	magical->uniqueID = 22;
	magical->pos = magicalPosition;
	magical->customSize.insert(magicalPosition);
	battle()->obstacles.push_back(magical);
	const auto manaBefore = attackerSideHero->getManaAvailable();
	const auto obstacleCountBefore = battle()->obstacles.size();
	const auto unitCountBefore = battle()->battleGetAllUnits(false).size();

	for(const auto position : {absolutePosition, moatPosition, magicalPosition, active->getPosition()})
	{
		EXPECT_FALSE(castAt(position));
		EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore);
		EXPECT_EQ(battle()->obstacles.size(), obstacleCountBefore);
		EXPECT_EQ(battle()->battleGetAllUnits(false).size(), unitCountBefore);
	}
	EXPECT_TRUE(diamondGolems().empty());
}
