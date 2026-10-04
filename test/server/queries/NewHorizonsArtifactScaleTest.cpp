/*
 * NewHorizonsArtifactScaleTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"

#include "../../mock/GameHandlerTestServer.h"
#include "../../mock/TinyH3MBuilder.h"
#include "../../mock/TinyMapGameTest.h"

#include "../../../lib/GameConstants.h"
#include "../../../lib/IGameSettings.h"
#include "../../../lib/bonuses/BonusEnum.h"
#include "../../../lib/entities/artifact/CArtifact.h"
#include "../../../lib/entities/artifact/CArtifactInstance.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/mapObjects/army/CStackInstance.h"
#include "../../../lib/mapping/CMap.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/networkPacks/ArtifactLocation.h"
#include "../../../lib/networkPacks/PacksForClient.h"
#include "../../../lib/pathfinder/TurnInfo.h"
#include "../../../server/CGameHandler.h"

namespace
{
ArtifactID coreArtifact(const char * id)
{
	return ArtifactID(ArtifactID::decode(id));
}

CreatureID coreCreature(const char * id)
{
	return CreatureID(CreatureID::decode(id));
}

ArtifactPosition firstHeroSlot(ArtifactID id)
{
	const auto * artifact = id.toArtifact();
	if(!artifact)
		return ArtifactPosition::PRE_FIRST;
	const auto & slots = artifact->getPossibleSlots().at(ArtBearer::HERO);
	return slots.empty() ? ArtifactPosition::PRE_FIRST : slots.front();
}

class NewHorizonsArtifactScaleTest : public TinyMapGameTest
{
protected:
	void mapLoaded(CMap * loaded) override
	{
		TinyMapGameTest::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsHeroes")));
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_CAPABILITIES,
			JsonNode(JsonPath::builtin("config/newHorizonsCapabilities")));
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
	}

	void SetUp() override
	{
		TinyMapGameTest::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons module";
	}
};
}

TEST_F(NewHorizonsArtifactScaleTest, EquipmentScalesHeroAttributesAndClampsSpellPointsOnRemoval)
{
	const auto dragon = coreCreature("core:redDragon");
	const auto centaurAxe = coreArtifact("core:centaurAxe");
	const auto titansGladius = coreArtifact("core:titansGladius");
	const auto thunderHelmet = coreArtifact("core:thunderHelmet");
	const auto bootsOfSpeed = coreArtifact("core:bootsOfSpeed");
	const auto equestriansGloves = coreArtifact("core:equestriansGloves");
	const auto oceanNecklace = coreArtifact("core:necklaceOfOceanGuidance");
	const auto seaCaptainsHat = coreArtifact("core:seaCaptainsHat");
	const auto vialOfDragonBlood = coreArtifact("core:vialOfDragonBlood");
	for(const auto artifact : {centaurAxe, titansGladius, thunderHelmet, bootsOfSpeed,
		equestriansGloves, oceanNecklace, seaCaptainsHat, vialOfDragonBlood})
		ASSERT_NE(firstHeroSlot(artifact), ArtifactPosition::PRE_FIRST);

	TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
	builder.size(36, false).name("NewHorizonsArtifactScale")
		.playerActive(PlayerColor(0))
		.playerActive(PlayerColor(1))
		.hero({5, 5, 0}, HeroTypeID(0), PlayerColor(0)).heroPrimary(10, 20, 10, 10)
			.heroEquipped({{firstHeroSlot(centaurAxe), centaurAxe}})
		.hero({7, 5, 0}, HeroTypeID(1), PlayerColor(0)).heroPrimary(10, 20, 10, 10)
			.heroEquipped({{firstHeroSlot(seaCaptainsHat), seaCaptainsHat}})
		.hero({9, 5, 0}, HeroTypeID(2), PlayerColor(0)).heroPrimary(10, 20, 10, 10)
			.heroEquipped({{firstHeroSlot(titansGladius), titansGladius}})
		.hero({11, 5, 0}, HeroTypeID(3), PlayerColor(0)).heroPrimary(10, 20, 10, 10)
			.heroEquipped({{firstHeroSlot(thunderHelmet), thunderHelmet}})
		.hero({13, 5, 0}, HeroTypeID(4), PlayerColor(0)).heroPrimary(10, 20, 10, 10)
			.heroEquipped({
				{firstHeroSlot(bootsOfSpeed), bootsOfSpeed},
				{firstHeroSlot(equestriansGloves), equestriansGloves},
				{firstHeroSlot(oceanNecklace), oceanNecklace}})
		.hero({15, 5, 0}, HeroTypeID(5), PlayerColor(0)).heroPrimary(10, 20, 10, 10)
			.heroEquipped({{firstHeroSlot(vialOfDragonBlood), vialOfDragonBlood}})
			.heroGarrison({{dragon, 1}})
		.hero({17, 5, 0}, HeroTypeID(6), PlayerColor(0)).heroPrimary(10, 20, 10, 10)
			.heroGarrison({{dragon, 1}});
	startWithMap(std::move(builder));

	auto * axeHero = findHeroAt({5, 5, 0});
	auto * baseline = findHeroAt({7, 5, 0});
	auto * gladiusHero = findHeroAt({9, 5, 0});
	auto * knowledgeHero = findHeroAt({11, 5, 0});
	auto * movementHero = findHeroAt({13, 5, 0});
	auto * vialHero = findHeroAt({15, 5, 0});
	auto * dragonBaseline = findHeroAt({17, 5, 0});
	ASSERT_NE(baseline, nullptr);
	ASSERT_NE(axeHero, nullptr);
	ASSERT_NE(gladiusHero, nullptr);
	ASSERT_NE(knowledgeHero, nullptr);
	ASSERT_NE(movementHero, nullptr);
	ASSERT_NE(vialHero, nullptr);
	ASSERT_NE(dragonBaseline, nullptr);

	EXPECT_EQ(knowledgeHero->getPrimSkillLevel(PrimarySkill::KNOWLEDGE), 60);
	EXPECT_EQ(knowledgeHero->getPrimSkillLevel(PrimarySkill::SPELL_POWER), 0);

	GameHandlerTestServer server(gameState(), PlayerColor(0));
	CGameHandler gameHandler(server, gameState());
	const auto removeEquippedArtifact = [&gameHandler](CGHeroInstance * hero, ArtifactID id)
	{
		ASSERT_NE(hero, nullptr);
		const auto slot = hero->getArtPos(id, true);
		ASSERT_NE(slot, ArtifactPosition::PRE_FIRST);
		const auto * equipped = hero->getArt(slot);
		ASSERT_NE(equipped, nullptr);
		ASSERT_EQ(equipped->getTypeId(), id);
		const auto * type = id.toArtifact();
		ASSERT_NE(type, nullptr);
		ASSERT_TRUE(vstd::contains(type->getPossibleSlots().at(ArtBearer::HERO), slot));
		gameHandler.removeArtifact(ArtifactLocation(hero->id, slot));
	};
	const int axeAttackWithArtifact = axeHero->getPrimSkillLevel(PrimarySkill::ATTACK);
	removeEquippedArtifact(axeHero, centaurAxe);
	EXPECT_EQ(axeAttackWithArtifact - axeHero->getPrimSkillLevel(PrimarySkill::ATTACK), 10);
	const int gladiusAttackWithArtifact = gladiusHero->getPrimSkillLevel(PrimarySkill::ATTACK);
	const int gladiusDefenseWithArtifact = gladiusHero->getPrimSkillLevel(PrimarySkill::DEFENSE);
	removeEquippedArtifact(gladiusHero, titansGladius);
	EXPECT_EQ(gladiusAttackWithArtifact - gladiusHero->getPrimSkillLevel(PrimarySkill::ATTACK), 60);
	EXPECT_EQ(gladiusDefenseWithArtifact - gladiusHero->getPrimSkillLevel(PrimarySkill::DEFENSE), -15);

	const auto manaCapacityWithHelmet = knowledgeHero->manaLimit();
	ASSERT_EQ(manaCapacityWithHelmet, 60);
	SetMana fillMana(knowledgeHero->id, SetMana::Operation::SET_NORMAL, manaCapacityWithHelmet);
	gameState()->apply(fillMana);
	ASSERT_EQ(knowledgeHero->getNormalSpellPoints(), manaCapacityWithHelmet);

	removeEquippedArtifact(knowledgeHero, thunderHelmet);
	EXPECT_EQ(knowledgeHero->getPrimSkillLevel(PrimarySkill::KNOWLEDGE), 10);
	EXPECT_EQ(knowledgeHero->getPrimSkillLevel(PrimarySkill::SPELL_POWER), 10);
	EXPECT_EQ(knowledgeHero->manaLimit(), 10);
	EXPECT_EQ(knowledgeHero->getNormalSpellPoints(), 10);

	const auto currentSeaTurnWithHat = baseline->getTurnInfo(0);
	const auto futureSeaTurnWithHat = baseline->getTurnInfo(1);
	ASSERT_NE(currentSeaTurnWithHat, nullptr);
	ASSERT_NE(futureSeaTurnWithHat, nullptr);
	const int currentSeaWithHat = currentSeaTurnWithHat->getMovePointsLimitWater();
	const int futureSeaWithHat = futureSeaTurnWithHat->getMovePointsLimitWater();
	removeEquippedArtifact(baseline, seaCaptainsHat);
	const auto currentSeaTurnWithoutHat = baseline->getTurnInfo(0);
	const auto futureSeaTurnWithoutHat = baseline->getTurnInfo(1);
	ASSERT_NE(currentSeaTurnWithoutHat, nullptr);
	ASSERT_NE(futureSeaTurnWithoutHat, nullptr);
	EXPECT_EQ(currentSeaWithHat - currentSeaTurnWithoutHat->getMovePointsLimitWater(), 50);
	EXPECT_EQ(futureSeaWithHat - futureSeaTurnWithoutHat->getMovePointsLimitWater(), 50);

	const auto currentTurnWithArtifacts = movementHero->getTurnInfo(0);
	const auto futureTurnWithArtifacts = movementHero->getTurnInfo(1);
	ASSERT_NE(currentTurnWithArtifacts, nullptr);
	ASSERT_NE(futureTurnWithArtifacts, nullptr);
	const int currentLandWithArtifacts = currentTurnWithArtifacts->getMovePointsLimitLand();
	const int futureLandWithArtifacts = futureTurnWithArtifacts->getMovePointsLimitLand();
	const int currentWaterWithArtifacts = currentTurnWithArtifacts->getMovePointsLimitWater();
	const int futureWaterWithArtifacts = futureTurnWithArtifacts->getMovePointsLimitWater();
	removeEquippedArtifact(movementHero, bootsOfSpeed);
	removeEquippedArtifact(movementHero, equestriansGloves);
	removeEquippedArtifact(movementHero, oceanNecklace);
	const auto currentTurnWithoutArtifacts = movementHero->getTurnInfo(0);
	const auto futureTurnWithoutArtifacts = movementHero->getTurnInfo(1);
	ASSERT_NE(currentTurnWithoutArtifacts, nullptr);
	ASSERT_NE(futureTurnWithoutArtifacts, nullptr);
	EXPECT_EQ(currentLandWithArtifacts - currentTurnWithoutArtifacts->getMovePointsLimitLand(), 90);
	EXPECT_EQ(futureLandWithArtifacts - futureTurnWithoutArtifacts->getMovePointsLimitLand(), 90);
	EXPECT_EQ(currentWaterWithArtifacts - currentTurnWithoutArtifacts->getMovePointsLimitWater(), 100);
	EXPECT_EQ(futureWaterWithArtifacts - futureTurnWithoutArtifacts->getMovePointsLimitWater(), 100);

	// Vial of Dragon Blood is intentionally not in the overlay: its limiter
	// targets dragons' creature ratings, never the hero's Primary Attributes.
	const int vialHeroAttack = vialHero->getPrimSkillLevel(PrimarySkill::ATTACK);
	const int vialHeroDefense = vialHero->getPrimSkillLevel(PrimarySkill::DEFENSE);
	auto * vialDragon = vialHero->getStackPtr(SlotID(0));
	auto * controlDragon = dragonBaseline->getStackPtr(SlotID(0));
	ASSERT_NE(vialDragon, nullptr);
	ASSERT_NE(controlDragon, nullptr);
	EXPECT_EQ(vialDragon->valOfBonuses(BonusType::PRIMARY_SKILL, BonusSubtypeID(PrimarySkill::ATTACK))
		- controlDragon->valOfBonuses(BonusType::PRIMARY_SKILL, BonusSubtypeID(PrimarySkill::ATTACK)), 5);
	EXPECT_EQ(vialDragon->valOfBonuses(BonusType::PRIMARY_SKILL, BonusSubtypeID(PrimarySkill::DEFENSE))
		- controlDragon->valOfBonuses(BonusType::PRIMARY_SKILL, BonusSubtypeID(PrimarySkill::DEFENSE)), 5);
	removeEquippedArtifact(vialHero, vialOfDragonBlood);
	EXPECT_EQ(vialHero->getPrimSkillLevel(PrimarySkill::ATTACK), vialHeroAttack);
	EXPECT_EQ(vialHero->getPrimSkillLevel(PrimarySkill::DEFENSE), vialHeroDefense);
	EXPECT_EQ(vialDragon->valOfBonuses(BonusType::PRIMARY_SKILL, BonusSubtypeID(PrimarySkill::ATTACK))
		- controlDragon->valOfBonuses(BonusType::PRIMARY_SKILL, BonusSubtypeID(PrimarySkill::ATTACK)), 0);
	EXPECT_EQ(vialDragon->valOfBonuses(BonusType::PRIMARY_SKILL, BonusSubtypeID(PrimarySkill::DEFENSE))
		- controlDragon->valOfBonuses(BonusType::PRIMARY_SKILL, BonusSubtypeID(PrimarySkill::DEFENSE)), 0);
}
