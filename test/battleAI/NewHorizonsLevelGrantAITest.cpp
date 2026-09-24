/*
 * NewHorizonsLevelGrantAITest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "../server/battles/HeroCommandFixture.h"
#include "../../AI/Nullkiller2/AIUtility.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/GameSettings.h"
#include "../../lib/spells/CSpell.h"
#include "../../lib/spells/CSpellHandler.h"
#include "../../lib/spells/NewHorizonsMagic.h"
#include "../../lib/spells/NewHorizonsSpellAvailability.h"
#include "../../lib/entities/artifact/CArtifactInstance.h"
#include "../../lib/networkPacks/ArtifactLocation.h"
#include "../spells/NewHorizonsMagicProfileFixture.h"

class NewHorizonsLevelGrantAITest : public HeroCommandFixture
{
protected:
	std::unique_ptr<newHorizonsTest::MagicV1Baseline> baseline;
	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		baseline = std::make_unique<newHorizonsTest::MagicV1Baseline>();
	}
	void TearDown() override
	{
		HeroCommandFixture::TearDown();
		baseline.reset();
	}
	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
	}
	std::vector<SpellID> eligibleHatGrants()
	{
		std::vector<SpellID> result;
		for(const auto spellID : LIBRARY->spellh->getDefaultAllowed())
		{
			const auto * spell = spellID.toSpell();
			if(attackerSideHero->getSpellLevel(spell) == 5 && spell->isCommonHeroSpell() && spell->isCombat()
				&& newHorizonsMagic::spellAllowedBySavedRoster(attackerSideHero->getMagicRules(), spellID)
				&& gameState()->isAllowed(spellID))
				result.push_back(spellID);
		}
		return result;
	}
};

TEST_F(NewHorizonsLevelGrantAITest, SpellbindersHatValuesMissingSpellsAtTheirSavedLevel)
{
	startGame();
	giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
	attackerSideHero->removeAllSpells();
	const ArtifactID hatID(ArtifactID::decode("core:spellbindersHat"));
	for(const auto skill : newHorizonsMagic::schoolSkills(attackerSideHero->getMagicRules()))
		attackerSideHero->setSecSkillLevel(skill, MasteryLevel::NONE, ChangeValueMode::ABSOLUTE);
	const auto granted = eligibleHatGrants();
	ASSERT_FALSE(granted.empty());
	giveArtifact(attackerSideHero, hatID, ArtifactPosition::HEAD);
	const auto * hat = attackerSideHero->getArt(ArtifactPosition::HEAD);
	ASSERT_NE(hat, nullptr);
	ASSERT_TRUE(attackerSideHero->isSpellInscribedForCasting(granted.front()));
	ASSERT_FALSE(attackerSideHero->spellbookContainsSpell(granted.front()));
	const auto equippedScore = NK2AI::getArtifactScoreForHero(attackerSideHero, hat);
	ASSERT_TRUE(gameHandler->moveArtifact(PlayerColor(0),
		ArtifactLocation(attackerSideHero->id, ArtifactPosition::HEAD),
		ArtifactLocation(attackerSideHero->id, ArtifactPosition::BACKPACK_START)));
	EXPECT_FALSE(attackerSideHero->isSpellInscribedForCasting(granted.front()));
	EXPECT_FALSE(attackerSideHero->canCastThisSpell(granted.front().toSpell()));
	const auto backpackHat = attackerSideHero->getArt(ArtifactPosition::BACKPACK_START);
	ASSERT_EQ(backpackHat, hat);
	const auto unequippedScore = NK2AI::getArtifactScoreForHero(attackerSideHero, backpackHat);
	EXPECT_EQ(equippedScore, unequippedScore)
		<< "Spellbinders Hat value is based on permanent learning, not its own temporary grants";
	ASSERT_TRUE(gameHandler->moveArtifact(PlayerColor(0),
		ArtifactLocation(attackerSideHero->id, ArtifactPosition::BACKPACK_START),
		ArtifactLocation(attackerSideHero->id, ArtifactPosition::HEAD)));
	ASSERT_TRUE(attackerSideHero->canCastThisSpell(granted.front().toSpell()));
	EXPECT_FALSE(attackerSideHero->spellbookContainsSpell(granted.front()));
	EXPECT_TRUE(attackerSideHero->isSpellInscribedForCasting(granted.front()));
	EXPECT_TRUE(attackerSideHero->getSpellsInSpellbook().empty());
	const auto unknownScore = NK2AI::getArtifactScoreForHero(attackerSideHero, hat);
	for(const auto spell : granted)
	{
		EXPECT_TRUE(attackerSideHero->isSpellInscribedForCasting(spell));
		EXPECT_FALSE(attackerSideHero->spellbookContainsSpell(spell));
		EXPECT_TRUE(attackerSideHero->canCastThisSpell(spell.toSpell()));
	}
	ASSERT_GE(granted.size(), 2u);
	attackerSideHero->addSpellToSpellbook(granted.front());
	ASSERT_TRUE(gameHandler->moveArtifact(PlayerColor(0),
		ArtifactLocation(attackerSideHero->id, ArtifactPosition::HEAD),
		ArtifactLocation(attackerSideHero->id, ArtifactPosition::BACKPACK_START)));
	EXPECT_TRUE(attackerSideHero->spellbookContainsSpell(granted.front()));
	EXPECT_TRUE(attackerSideHero->isSpellInscribedForCasting(granted.front()));
	EXPECT_FALSE(attackerSideHero->spellbookContainsSpell(granted[1]));
	EXPECT_FALSE(attackerSideHero->isSpellInscribedForCasting(granted[1]));
	for(const auto spell : granted)
		attackerSideHero->addSpellToSpellbook(spell);
	const auto knownScore = NK2AI::getArtifactScoreForHero(attackerSideHero, hat);
	EXPECT_GT(unknownScore, knownScore);
	EXPECT_EQ(knownScore, 0);
	RecordProperty("activeSchools", static_cast<int>(gameState()->getActiveSpellSchools().size()));
	RecordProperty("unknownSpellScore", std::to_string(unknownScore));
	RecordProperty("allKnownSpellScore", std::to_string(knownScore));
}

TEST_F(NewHorizonsLevelGrantAITest, SpellbindersHatHasNoValueWhenEveryEligibleGrantIsMapBanned)
{
	startGame();
	giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
	attackerSideHero->removeAllSpells();
	const ArtifactID hatID(ArtifactID::decode("core:spellbindersHat"));
	giveArtifact(attackerSideHero, hatID, ArtifactPosition::HEAD);
	const auto * hat = attackerSideHero->getArt(ArtifactPosition::HEAD);
	ASSERT_NE(hat, nullptr);
	const auto eligible = eligibleHatGrants();
	ASSERT_FALSE(eligible.empty());
	EXPECT_GT(NK2AI::getArtifactScoreForHero(attackerSideHero, hat), 0);
	for(const auto spell : eligible)
		gameState()->getMap().allowedSpells.erase(spell);
	EXPECT_EQ(NK2AI::getArtifactScoreForHero(attackerSideHero, hat), 0);
}
