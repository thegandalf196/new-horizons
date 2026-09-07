/*
 * NewHorizonsMagicStateTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/modding/ActiveModsInSaveList.h"
#include "../../../lib/modding/ModDescription.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/NewHorizonsMagic.h"
#include "../../../lib/constants/StringConstants.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../lib/battle/CObstacleInstance.h"
#include "../../../lib/bonuses/BonusParameters.h"
#include "../../../lib/bonuses/Limiters.h"
#include "../../../lib/bonuses/Propagators.h"
#include "../../../lib/bonuses/Updaters.h"

class NewHorizonsMagicStateTest : public HeroCommandFixture
{
protected:
	bool useMagic = true;

	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires separate native curated testModSettings preset; baseline profile remains legacy";
	}

	void mapLoaded(CMap * map) override
	{
		HeroCommandFixture::mapLoaded(map);
		JsonNode rules;
		if(useMagic)
		{
			const JsonNode file(JsonPath::builtin("config/newHorizonsMagic"));
			rules = file;
			newHorizonsMagic::validateRules(rules);
		}
		map->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, rules);
	}

	void startSkilledHero()
	{
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).playerActive(PlayerColor(0))
			.hero({5, 5, 0}, HeroTypeID(0), PlayerColor(0))
			.heroExperience(0).heroPrimary(2, 2, 3, 10)
			.heroSecondarySkills({{SecondarySkill::AIR_MAGIC, 1}})
			.heroGarrison({{CreatureID(0), 10}})
			.heroEquipped({{ArtifactPosition::SPELLBOOK, ArtifactID::SPELLBOOK}})
			.heroSpells({SpellID::HASTE});
		startWithMap(builder);
	}
};

TEST_F(NewHorizonsMagicStateTest, LegacyHeaderDoesNotRequireDisablingCuratedModule)
{
	useMagic = false;
	useCommands = false;
	startSkilledHero();
	const auto legacyWorld = gameState();
	ASSERT_FALSE(newHorizonsHeroes::usesRules(legacyWorld->getHeroDevelopmentRules()));
	ASSERT_FALSE(findHeroByOwner(PlayerColor(0))->getPrimaryGrowthView());
	const auto legacyBytes = legacyWorld->saveToMemory();

	// Exact ActiveModsInSaveList framing of a pre-NH save: all its existing
	// gameplay mods, but not the subsequently installed curated module.
	std::vector<TModID> oldMods;
	for(const auto & mod : LIBRARY->modh->getActiveMods())
		if(mod != GameConstants::NEW_HORIZONS_MOD_SCOPE && LIBRARY->modh->getModInfo(mod).affectsGameplay())
			oldMods.push_back(mod);
	CMemorySerializer header;
	header.oser & oldMods;
	for(const auto & mod : oldMods)
	{
		auto info = LIBRARY->modh->getModInfo(mod).getVerificationInfo();
		header.oser & info;
	}
	ActiveModsInSaveList incoming;
	ASSERT_NO_THROW(header.iser & incoming);
	EXPECT_TRUE(vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE));
	auto restored = std::make_shared<CGameState>();
	restored->preInit(LIBRARY);
	restored->loadFromMemory(legacyBytes);
	EXPECT_EQ(restored->getMagicRules(), legacyWorld->getMagicRules());
	EXPECT_EQ(restored->getActiveSpellSchools().size(), 4u);
	const auto * oldHero = restored->getHero(findHeroByOwner(PlayerColor(0))->id);
	ASSERT_NE(oldHero, nullptr);
	EXPECT_EQ(oldHero->getSecSkillLevel(SecondarySkill::AIR_MAGIC), 1);

	TearDown();
	SetUp();
	useMagic = true;
	useCommands = true;
	startSkilledHero();
	EXPECT_EQ(gameState()->getActiveSpellSchools().size(), 6u);
	EXPECT_EQ(legacyWorld->getActiveSpellSchools().size(), 4u);
}

TEST_F(NewHorizonsMagicStateTest, ActualSchoolRankCostAndServerCastUseSavedClassification)
{
	prepareCommands(true);
	const auto sorcery = SpellSchool::fromSerializationKey("new-horizons:sorcery");
	const SecondarySkill skill(SecondarySkill::decode("new-horizons:sorceryMagic"));
	attackerSideHero->setSecSkillLevel(skill, 3, ChangeValueMode::ABSOLUTE);
	const auto * haste = SpellID(SpellID::HASTE).toSpell();
	const auto originalSchools = haste->schools;
	const auto originalLevel = haste->getLevel();
	SpellSchool best;
	ASSERT_EQ(attackerSideHero->getSpellSchoolLevel(haste, &best), 3);
	EXPECT_EQ(best, sorcery);
	EXPECT_EQ(gameState()->getActiveSpellSchools().size(), 6u);
	EXPECT_EQ(gameState()->getActiveSpellSchools(), battle()->battleGetActiveSpellSchools());
	EXPECT_EQ(attackerSideHero->getSpellSchools(haste), battle()->battleGetSpellSchools(haste->getId()));
	EXPECT_EQ(attackerSideHero->getSpellLevel(haste), battle()->battleGetSpellLevel(haste->getId()));
	const auto cost = attackerSideHero->getSpellCost(haste);
	ASSERT_EQ(cost, newHorizonsMagic::spellCost(gameState()->getMagicRules(), haste->getId(), 3));
	const auto speed = battle()->battleActiveUnit()->getMovementRange();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), heroAction(0)));
	EXPECT_EQ(attackerSideHero->mana, 100 - cost);
	EXPECT_GT(battle()->battleActiveUnit()->getMovementRange(), speed);
	EXPECT_FALSE(battle()->battleCanUseHeroCommand(BattleSide::ATTACKER, HeroCommand::CHARGE));
	EXPECT_EQ(haste->schools, originalSchools);
	EXPECT_EQ(haste->getLevel(), originalLevel);
}

TEST_F(NewHorizonsMagicStateTest, StartingRanksConvertAndNewSkillsCanBeOffered)
{
	startSkilledHero();
	const auto * hero = findHeroByOwner(PlayerColor(0));
	ASSERT_NE(hero, nullptr);
	const SecondarySkill sorcery(SecondarySkill::decode("new-horizons:sorceryMagic"));
	const SecondarySkill light(SecondarySkill::decode("new-horizons:lightMagic"));
	EXPECT_EQ(hero->getSecSkillLevel(SecondarySkill::AIR_MAGIC), 0);
	EXPECT_EQ(hero->getSecSkillLevel(sorcery), 1);
	EXPECT_FALSE(hero->canLearnSkill(SecondarySkill::AIR_MAGIC));
	EXPECT_TRUE(hero->canLearnSkill(light));
	EXPECT_EQ(hero->getSpellSchoolLevel(SpellID(SpellID::HASTE).toSpell()), 1);
}

TEST_F(NewHorizonsMagicStateTest, LegacyWorldKeepsOriginalRankAndCannotOfferNewSkills)
{
	useMagic = false;
	useCommands = false;
	startSkilledHero();
	const auto * hero = findHeroByOwner(PlayerColor(0));
	ASSERT_NE(hero, nullptr);
	EXPECT_FALSE(hero->getPrimaryGrowthView());
	EXPECT_EQ(hero->getSecSkillLevel(SecondarySkill::AIR_MAGIC), 1);
	EXPECT_EQ(hero->getSpellSchoolLevel(SpellID(SpellID::HASTE).toSpell()), 1);
	EXPECT_EQ(gameState()->getActiveSpellSchools().size(), 4u);
	EXPECT_FALSE(hero->canLearnSkill(SecondarySkill(SecondarySkill::decode("new-horizons:lightMagic"))));
}

TEST_F(NewHorizonsMagicStateTest, ActualGameAndBattlePacketRetainSavedRules)
{
	startGame();
	giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
	attackerSideHero->addSpellToSpellbook(SpellID::HASTE);
	attackerSideHero->mana = 100;
	attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:sorceryMagic")), 3, ChangeValueMode::ABSOLUTE);
	const auto rules = gameState()->getMagicRules();
	ASSERT_EQ(rules["rulesetVersion"].Integer(), 1);
	const auto bytes = gameState()->saveToMemory();
	auto restored = std::make_shared<CGameState>();
	restored->preInit(LIBRARY);
	restored->loadFromMemory(bytes);
	EXPECT_EQ(restored->getMagicRules(), rules);
	EXPECT_EQ(restored->getActiveSpellSchools(), gameState()->getActiveSpellSchools());

	startBattle();
	beginCombat();
	BattleStart outgoing;
	outgoing.battleID = BattleID(0);
	outgoing.info = CMemorySerializer::deepCopy(*battle(), restored.get());
	CMemorySerializer wire;
	wire.oser & outgoing;
	wire.iser.cb = restored.get();
	BattleStart incoming;
	wire.iser & incoming;
	ASSERT_NE(incoming.info, nullptr);
	EXPECT_EQ(incoming.info->getMagicRules(), rules);
	EXPECT_EQ(incoming.info->battleGetActiveSpellSchools(), gameState()->getActiveSpellSchools());
	RecordingGameServer receiver;
	receiver.gameState = restored;
	CGameHandler handler(receiver, restored);
	handler.sendAndApply(incoming);
	ASSERT_NE(restored->getBattle(BattleID(0)), nullptr);
	const auto * restoredBattle = restored->getBattle(BattleID(0));
	EXPECT_EQ(restoredBattle->getMagicRules(), rules);
	const auto * hero = restored->getHero(attackerSideHero->id);
	ASSERT_NE(hero, nullptr);
	const auto * spell = SpellID(SpellID::HASTE).toSpell();
	const auto cost = hero->getSpellCost(spell);
	EXPECT_EQ(hero->getSpellSchoolLevel(spell), 3);
	BattleAction action;
	action.actionType = EActionType::HERO_SPELL;
	action.side = BattleSide::ATTACKER;
	action.spell = SpellID::HASTE;
	action.aimToUnit(restoredBattle->battleActiveUnit());
	ASSERT_TRUE(handler.battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_EQ(hero->mana, 100 - cost);
	EXPECT_EQ(attackerSideHero->mana, 100);
}
