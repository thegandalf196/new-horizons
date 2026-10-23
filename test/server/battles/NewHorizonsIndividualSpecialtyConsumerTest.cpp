/*
 * NewHorizonsIndividualSpecialtyConsumerTest.cpp, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include "../../../lib/entities/hero/CHero.h"
#include "../../../lib/entities/hero/NewHorizonsHeroRules.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/ISpellMechanics.h"
#include "../../../lib/spells/NewHorizonsMagic.h"
#include "../../../lib/gameState/CGameState.h"
#include "../../../lib/json/JsonUtils.h"
#include "../../../server/queries/QueriesProcessor.h"
#include "../../../server/queries/BattleQueries.h"

namespace
{
class IndividualSpecialtyResultServer : public RecordingGameServer
{
public:
	std::vector<BattleResultsApplied> results;
	void applyPack(CPackForClient & pack) override
	{
		if(const auto * result = dynamic_cast<const BattleResultsApplied *>(&pack))
			results.push_back(*result);
		RecordingGameServer::applyPack(pack);
	}
};

class NewHorizonsIndividualSpecialtyConsumerTest : public HeroCommandFixture
{
protected:
	bool capturedPolicy = true;
	std::unique_ptr<IndividualSpecialtyResultServer> resultServer;
	void TearDown() override
	{
		gameHandler.reset();
		resultServer.reset();
		HeroCommandFixture::TearDown();
	}
	void mapLoaded(CMap * map) override
	{
		HeroCommandFixture::mapLoaded(map);
		JsonNode rules(JsonPath::builtin("config/newHorizonsHeroes"));
		rules.setOverrideFlag(true);
		rules.Struct().erase("individualSpecialties");
		if(capturedPolicy)
		{
			auto & row = rules["individualSpecialties"];
			row["version"].Integer() = 1;
			row["graveReaper"]["percentagePointsPerLevel"].Integer() = 1;
			row["graveReaper"]["maximumPercentagePoints"].Integer() = 20;
			row["warlocksFormula"]["percentPerLevel"].Integer() = 5;
			row["warlocksFormula"]["maximumPercent"].Integer() = 100;
		}
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, rules);
	}

	void prepare(const char * name, int level)
	{
		startGame();
		attackerSideHero->setHeroType(HeroTypeID(HeroTypeID::decode(name)));
		attackerSideHero->level = level;
	}

	void setRank(const char * name, int rank)
	{
		attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode(name)),
			rank, ChangeValueMode::ABSOLUTE);
	}

	void prepareCast()
	{
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 200, ChangeValueMode::ABSOLUTE);
		setTestSpellPointTotal(attackerSideHero, 100);
		startBattle();
		beginCombat();
	}
};
}

TEST_F(NewHorizonsIndividualSpecialtyConsumerTest, IsraRequiresOwnCapturedIdentityAndLearnedNecromancy)
{
	ASSERT_NO_FATAL_FAILURE(prepare("core:isra", 1));
	setRank("new-horizons:necromancy", MasteryLevel::NONE);
	EXPECT_EQ(newHorizonsHeroes::graveReaperPercentagePoints(attackerSideHero), 0);
	setRank("new-horizons:necromancy", MasteryLevel::BASIC);
	EXPECT_EQ(newHorizonsHeroes::graveReaperPercentagePoints(attackerSideHero), 1);
	const auto presentation = attackerSideHero->getSpecialtyPresentation();
	EXPECT_EQ(presentation.name, "Grave Reaper");
	EXPECT_EQ(presentation.artwork, std::optional<std::string>("newHorizons/heroSpecialties/IsraGraveReaper"));
	attackerSideHero->level = 30;
	EXPECT_EQ(newHorizonsHeroes::graveReaperPercentagePoints(attackerSideHero), 20);
	attackerSideHero->setHeroType(HeroTypeID(HeroTypeID::decode("core:vidomina")));
	EXPECT_EQ(newHorizonsHeroes::graveReaperPercentagePoints(attackerSideHero), 0);
}

TEST_F(NewHorizonsIndividualSpecialtyConsumerTest, MissingCaptureStaysAbsentAcrossWorldSaveAndLoad)
{
	capturedPolicy = false;
	ASSERT_NO_FATAL_FAILURE(prepare("core:isra", 20));
	setRank("new-horizons:necromancy", MasteryLevel::BASIC);
	EXPECT_EQ(newHorizonsHeroes::graveReaperPercentagePoints(attackerSideHero), 0);
	const auto heroID = attackerSideHero->id;
	CGameState restored;
	restored.preInit(LIBRARY);
	restored.loadFromMemory(gameState()->saveToMemory());
	const auto * loaded = restored.getHero(heroID);
	ASSERT_NE(loaded, nullptr);
	EXPECT_FALSE(newHorizonsHeroes::individualSpecialtyRules(loaded->getPrimaryGrowthRules()));
	EXPECT_EQ(newHorizonsHeroes::graveReaperPercentagePoints(loaded), 0);
}

TEST_F(NewHorizonsIndividualSpecialtyConsumerTest, CapturedCoefficientsAndIdentitySurviveWorldSaveAndLoad)
{
	ASSERT_NO_FATAL_FAILURE(prepare("core:isra", 10));
	setRank("new-horizons:necromancy", MasteryLevel::BASIC);
	const auto heroID = attackerSideHero->id;
	CGameState restored;
	restored.preInit(LIBRARY);
	restored.loadFromMemory(gameState()->saveToMemory());
	const auto * loaded = restored.getHero(heroID);
	ASSERT_NE(loaded, nullptr);
	EXPECT_EQ(loaded->getPrimaryGrowthRules(), attackerSideHero->getPrimaryGrowthRules());
	EXPECT_EQ(newHorizonsHeroes::graveReaperPercentagePoints(loaded), 10);
}

TEST_F(NewHorizonsIndividualSpecialtyConsumerTest, MalekithChangesOnlyImmediateDamageCoefficient)
{
	ASSERT_NO_FATAL_FAILURE(prepare("core:malekith", 1));
	setRank("new-horizons:spellcraft", MasteryLevel::BASIC);
	const auto & rules = attackerSideHero->getMagicRules();
	for(const auto identity : {"core:magicArrow", "core:chainLightning", "core:armageddon",
		"new-horizons:vengefulVines", "core:implosion", "core:earthquake", "new-horizons:naturesWrath"})
	{
		const SpellID spell(SpellID::decode(identity));
		const int school = newHorizonsMagic::spellPowerCoefficientPercent(rules, attackerSideHero, spell);
		EXPECT_EQ(newHorizonsMagic::directDamageSpellPowerCoefficientBasisPoints(rules, attackerSideHero, spell),
			school * 11050 / 100) << identity;
		EXPECT_EQ(newHorizonsMagic::spellPowerCoefficientBasisPoints(rules, attackerSideHero, spell),
			school * 110) << identity;
	}
	for(const auto identity : {"core:fireWall", "core:landMine", "core:cure", "core:haste",
		"new-horizons:plague", "new-horizons:hexOfPain", "new-horizons:phantomArmy"})
	{
		const SpellID spell(SpellID::decode(identity));
		EXPECT_EQ(newHorizonsMagic::directDamageSpellPowerCoefficientBasisPoints(rules, attackerSideHero, spell),
			newHorizonsMagic::spellPowerCoefficientBasisPoints(rules, attackerSideHero, spell)) << identity;
	}
}

TEST_F(NewHorizonsIndividualSpecialtyConsumerTest, MagicArrowKeepsItsFixedBaseAndPassiveDamageUnchanged)
{
	ASSERT_NO_FATAL_FAILURE(prepare("core:malekith", 1));
	setRank("new-horizons:spellcraft", MasteryLevel::BASIC);
	ASSERT_NO_FATAL_FAILURE(prepareCast());
	const auto * spell = SpellID(SpellID::MAGIC_ARROW).toSpell();
	spells::BattleCast direct(battle(), attackerSideHero, spells::Mode::HERO, spell);
	const auto mechanics = spell->battleMechanics(&direct);
	EXPECT_EQ(mechanics->getSpellPowerCoefficientBasisPoints(), 11000);
	EXPECT_EQ(mechanics->getDirectDamageSpellPowerCoefficientBasisPoints(), 11050);
	EXPECT_EQ(mechanics->getEffectValue(), 20 + 442);
	EXPECT_EQ(spell->calculateDamage(attackerSideHero), mechanics->getEffectValue());
	spells::BattleCast passive(battle(), attackerSideHero, spells::Mode::PASSIVE, spell);
	const auto indirect = spell->battleMechanics(&passive);
	EXPECT_EQ(indirect->getDirectDamageSpellPowerCoefficientBasisPoints(), 11000);
	EXPECT_EQ(indirect->getEffectValue(), 20 + 440);
}

TEST_F(NewHorizonsIndividualSpecialtyConsumerTest, NoSpellcraftOrOtherHeroHasNoFormulaBonus)
{
	ASSERT_NO_FATAL_FAILURE(prepare("core:malekith", 20));
	setRank("new-horizons:spellcraft", MasteryLevel::NONE);
	const auto & rules = attackerSideHero->getMagicRules();
	const SpellID spell(SpellID::MAGIC_ARROW);
	EXPECT_EQ(newHorizonsMagic::directDamageSpellPowerCoefficientBasisPoints(rules, attackerSideHero, spell),
		newHorizonsMagic::spellPowerCoefficientBasisPoints(rules, attackerSideHero, spell));
	setRank("new-horizons:spellcraft", MasteryLevel::EXPERT);
	EXPECT_EQ(newHorizonsMagic::directDamageSpellPowerCoefficientBasisPoints(rules, attackerSideHero, spell),
		newHorizonsMagic::spellPowerCoefficientPercent(rules, attackerSideHero, spell) * 160);
	attackerSideHero->setHeroType(HeroTypeID(HeroTypeID::decode("core:jaegar")));
	EXPECT_EQ(newHorizonsMagic::directDamageSpellPowerCoefficientBasisPoints(rules, attackerSideHero, spell),
		newHorizonsMagic::spellPowerCoefficientBasisPoints(rules, attackerSideHero, spell));
}


TEST_F(NewHorizonsIndividualSpecialtyConsumerTest, IsraAuthoritativeBattleResultAppliesEnhancedRaisingAutomatically)
{
	ASSERT_NO_FATAL_FAILURE(prepare("core:isra", 10));
	setRank("new-horizons:necromancy", MasteryLevel::BASIC);
	const auto skeleton = creatureByName("core:skeleton");
	attackerSideHero->clearSlots();
	defenderSideHero->clearSlots();
	ASSERT_TRUE(attackerSideHero->setCreature(SlotID(0), skeleton, 1));
	ASSERT_TRUE(defenderSideHero->setCreature(SlotID(0), creatureByName("core:pikeman"), 15));
	gameHandler.reset();
	resultServer = std::make_unique<IndividualSpecialtyResultServer>();
	resultServer->gameState = gameState();
	gameHandler = std::make_shared<CGameHandler>(*resultServer, gameState());
	gameHandler->onAdvInterfaceReady(PlayerColor(0));
	gameHandler->onAdvInterfaceReady(PlayerColor(1));
	gameHandler->battles->startBattle(attackerSideHero, defenderSideHero);
	ASSERT_NE(gameState()->getBattle(PlayerColor(0)), nullptr);
	gameHandler->battles->cheatBattleVictory(PlayerColor(0));
	for(const auto player : {PlayerColor(0), PlayerColor(1)})
	{
		const auto query = gameHandler->queries->topQuery(player);
		if(query && query->getType() == QueryType::BattleDialog)
		{
			ASSERT_TRUE(gameHandler->queryReply(query->queryID, 0, player));
		}
	}
	EXPECT_EQ(gameHandler->queries->topQuery(PlayerColor(0)), nullptr);
	ASSERT_EQ(resultServer->results.size(), 1);
	const auto & raising = resultServer->results.back().necromancy;
	EXPECT_EQ(raising.percentage, 20);
	EXPECT_EQ(raising.eligibleCasualties, 15);
	EXPECT_EQ(raising.skeletonsOffered, 3);
	EXPECT_EQ(raising.skeletonsRaised, 3);
	EXPECT_TRUE(raising.applied);
	EXPECT_EQ(attackerSideHero->getStackCount(SlotID(0)), 4);
}


TEST_F(NewHorizonsIndividualSpecialtyConsumerTest, PresentationUsesCurrentAndCapturedPolicyWithoutReplacingOldIdentity)
{
	ASSERT_NO_FATAL_FAILURE(prepare("core:malekith", 1));
	const auto * heroType = attackerSideHero->getHeroType();
	const auto & development = attackerSideHero->getPrimaryGrowthRules();
	const auto & magic = attackerSideHero->getMagicRules();
	const auto preview = newHorizonsHeroes::resolveSpecialtyPresentation(*heroType, development, magic, std::nullopt);
	const auto live = newHorizonsHeroes::resolveSpecialtyPresentation(*heroType, development, magic, std::nullopt, attackerSideHero);
	EXPECT_EQ(preview.name, "Warlock's Formula");
	EXPECT_EQ(live.name, preview.name);
	EXPECT_EQ(preview.artwork, std::optional<std::string>("newHorizons/heroSpecialties/MalekithWarlocksFormula"));
	EXPECT_EQ(live.artwork, preview.artwork);
	EXPECT_NE(live.description.find("Current increase to the Spellcraft bonus: +5%"), std::string::npos);
	auto old = development;
	old.Struct().erase("individualSpecialties");
	const auto historical = newHorizonsHeroes::resolveSpecialtyPresentation(*heroType, old, magic, std::nullopt);
	EXPECT_NE(historical.name, "Warlock's Formula");
	EXPECT_FALSE(historical.artwork);
}


TEST_F(NewHorizonsIndividualSpecialtyConsumerTest, PresentationUsesValidOlderCapturedMagicWithoutSpellcraftEfficiency)
{
	ASSERT_NO_FATAL_FAILURE(prepare("core:malekith", 20));
	setRank("new-horizons:spellcraft", MasteryLevel::EXPERT);
	const auto * heroType = attackerSideHero->getHeroType();
	const auto & development = attackerSideHero->getPrimaryGrowthRules();
	const auto & currentMagic = attackerSideHero->getMagicRules();
	const auto current = newHorizonsHeroes::resolveSpecialtyPresentation(
		*heroType, development, currentMagic, std::nullopt, attackerSideHero);
	EXPECT_NE(current.description.find("+30% becomes +60%"), std::string::npos);

	auto capturedMagic = currentMagic;
	capturedMagic.Struct().erase("spellcraftEfficiencyPercent");
	ASSERT_NO_THROW(newHorizonsMagic::validateRules(capturedMagic));
	ASSERT_TRUE(JsonUtils::validate(capturedMagic, "vcmi:newHorizonsMagicV3", "older captured Spellcraft omission"));
	EXPECT_EQ(newHorizonsMagic::spellcraftEfficiencyPercent(capturedMagic, MasteryLevel::EXPERT), 100);
	const auto historical = newHorizonsHeroes::resolveSpecialtyPresentation(
		*heroType, development, capturedMagic, std::nullopt);
	EXPECT_EQ(historical.name, "Warlock's Formula");
	EXPECT_NE(historical.description.find("Spellcraft provides no damage bonus under this game's rules."), std::string::npos);
	EXPECT_EQ(historical.description.find("+30% becomes +60%"), std::string::npos);
	const SpellID spell(SpellID::MAGIC_ARROW);
	EXPECT_EQ(newHorizonsMagic::directDamageSpellPowerCoefficientBasisPoints(capturedMagic, attackerSideHero, spell),
		newHorizonsMagic::spellPowerCoefficientBasisPoints(capturedMagic, attackerSideHero, spell));
}
