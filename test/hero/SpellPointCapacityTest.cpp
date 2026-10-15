/*
 * SpellPointCapacityTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"

#include "../server/battles/BattleTestFixture.h"
#include "../server/battles/BattleStartSnapshotFixture.h"

#include "../../lib/GameLibrary.h"
#include "../../lib/GameSettings.h"
#include "../../lib/StartInfo.h"
#include "../../lib/campaign/CampaignState.h"
#include "../../lib/entities/artifact/CArtifactInstance.h"
#include "../../lib/callback/CCallback.h"
#include "../../lib/gameState/InfoAboutArmy.h"
#include "../../lib/IGameSettings.h"
#include "../../lib/bonuses/Bonus.h"
#include "../../lib/bonuses/BonusParameters.h"
#include "../../lib/battle/CObstacleInstance.h"
#include "../../lib/gameState/CGameState.h"
#include "../../lib/gameState/GameStatePackVisitor.h"
#include "../../lib/gameState/TavernHeroesPool.h"
#include "../../lib/bonuses/Limiters.h"
#include "../../lib/bonuses/Propagators.h"
#include "../../lib/bonuses/Updaters.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/entities/hero/CHero.h"
#include "../../lib/entities/hero/CHeroClass.h"
#include "../../lib/mapObjects/CGTownInstance.h"
#include "../../lib/mapObjects/MiscObjects.h"
#include "../../lib/mapObjects/army/CSimpleArmy.h"
#include "../../lib/mapping/CMap.h"
#include "../../lib/mapObjects/ObjectTemplate.h"
#include "../../lib/modding/CModHandler.h"
#include "../../lib/networkPacks/PacksForClient.h"
#include "../../lib/networkPacks/PacksForLobby.h"
#include "../../lib/spells/NewHorizonsMagic.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include "../../server/CGameHandler.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

namespace
{
using KnowledgeArtifact = std::pair<ArtifactID, ArtifactPosition>;

constexpr auto WISDOM_SKILL_ID = "new-horizons:wisdom";
constexpr auto MYSTICISM_PERK_ID = "new-horizons:wisdom.mysticism";
constexpr auto INTELLIGENCE_PERK_ID = "new-horizons:wisdom.intelligence";
constexpr auto MEDITATION_PERK_ID = "new-horizons:wisdom.meditation";
constexpr auto DEEP_KNOWLEDGE_PERK_ID = "new-horizons:wisdom.deepKnowledge";
constexpr auto ARCANE_RESERVOIR_PERK_ID = "new-horizons:wisdom.arcaneReservoir";

const std::array<KnowledgeArtifact, 4> & knowledgeArtifacts()
{
	// Identifier decoding requires the initialized game library, not static
	// initialization before the test environment has loaded its content.
	static const std::array<KnowledgeArtifact, 4> artifacts = {{
		{ArtifactID(ArtifactID::decode("core:thunderHelmet")), ArtifactPosition::HEAD},
		{ArtifactID(ArtifactID::decode("core:swordOfJudgement")), ArtifactPosition::RIGHT_HAND},
		{ArtifactID(ArtifactID::decode("core:necklaceOfDragonteeth")), ArtifactPosition::NECK},
		{ArtifactID(ArtifactID::decode("core:dragonWingTabard")), ArtifactPosition::SHOULDERS}
	}};
	return artifacts;
}

class SpellPointCapacityTest : public BattleTestFixture
{
protected:
	void SetUp() override
	{
		BattleTestFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module for its saved spell-point rules";

		startGame(startsWithFortifiedTown());
		ASSERT_TRUE(newHorizonsMagic::spellPointRulesActive(attackerSideHero->getMagicRules()));
	}

	void mapLoaded(CMap * loaded) override
	{
		BattleTestFixture::mapLoaded(loaded);

		JsonNode magicRules(JsonPath::builtin("config/newHorizonsMagic"));
		newHorizonsMagic::validateRules(magicRules);
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, magicRules);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
	}

	virtual bool startsWithFortifiedTown() const
	{
		return false;
	}

	void setKnowledge(CGHeroInstance * hero, int32_t value)
	{
		SetPrimarySkill change;
		change.id = hero->id;
		change.which = PrimarySkill::KNOWLEDGE;
		change.val = value;
		change.mode = ChangeValueMode::ABSOLUTE;
		gameState()->apply(change);
	}

	void setNormal(CGHeroInstance * hero, int32_t value)
	{
		SetMana change(hero->id, SetMana::Operation::SET_NORMAL, value);
		gameState()->apply(change);
	}

	void setMovement(CGHeroInstance * hero, int32_t value)
	{
		SetMovePoints change(hero->id, value);
		gameState()->apply(change);
	}

	virtual void setWisdomRank(CGHeroInstance * hero, int32_t rank)
	{
		// Wisdom is Magic-exclusive. Use a real Wizard hero for the normal perk-offer path.
		const auto solmyr = HeroTypeID(HeroTypeID::decode("core:solmyr"));
		ASSERT_NE(solmyr, HeroTypeID::NONE);
		hero->setHeroType(solmyr);
		const int wisdomId = SecondarySkill::decode(WISDOM_SKILL_ID);
		ASSERT_GE(wisdomId, 0);
		SetSecSkill change;
		change.id = hero->id;
		change.which = SecondarySkill(wisdomId);
		change.val = rank;
		change.mode = ChangeValueMode::ABSOLUTE;
		gameState()->apply(change);
	}

	bool selectWisdomPerkThroughLegalOffer(CGHeroInstance * hero, const std::string & perkId)
	{
		const auto rankLookup = [hero](const std::string & skillId)
		{
			return hero->getPerkSkillRank(skillId);
		};

		for(uint64_t seed = 0; seed < 4096; ++seed)
		{
			const auto offer = hero->getPerkState().prepareOffer(rankLookup, seed, hero->getPerkOfferWeightContext());
			const auto selected = std::find_if(offer.begin(), offer.end(), [&perkId](const auto & candidate)
			{
				return candidate.selection.perkId == perkId;
			});
			if(selected == offer.end())
				continue;

			const auto choice = static_cast<size_t>(std::distance(offer.begin(), selected));
			gameHandler->levelUpHero(hero, offer, choice, seed, false);
			return hero->hasActivePerk(WISDOM_SKILL_ID, perkId);
		}
		return false;
	}

	bool advanceWisdomAndSelectPerkThroughLegalOffers(CGHeroInstance * hero,
		const std::string & basicPerkId, const std::string & advancedPerkId)
	{
		setWisdomRank(hero, MasteryLevel::BASIC);
		if(!selectWisdomPerkThroughLegalOffer(hero, basicPerkId))
			return false;

		const int wisdomId = SecondarySkill::decode(WISDOM_SKILL_ID);
		if(wisdomId < 0)
			return false;
		const auto wisdom = SecondarySkill(wisdomId);
		gameHandler->levelUpHero(hero, wisdom, false);
		return hero->getSecSkillLevel(wisdom) == MasteryLevel::ADVANCED
			&& selectWisdomPerkThroughLegalOffer(hero, advancedPerkId);
	}

	bool advanceWisdomToExpertWithDeepKnowledge(CGHeroInstance * hero)
	{
		if(!advanceWisdomAndSelectPerkThroughLegalOffers(hero,
			INTELLIGENCE_PERK_ID, DEEP_KNOWLEDGE_PERK_ID))
			return false;

		const int wisdomId = SecondarySkill::decode(WISDOM_SKILL_ID);
		if(wisdomId < 0)
			return false;
		const auto wisdom = SecondarySkill(wisdomId);
		gameHandler->levelUpHero(hero, wisdom, false);
		return hero->getSecSkillLevel(wisdom) == MasteryLevel::EXPERT;
	}

	bool selectArcaneReservoirThroughLegalExpertOffer(CGHeroInstance * hero)
	{
		return advanceWisdomToExpertWithDeepKnowledge(hero)
			&& selectWisdomPerkThroughLegalOffer(hero, ARCANE_RESERVOIR_PERK_ID);
	}

	bool selectMysticismThroughLegalOffer(CGHeroInstance * hero)
	{
		setWisdomRank(hero, MasteryLevel::BASIC);
		return selectWisdomPerkThroughLegalOffer(hero, MYSTICISM_PERK_ID);
	}

	bool selectWisdomPerkFromPoolOffer(CGHeroInstance * hero, const std::string & perkId)
	{
		const auto rankLookup = [hero](const std::string & skillId)
		{
			return hero->getPerkSkillRank(skillId);
		};

		for(uint64_t seed = 0; seed < 4096; ++seed)
		{
			const auto offer = hero->getPerkState().prepareOffer(rankLookup, seed, hero->getPerkOfferWeightContext());
			const auto selected = std::find_if(offer.begin(), offer.end(), [&perkId](const auto & candidate)
			{
				return candidate.selection.perkId == perkId;
			});
			if(selected == offer.end())
				continue;

			hero->applyPerkSelection(selected->selection);
			return hero->hasActivePerk(WISDOM_SKILL_ID, perkId);
		}
		return false;
	}

	void grantBuffer(CGHeroInstance * hero, int32_t value)
	{
		SetMana change(hero->id, SetMana::Operation::GRANT_BUFFER, value);
		gameState()->apply(change);
	}

	void equipKnowledgeSet(CGHeroInstance * hero)
	{
		for(const auto & [artifact, slot] : knowledgeArtifacts())
		{
			ASSERT_NE(artifact, ArtifactID::NONE);
			ASSERT_EQ(hero->getArt(slot), nullptr);
			giveArtifact(hero, artifact, slot);
		}
	}

	void eraseKnowledgeSet(CGHeroInstance * hero)
	{
		BulkEraseArtifacts erase;
		erase.artHolder = hero->id;
		for(const auto & [artifact, slot] : knowledgeArtifacts())
		{
			(void)artifact;
			erase.posPack.push_back(slot);
		}
		gameState()->apply(erase);
	}

	void expectPools(const CGHeroInstance * hero, int32_t normal, int32_t buffer, int32_t maximum)
	{
		EXPECT_EQ(hero->getNormalSpellPoints(), normal);
		EXPECT_EQ(hero->getBufferSpellPoints(), buffer);
		EXPECT_EQ(hero->manaLimit(), maximum);
		EXPECT_EQ(hero->getManaAvailable(), static_cast<int64_t>(normal) + buffer);
	}
};

// Current weighted offers need the same Wizard identity at rule capture and
// selection. Keep the generic arithmetic/explicit legacy fixtures unchanged.
class SpellPointCapacityWizardTest : public SpellPointCapacityTest
{
protected:
	void mapLoaded(CMap * loaded) override
	{
		SpellPointCapacityTest::mapLoaded(loaded);
		const HeroTypeID solmyr(HeroTypeID::decode("core:solmyr"));
		const HeroTypeID aine(HeroTypeID::decode("core:aine"));
		ASSERT_NE(solmyr, HeroTypeID::NONE);
		ASSERT_NE(aine, HeroTypeID::NONE);
		for(const auto & object : loaded->objects)
			if(auto * hero = dynamic_cast<CGHeroInstance *>(object.get()))
			{
				if(hero->getOwner() == PlayerColor(0))
					hero->setHeroType(solmyr);
				else if(hero->getOwner() == PlayerColor(1))
					hero->setHeroType(aine);
			}
	}
	void SetUp() override
	{
		SpellPointCapacityTest::SetUp();
		ASSERT_NE(attackerSideHero, nullptr);
		ASSERT_NE(defenderSideHero, nullptr);
		ASSERT_EQ(attackerSideHero->getHeroType()->getJsonKey(), "core:solmyr");
		ASSERT_EQ(defenderSideHero->getHeroType()->getJsonKey(), "core:aine");
		for(const auto * hero : {attackerSideHero, defenderSideHero})
		{
			ASSERT_EQ(hero->getHeroClass()->getJsonKey(), "core:wizard");
			ASSERT_TRUE(newHorizonsHeroes::usesSkillOfferWeights(hero->getPrimaryGrowthRules()));
			ASSERT_EQ(hero->getPrimaryGrowthRules(), newHorizonsHeroes::resolveHeroRules(
				gameState()->getHeroDevelopmentRules(), hero->getHeroClass()->getId()));
			const auto context = hero->getPerkOfferWeightContext();
			ASSERT_EQ(context.mode, newHorizonsHeroes::PerkState::OfferWeightMode::CapturedTable);
			ASSERT_TRUE(context.lookup);
			EXPECT_EQ(context.lookup(WISDOM_SKILL_ID), std::optional<int>(9));
		}
	}
	void setWisdomRank(CGHeroInstance * hero, int32_t rank) override
	{
		// Identity and captured weights already agree. Never replace identity or
		// overwrite captured rules to make a formerly zero-weight offer appear.
		ASSERT_EQ(hero->getHeroClass()->getJsonKey(), "core:wizard");
		SetSecSkill change;
		change.id = hero->id;
		change.which = SecondarySkill(SecondarySkill::decode(WISDOM_SKILL_ID));
		change.val = rank;
		change.mode = ChangeValueMode::ABSOLUTE;
		gameState()->apply(change);
	}
};

class BottomlessMindCapacityTest : public SpellPointCapacityTest
{
protected:
	virtual bool captureBottomlessMind() const { return true; }
	void SetUp() override
	{
		BattleTestFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires New Horizons";
		// Initialize the actual authored Andra profile. Unlike BattleTestFixture's
		// damage-isolation setup, do not strip her native skills or replace identity.
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).name("Bottomless Mind")
			.playerActive(PlayerColor(0)).playerActive(PlayerColor(1))
			.hero({5, 5, 0}, HeroTypeID(HeroTypeID::decode("core:andra")), PlayerColor(0))
			.hero({7, 7, 0}, HeroTypeID(HeroTypeID::decode("core:serena")), PlayerColor(1));
		startWithMap(std::move(builder));
		server.gameState = gameState();
		gameHandler = std::make_shared<CGameHandler>(server, gameState());
		attackerSideHero = findHeroByOwner(PlayerColor(0));
		defenderSideHero = findHeroByOwner(PlayerColor(1));
		ASSERT_NE(attackerSideHero, nullptr);
		ASSERT_NE(defenderSideHero, nullptr);
		ASSERT_EQ(attackerSideHero->getHeroType()->getJsonKey(), "core:andra");
		ASSERT_TRUE(attackerSideHero->hasActivePerk(WISDOM_SKILL_ID, INTELLIGENCE_PERK_ID));
		ASSERT_TRUE(newHorizonsHeroes::usesSkillOfferWeights(attackerSideHero->getPrimaryGrowthRules()));
	}
	void mapLoaded(CMap * loaded) override
	{
		SpellPointCapacityTest::mapLoaded(loaded);
		JsonNode development(JsonPath::builtin("config/newHorizonsHeroes"));
		if(!captureBottomlessMind())
		{
			development.Struct().erase("bottomlessMindSpecialty");
			development.Struct().erase("masterSpellwardSpecialty"); // Explicit pre-Bottomless capture.
			// This historical world predates R2 but retains captured policy1.
			auto perks = loaded->getSettings().getValue(EGameSettings::HEROES_NEW_HORIZONS_PERKS);
			perks["offerWeightPolicyVersion"].Integer() = 1;
			perks.Struct().erase("perkOfferNormalization");
			perks.setOverrideFlag(true);
			loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, perks);
		}
		development.setOverrideFlag(true);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, development);
	}
	void setWisdomActive(bool active)
	{
		SetSecSkill rank;
		rank.id = attackerSideHero->id;
		rank.which = SecondarySkill(SecondarySkill::decode(WISDOM_SKILL_ID));
		rank.val = active ? MasteryLevel::BASIC : MasteryLevel::NONE;
		rank.mode = ChangeValueMode::ABSOLUTE;
		gameState()->apply(rank);
	}
};

class BottomlessMindAbsentCapacityTest : public BottomlessMindCapacityTest
{
protected:
	bool captureBottomlessMind() const override { return false; }
};

class ArcanePreparationCapacityTest : public SpellPointCapacityTest
{
protected:
	virtual bool capturePreparation() const { return true; }
	void mapLoaded(CMap * loaded) override
	{
		SpellPointCapacityTest::mapLoaded(loaded);
		JsonNode development(JsonPath::builtin("config/newHorizonsHeroes"));
		if(!capturePreparation())
			development.Struct().erase("arcanePreparationSpecialty");
		development.setOverrideFlag(true);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, development);
		for(const auto & object : loaded->objects)
			if(auto * hero = dynamic_cast<CGHeroInstance *>(object.get()); hero && hero->getOwner() == PlayerColor(0))
				hero->setHeroType(HeroTypeID(HeroTypeID::decode("core:serena")));
	}
	void activateIntelligence()
	{
		ASSERT_TRUE(attackerSideHero->getPerkState().hasSelection(WISDOM_SKILL_ID, INTELLIGENCE_PERK_ID));
		SetSecSkill rank;
		rank.id = attackerSideHero->id;
		rank.which = SecondarySkill(SecondarySkill::decode(WISDOM_SKILL_ID));
		rank.val = MasteryLevel::BASIC;
		rank.mode = ChangeValueMode::ABSOLUTE;
		gameState()->apply(rank);
		ASSERT_TRUE(attackerSideHero->hasActivePerk(WISDOM_SKILL_ID, INTELLIGENCE_PERK_ID));
	}
};

class ArcanePreparationAbsentCapacityTest : public ArcanePreparationCapacityTest
{
protected:
	bool capturePreparation() const override { return false; }
};

class HighLoreCapacityTest : public SpellPointCapacityTest
{
protected:
	virtual bool captureHighLore() const { return true; }
	void mapLoaded(CMap * loaded) override
	{
		SpellPointCapacityTest::mapLoaded(loaded);
		JsonNode development(JsonPath::builtin("config/newHorizonsHeroes"));
		ASSERT_TRUE(development["druidSpecialties"].isStruct());
		// This fixture isolates the preceding Druid epoch, including its
		// faithful policy-absent full-world round trip.
		development.Struct().erase("fortuneChildSpecialty");
		development.Struct().erase("arcanePreparationSpecialty");
		development.Struct().erase("bottomlessMindSpecialty");
		development.Struct().erase("masterSpellwardSpecialty");
		// Isolate Druid admission/codec guards from the same-epoch weighted
		// offer feature; the existing fixture uses its historical offer helper.
		JsonNode perks(JsonPath::builtin("config/newHorizonsPerks"));
		perks.Struct().erase("offerWeightPolicyVersion");
		perks.Struct().erase("perkOfferNormalization");
		perks.setOverrideFlag(true);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, perks);
		if(!captureHighLore())
			development.Struct().erase("druidSpecialties");
		development.setOverrideFlag(true);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, development);
		for(const auto & object : loaded->objects)
			if(auto * hero = dynamic_cast<CGHeroInstance *>(object.get()); hero && hero->getOwner() == PlayerColor(0))
				hero->setHeroType(HeroTypeID(HeroTypeID::decode("core:elleshar")));
	}
	void activateDefaultIntelligence()
	{
		ASSERT_EQ(attackerSideHero->getHeroType()->getJsonKey(), "core:elleshar");
		ASSERT_TRUE(attackerSideHero->getPerkState().hasSelection(WISDOM_SKILL_ID, INTELLIGENCE_PERK_ID));
		SetSecSkill rank;
		rank.id = attackerSideHero->id;
		rank.which = SecondarySkill(SecondarySkill::decode(WISDOM_SKILL_ID));
		rank.val = MasteryLevel::BASIC;
		rank.mode = ChangeValueMode::ABSOLUTE;
		gameState()->apply(rank);
		ASSERT_TRUE(attackerSideHero->hasActivePerk(WISDOM_SKILL_ID, INTELLIGENCE_PERK_ID));
	}
};

class HighLoreAbsentCapacityTest : public HighLoreCapacityTest
{
protected:
	bool captureHighLore() const override { return false; }
};

class SpellPointCapacityPlannedMysticismTest : public SpellPointCapacityWizardTest
{
protected:
	void mapLoaded(CMap * loaded) override
	{
		SpellPointCapacityWizardTest::mapLoaded(loaded);
		// A planned perk cannot remain an active starting selection elsewhere in
		// the initialized hero pool. Change only this fixture's five conflicting
		// captured starts; Solmyr/Aine, all skills and weighted tables stay current.
		JsonNode development(JsonPath::builtin("config/newHorizonsHeroes"));
		auto & profiles = development["startingSkills"]["startingDevelopmentProfiles"].Struct();
		const std::array<std::string, 5> mysticismStarts = {
			"core:jaegar", "core:thant", "core:axsis", "core:ingham", "core:rosic"};
		size_t conflicts = 0;
		for(const auto & [hero, profile] : profiles)
			for(const auto & start : profile["startingPerks"].Vector())
				if(start["perk"].String() == MYSTICISM_PERK_ID)
				{
					ASSERT_NE(std::find(mysticismStarts.begin(), mysticismStarts.end(), hero), mysticismStarts.end());
					++conflicts;
				}
		ASSERT_EQ(conflicts, mysticismStarts.size());
		for(const auto & hero : mysticismStarts)
		{
			ASSERT_TRUE(profiles.contains(hero));
			auto & starts = profiles.at(hero)["startingPerks"].Vector();
			ASSERT_EQ(starts.size(), 1);
			ASSERT_EQ(starts.front()["skill"].String(), WISDOM_SKILL_ID);
			ASSERT_EQ(starts.front()["perk"].String(), MYSTICISM_PERK_ID);
			starts.front()["perk"].String() = INTELLIGENCE_PERK_ID;
		}
		development.setOverrideFlag(true);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, development);
		JsonNode perks(JsonPath::builtin("config/newHorizonsPerks"));
		for(auto & perk : perks["skills"][WISDOM_SKILL_ID]["perks"].Vector())
		{
			if(perk["id"].String() == MYSTICISM_PERK_ID)
				perk["effect"]["status"].String() = "planned";
		}
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, perks);
	}
	void SetUp() override
	{
		SpellPointCapacityWizardTest::SetUp();
		for(const auto * hero : {attackerSideHero, defenderSideHero})
		{
			ASSERT_FALSE(hero->getPerkState().hasSelection(WISDOM_SKILL_ID, MYSTICISM_PERK_ID));
		}
	}
};

class SpellPointCapacityPlannedMeditationTest : public SpellPointCapacityWizardTest
{
protected:
	void mapLoaded(CMap * loaded) override
	{
		SpellPointCapacityWizardTest::mapLoaded(loaded);
		JsonNode perks(JsonPath::builtin("config/newHorizonsPerks"));
		for(auto & perk : perks["skills"][WISDOM_SKILL_ID]["perks"].Vector())
		{
			if(perk["id"].String() == MEDITATION_PERK_ID)
				perk["effect"]["status"].String() = "planned";
		}
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, perks);
	}
};

class SpellPointCapacityPlannedArcaneReservoirTest : public SpellPointCapacityWizardTest
{
protected:
	void mapLoaded(CMap * loaded) override
	{
		SpellPointCapacityWizardTest::mapLoaded(loaded);
		JsonNode perks(JsonPath::builtin("config/newHorizonsPerks"));
		for(auto & perk : perks["skills"][WISDOM_SKILL_ID]["perks"].Vector())
		{
			if(perk["id"].String() == ARCANE_RESERVOIR_PERK_ID)
				perk["effect"]["status"].String() = "planned";
		}
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, perks);
	}
};

class SpellPointCapacityMageGuildTest : public SpellPointCapacityWizardTest
{
protected:
	bool startsWithFortifiedTown() const override
	{
		return true;
	}
};

class NewTurnRecordingGameServer final : public RecordingGameServer
{
public:
	void applyPack(CPackForClient & pack) override
	{
		if(const auto * turn = dynamic_cast<const NewTurn *>(&pack))
			lastNewTurn = *turn;
		RecordingGameServer::applyPack(pack);
	}

	std::optional<NewTurn> lastNewTurn;
};
}

TEST_F(SpellPointCapacityTest, OwnedAdventureHeroInfoIncludesActualCapacityAndBuffer)
{
	setKnowledge(attackerSideHero, 30);
	setNormal(attackerSideHero, 30);
	grantBuffer(attackerSideHero, 50);
	auto callback = makeCallback(attackerSideHero->tempOwner);
	InfoAboutHero info;
	ASSERT_TRUE(callback->getHeroInfo(attackerSideHero, info));
	ASSERT_TRUE(info.details);
	EXPECT_EQ(info.details->manaLimit, attackerSideHero->manaLimit());
	EXPECT_EQ(info.details->mana, 80);
	EXPECT_EQ(info.details->bufferMana, 50);

	// DETAILED alone (also used by enemy Visions) must not grant capacity access.
	InfoAboutHero restricted(attackerSideHero, InfoAboutHero::EInfoLevel::DETAILED);
	ASSERT_TRUE(restricted.details);
	EXPECT_EQ(restricted.details->manaLimit, -1);
}

TEST_F(SpellPointCapacityTest, KnowledgeEquipmentRaisesCapacityWithoutRefillAndRemovalClampsOnlyNormal)
{
	setKnowledge(attackerSideHero, 80);
	setNormal(attackerSideHero, 80);
	grantBuffer(attackerSideHero, 17);
	expectPools(attackerSideHero, 80, 17, 80);

	// These four real artifacts contribute +100 effective Knowledge in total.
	equipKnowledgeSet(attackerSideHero);
	EXPECT_EQ(attackerSideHero->getPrimSkillLevel(PrimarySkill::KNOWLEDGE), 180);
	expectPools(attackerSideHero, 80, 17, 180);

	setNormal(attackerSideHero, 180);
	expectPools(attackerSideHero, 180, 17, 180);
	eraseKnowledgeSet(attackerSideHero);
	EXPECT_EQ(attackerSideHero->getPrimSkillLevel(PrimarySkill::KNOWLEDGE), 80);
	expectPools(attackerSideHero, 80, 17, 80);

	// Restoring the same capacity later cannot recover the points lost on removal.
	equipKnowledgeSet(attackerSideHero);
	expectPools(attackerSideHero, 80, 17, 180);
}

TEST_F(SpellPointCapacityTest, EqualCapacityBulkArtifactSwapDoesNotClampEitherHeroMidTransaction)
{
	setKnowledge(attackerSideHero, 80);
	setKnowledge(defenderSideHero, 80);
	setNormal(attackerSideHero, 80);
	setNormal(defenderSideHero, 80);
	grantBuffer(attackerSideHero, 17);
	grantBuffer(defenderSideHero, 29);
	equipKnowledgeSet(attackerSideHero);
	equipKnowledgeSet(defenderSideHero);
	setNormal(attackerSideHero, 180);
	setNormal(defenderSideHero, 180);
	expectPools(attackerSideHero, 180, 17, 180);
	expectPools(defenderSideHero, 180, 29, 180);

	BulkMoveArtifacts swap(PlayerColor(0), attackerSideHero->id, defenderSideHero->id, true);
	for(const auto & [artifact, slot] : knowledgeArtifacts())
	{
		(void)artifact;
		swap.artsPack0.emplace_back(slot, slot);
		swap.artsPack1.emplace_back(slot, slot);
	}
	gameState()->apply(swap);

	// The visitor removes both sets before putting either back. Only the complete
	// top-level pack has the unchanged capacities, so neither hero may lose Normal.
	expectPools(attackerSideHero, 180, 17, 180);
	expectPools(defenderSideHero, 180, 29, 180);
}

TEST_F(SpellPointCapacityTest, IntelligenceRaisesKnowledgeCapacityByThirtyPercentWithoutFillingIt)
{
	setKnowledge(attackerSideHero, 100);
	setNormal(attackerSideHero, 100);
	grantBuffer(attackerSideHero, 37);
	expectPools(attackerSideHero, 100, 37, 100);

	const int wisdomId = SecondarySkill::decode("new-horizons:wisdom");
	ASSERT_GE(wisdomId, 0);
	SetSecSkill rank;
	rank.id = attackerSideHero->id;
	rank.which = SecondarySkill(wisdomId);
	rank.val = 1;
	rank.mode = ChangeValueMode::ABSOLUTE;
	gameState()->apply(rank);

	HeroPerkChosen intelligence;
	intelligence.hero = attackerSideHero->id;
	intelligence.selection = {"new-horizons:wisdom", "new-horizons:wisdom.intelligence"};
	gameState()->apply(intelligence);

	EXPECT_TRUE(attackerSideHero->hasActivePerk("new-horizons:wisdom", "new-horizons:wisdom.intelligence"));
	expectPools(attackerSideHero, 100, 37, 130);

	equipKnowledgeSet(attackerSideHero);
	EXPECT_EQ(attackerSideHero->getPrimSkillLevel(PrimarySkill::KNOWLEDGE), 200);
	expectPools(attackerSideHero, 100, 37, 260);

	setNormal(attackerSideHero, 260);
	eraseKnowledgeSet(attackerSideHero);
	expectPools(attackerSideHero, 130, 37, 130);
	equipKnowledgeSet(attackerSideHero);
	expectPools(attackerSideHero, 130, 37, 260);

	// Removing the skill disables the learned perk and must invalidate capacity.
	setNormal(attackerSideHero, 260);
	rank.val = 0;
	gameState()->apply(rank);
	expectPools(attackerSideHero, 200, 37, 200);
}

TEST_F(SpellPointCapacityWizardTest, ExpertArcaneReservoirAddsTwentyFiveAfterIntelligenceWithoutRefilling)
{
	setKnowledge(attackerSideHero, 100);
	setNormal(attackerSideHero, 64);
	grantBuffer(attackerSideHero, 17);

	ASSERT_TRUE(selectArcaneReservoirThroughLegalExpertOffer(attackerSideHero));
	const int wisdomId = SecondarySkill::decode(WISDOM_SKILL_ID);
	ASSERT_GE(wisdomId, 0);
	EXPECT_EQ(attackerSideHero->getSecSkillLevel(SecondarySkill(wisdomId)), MasteryLevel::EXPERT);
	EXPECT_TRUE(attackerSideHero->hasActivePerk(WISDOM_SKILL_ID, ARCANE_RESERVOIR_PERK_ID));
	expectPools(attackerSideHero, 64, 17, 155); // floor(1.30 * 100) + 25; capacity does not refill.
}

TEST_F(HighLoreCapacityTest, CapturedPolicyScalesOnlyIntelligenceExtraAtLevelOneTenAndCapWithoutRefill)
{
	const auto policy = attackerSideHero->getDruidSpecialtyRules(newHorizonsHeroes::DruidSpecialtyKind::HIGH_LORE);
	ASSERT_TRUE(policy);
	ASSERT_EQ(policy->highLore.percentPerLevel, 10);
	ASSERT_EQ(policy->highLore.maximumPercent, 100);
	setKnowledge(attackerSideHero, 101);
	setNormal(attackerSideHero, 60);
	grantBuffer(attackerSideHero, 17);
	EXPECT_FALSE(attackerSideHero->hasActivePerk(WISDOM_SKILL_ID, INTELLIGENCE_PERK_ID));
	EXPECT_EQ(attackerSideHero->getHighLoreManaBonus(), 0);
	expectPools(attackerSideHero, 60, 17, 101);
	ASSERT_NO_FATAL_FAILURE(activateDefaultIntelligence());
	// Levels here are native fixture inputs, not a claim of accepted level-up actions.
	for(const auto & [level, extra] : std::array<std::pair<ui32, int32_t>, 3>{{{1, 3}, {10, 30}, {20, 30}}})
	{
		SCOPED_TRACE(level);
		attackerSideHero->level = level;
		EXPECT_EQ(attackerSideHero->getHighLoreManaBonus(), extra);
		expectPools(attackerSideHero, 60, 17, 131 + extra);
	}

	setKnowledge(defenderSideHero, 101);
	ASSERT_TRUE(advanceWisdomAndSelectPerkThroughLegalOffers(defenderSideHero,
		INTELLIGENCE_PERK_ID, DEEP_KNOWLEDGE_PERK_ID));
	ASSERT_NE(defenderSideHero->getHeroType()->getJsonKey(), "core:elleshar");
	EXPECT_FALSE(defenderSideHero->getDruidSpecialtyRules(newHorizonsHeroes::DruidSpecialtyKind::HIGH_LORE));
	EXPECT_EQ(defenderSideHero->getHighLoreManaBonus(), 0);
	EXPECT_EQ(defenderSideHero->manaLimit(), 131);
}

TEST_F(HighLoreCapacityTest, ArcaneReservoirStaysTwentyFiveAndCurrentWorldCodecRetainsPolicyAndPools)
{
	setKnowledge(attackerSideHero, 101);
	setNormal(attackerSideHero, 60);
	grantBuffer(attackerSideHero, 17);
	ASSERT_NO_FATAL_FAILURE(activateDefaultIntelligence());
	const SecondarySkill wisdom(SecondarySkill::decode(WISDOM_SKILL_ID));
	gameHandler->levelUpHero(attackerSideHero, wisdom, false);
	ASSERT_TRUE(selectWisdomPerkThroughLegalOffer(attackerSideHero, DEEP_KNOWLEDGE_PERK_ID));
	gameHandler->levelUpHero(attackerSideHero, wisdom, false);
	ASSERT_TRUE(selectWisdomPerkThroughLegalOffer(attackerSideHero, ARCANE_RESERVOIR_PERK_ID));
	attackerSideHero->level = 10;
	EXPECT_EQ(attackerSideHero->getHighLoreManaBonus(), 30);
	expectPools(attackerSideHero, 60, 17, 186); // 131 + HighLore30 + unchanged Reservoir25.
	const auto captured = attackerSideHero->getPrimaryGrowthRules();
	const auto saved = gameState()->saveToMemory();
	CGameState restored;
	restored.preInit(LIBRARY);
	restored.loadFromMemory(saved);
	const auto * hero = restored.getHero(attackerSideHero->id);
	ASSERT_NE(hero, nullptr);
	EXPECT_EQ(hero->getPrimaryGrowthRules(), captured);
	EXPECT_EQ(hero->level, 10);
	EXPECT_EQ(hero->getHighLoreManaBonus(), 30);
	expectPools(hero, 60, 17, 186);
	CMemorySerializer old;
	old.oser.version = ESerializationVersion::NEW_HORIZONS_HERO_LUCK;
	EXPECT_THROW(old.oser & attackerSideHero, std::runtime_error);
}

TEST_F(HighLoreAbsentCapacityTest, OlderCapturedAbsenceKeepsOrdinaryCapacityAndSurvivesPriorEpoch)
{
	for(const char * key : {"fortuneChildSpecialty", "arcanePreparationSpecialty"})
	{
		EXPECT_FALSE(gameState()->getHeroDevelopmentRules().Struct().contains(key));
		EXPECT_FALSE(attackerSideHero->getPrimaryGrowthRules().Struct().contains(key));
		EXPECT_FALSE(defenderSideHero->getPrimaryGrowthRules().Struct().contains(key));
	}
	ASSERT_TRUE(attackerSideHero->getPrimaryGrowthRules()["druidSpecialties"].isNull());
	setKnowledge(attackerSideHero, 101);
	setNormal(attackerSideHero, 60);
	grantBuffer(attackerSideHero, 17);
	ASSERT_NO_FATAL_FAILURE(activateDefaultIntelligence());
	attackerSideHero->level = 10;
	EXPECT_FALSE(attackerSideHero->getDruidSpecialtyRules(newHorizonsHeroes::DruidSpecialtyKind::HIGH_LORE));
	EXPECT_EQ(attackerSideHero->getHighLoreManaBonus(), 0);
	expectPools(attackerSideHero, 60, 17, 131);
	CMemorySerializer bytes;
	bytes.oser.version = ESerializationVersion::NEW_HORIZONS_HERO_LUCK;
	bytes.iser.version = ESerializationVersion::NEW_HORIZONS_HERO_LUCK;
	// A detached hero codec does not rebuild inherited bonuses. Restore the
	// real world through its ordinary post-load bonus-tree reconstruction.
	ASSERT_NO_THROW(bytes.oser & *gameState());
	CGameState restored;
	restored.preInit(LIBRARY);
	bytes.iser.loadingGamestate = true;
	bytes.iser.cb = &restored;
	ASSERT_NO_THROW(bytes.iser & restored);
	const auto * hero = restored.getHero(attackerSideHero->id);
	ASSERT_NE(hero, nullptr);
	EXPECT_TRUE(hero->getPrimaryGrowthRules()["druidSpecialties"].isNull());
	EXPECT_FALSE(hero->getPerkState().rules.Struct().contains("offerWeightPolicyVersion"));
	for(const char * key : {"fortuneChildSpecialty", "arcanePreparationSpecialty"})
	{
		EXPECT_FALSE(restored.getHeroDevelopmentRules().Struct().contains(key));
		EXPECT_FALSE(hero->getPrimaryGrowthRules().Struct().contains(key));
	}
	EXPECT_EQ(hero->getHighLoreManaBonus(), 0);
	expectPools(hero, 60, 17, 131);
}

TEST_F(SpellPointCapacityWizardTest, LosingExpertWisdomClampsNormalCapacityAndPreservesBuffer)
{
	setKnowledge(attackerSideHero, 100);
	ASSERT_TRUE(selectArcaneReservoirThroughLegalExpertOffer(attackerSideHero));
	setNormal(attackerSideHero, 155);
	grantBuffer(attackerSideHero, 23);
	expectPools(attackerSideHero, 155, 23, 155);

	setWisdomRank(attackerSideHero, MasteryLevel::ADVANCED);

	EXPECT_FALSE(attackerSideHero->hasActivePerk(WISDOM_SKILL_ID, ARCANE_RESERVOIR_PERK_ID));
	expectPools(attackerSideHero, 130, 23, 130); // The authoritative rank-change event clamps Normal only.
}

TEST_F(SpellPointCapacityWizardTest, ExpertArcaneReservoirSelectionSurvivesSaveLoad)
{
	setKnowledge(attackerSideHero, 100);
	setNormal(attackerSideHero, 48);
	grantBuffer(attackerSideHero, 12);
	ASSERT_TRUE(selectArcaneReservoirThroughLegalExpertOffer(attackerSideHero));
	expectPools(attackerSideHero, 48, 12, 155);

	const auto saved = gameState()->saveToMemory();
	CGameState restored;
	restored.preInit(LIBRARY);
	restored.loadFromMemory(saved);
	const auto * restoredHero = restored.getHero(attackerSideHero->id);
	ASSERT_NE(restoredHero, nullptr);
	EXPECT_TRUE(restoredHero->hasActivePerk(WISDOM_SKILL_ID, ARCANE_RESERVOIR_PERK_ID));
	expectPools(restoredHero, 48, 12, 155);
}

TEST_F(SpellPointCapacityPlannedArcaneReservoirTest, SavedPlannedSnapshotCannotOfferOrApplyCapacity)
{
	setKnowledge(attackerSideHero, 100);
	setNormal(attackerSideHero, 48);
	grantBuffer(attackerSideHero, 9);
	ASSERT_TRUE(advanceWisdomToExpertWithDeepKnowledge(attackerSideHero));
	expectPools(attackerSideHero, 48, 9, 130);

	const auto rankLookup = [this](const std::string & skillId)
	{
		return attackerSideHero->getPerkSkillRank(skillId);
	};
	for(uint64_t seed = 0; seed < 128; ++seed)
	{
		const auto offer = attackerSideHero->getPerkState().prepareOffer(rankLookup, seed, attackerSideHero->getPerkOfferWeightContext());
		EXPECT_TRUE(std::none_of(offer.begin(), offer.end(), [](const auto & candidate)
		{
			return candidate.selection.perkId == ARCANE_RESERVOIR_PERK_ID;
		}));
	}
	EXPECT_THROW(attackerSideHero->applyPerkSelection({WISDOM_SKILL_ID, ARCANE_RESERVOIR_PERK_ID}), std::runtime_error);
	EXPECT_FALSE(attackerSideHero->hasActivePerk(WISDOM_SKILL_ID, ARCANE_RESERVOIR_PERK_ID));

	const auto saved = gameState()->saveToMemory();
	CGameState restored;
	restored.preInit(LIBRARY);
	restored.loadFromMemory(saved);
	const auto * restoredHero = restored.getHero(attackerSideHero->id);
	ASSERT_NE(restoredHero, nullptr);
	EXPECT_FALSE(restoredHero->hasActivePerk(WISDOM_SKILL_ID, ARCANE_RESERVOIR_PERK_ID));
	expectPools(restoredHero, 48, 9, 130);
	const auto restoredRankLookup = [restoredHero](const std::string & skillId)
	{
		return restoredHero->getPerkSkillRank(skillId);
	};
	for(uint64_t seed = 0; seed < 128; ++seed)
	{
		const auto offer = restoredHero->getPerkState().prepareOffer(restoredRankLookup, seed, restoredHero->getPerkOfferWeightContext());
		EXPECT_TRUE(std::none_of(offer.begin(), offer.end(), [](const auto & candidate)
		{
			return candidate.selection.perkId == ARCANE_RESERVOIR_PERK_ID;
		}));
	}
}

TEST_F(SpellPointCapacityWizardTest, WisdomMysticismUsesFiveMinimumTenPercentAndOnlyRestoresNormalMana)
{
	setKnowledge(attackerSideHero, 20);
	setNormal(attackerSideHero, 0);
	grantBuffer(attackerSideHero, 13);
	setWisdomRank(attackerSideHero, MasteryLevel::BASIC);

	// The Wisdom rank alone does not grant the perk effect.
	EXPECT_FALSE(attackerSideHero->hasActivePerk(WISDOM_SKILL_ID, MYSTICISM_PERK_ID));
	EXPECT_EQ(attackerSideHero->getManaNewTurn(), 1); // Ordinary daily regeneration remains.
	expectPools(attackerSideHero, 0, 13, 20);

	ASSERT_TRUE(selectMysticismThroughLegalOffer(attackerSideHero));
	EXPECT_EQ(attackerSideHero->getManaNewTurn(), 5); // floor(20 / 10) is below the minimum.
	expectPools(attackerSideHero, 0, 13, 20); // Forecasting must not mutate either pool.

	setKnowledge(attackerSideHero, 100);
	EXPECT_EQ(attackerSideHero->getManaNewTurn(), 10);
	expectPools(attackerSideHero, 0, 13, 100);
	setKnowledge(attackerSideHero, 109);
	EXPECT_EQ(attackerSideHero->getManaNewTurn(), 10); // The 10% term is floored.
	expectPools(attackerSideHero, 0, 13, 109);

	setNormal(attackerSideHero, 106);
	EXPECT_EQ(attackerSideHero->getManaNewTurn(), 109); // Recovery is capped at Normal capacity.
	expectPools(attackerSideHero, 106, 13, 109);
}

TEST_F(SpellPointCapacityWizardTest, MysticismIsARegenerationFloorAndStrongerExistingRegenerationWins)
{
	setKnowledge(attackerSideHero, 100);
	setNormal(attackerSideHero, 0);
	ASSERT_EQ(attackerSideHero->manaRegain(), 1);
	ASSERT_TRUE(selectMysticismThroughLegalOffer(attackerSideHero));

	auto ordinaryRegeneration = std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::MANA_REGENERATION, BonusSource::OTHER, 1, BonusSourceID());
	attackerSideHero->addNewBonus(ordinaryRegeneration);
	EXPECT_EQ(attackerSideHero->manaRegain(), 10); // Mysticism is a floor over the +1 source.
	EXPECT_EQ(attackerSideHero->getManaNewTurn(), 10);
	attackerSideHero->removeBonus(ordinaryRegeneration);

	auto strongerRegeneration = std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::MANA_REGENERATION, BonusSource::OTHER, 25, BonusSourceID());
	attackerSideHero->addNewBonus(strongerRegeneration);
	EXPECT_EQ(attackerSideHero->manaRegain(), 26); // Existing +1 and new +25, not another Mysticism grant.
	EXPECT_EQ(attackerSideHero->getManaNewTurn(), 26);
	attackerSideHero->removeBonus(strongerRegeneration);
}

TEST_F(SpellPointCapacityWizardTest, BasicWisdomCanSelectMysticismLegallyAndSelectionSurvivesSaveLoad)
{
	setKnowledge(attackerSideHero, 40);
	setNormal(attackerSideHero, 0);
	grantBuffer(attackerSideHero, 7);
	ASSERT_TRUE(selectMysticismThroughLegalOffer(attackerSideHero));
	EXPECT_EQ(attackerSideHero->getSecSkillLevel(SecondarySkill(SecondarySkill::decode(WISDOM_SKILL_ID))),
		MasteryLevel::BASIC);

	const auto saved = gameState()->saveToMemory();
	CGameState restored;
	restored.preInit(LIBRARY);
	restored.loadFromMemory(saved);
	const auto * restoredHero = restored.getHero(attackerSideHero->id);
	ASSERT_NE(restoredHero, nullptr);
	EXPECT_TRUE(restoredHero->hasActivePerk(WISDOM_SKILL_ID, MYSTICISM_PERK_ID));
	EXPECT_EQ(restoredHero->getManaNewTurn(), 5);
	EXPECT_EQ(restoredHero->getNormalSpellPoints(), 0);
	EXPECT_EQ(restoredHero->getBufferSpellPoints(), 7);

	setWisdomRank(attackerSideHero, 0);
	EXPECT_FALSE(attackerSideHero->hasActivePerk(WISDOM_SKILL_ID, MYSTICISM_PERK_ID));
	EXPECT_EQ(attackerSideHero->getManaNewTurn(), 1);
}

TEST_F(SpellPointCapacityPlannedMysticismTest, SavedPlannedSnapshotDoesNotOfferOrApplyDailyRecovery)
{
	setKnowledge(attackerSideHero, 20);
	setNormal(attackerSideHero, 0);
	grantBuffer(attackerSideHero, 9);
	setWisdomRank(attackerSideHero, MasteryLevel::BASIC);

	const auto saved = gameState()->saveToMemory();
	CGameState restored;
	restored.preInit(LIBRARY);
	restored.loadFromMemory(saved);
	const auto * restoredHero = restored.getHero(attackerSideHero->id);
	ASSERT_NE(restoredHero, nullptr);
	const auto offer = restoredHero->getPerkState().prepareOffer([restoredHero](const std::string & skillId)
	{
		return restoredHero->getPerkSkillRank(skillId);
	}, 42, restoredHero->getPerkOfferWeightContext());
	EXPECT_TRUE(std::none_of(offer.begin(), offer.end(), [](const auto & candidate)
	{
		return candidate.selection.perkId == MYSTICISM_PERK_ID;
	}));
	EXPECT_FALSE(restoredHero->hasActivePerk(WISDOM_SKILL_ID, MYSTICISM_PERK_ID));
	EXPECT_EQ(restoredHero->getManaNewTurn(), 1);
	EXPECT_EQ(restoredHero->getNormalSpellPoints(), 0);
	EXPECT_EQ(restoredHero->getBufferSpellPoints(), 9);
}

TEST_F(SpellPointCapacityWizardTest, NewTurnPacketAppliesDailyMysticismRecoveryToNormalPoolOnly)
{
	setKnowledge(attackerSideHero, 100);
	setNormal(attackerSideHero, 40);
	grantBuffer(attackerSideHero, 11);
	ASSERT_TRUE(selectMysticismThroughLegalOffer(attackerSideHero));

	NewTurnRecordingGameServer dayStartServer;
	dayStartServer.gameState = gameState();
	CGameHandler dayStartHandler(dayStartServer, gameState());
	dayStartHandler.onNewTurn();

	ASSERT_TRUE(dayStartServer.lastNewTurn.has_value());
	const auto mana = std::find_if(dayStartServer.lastNewTurn->heroesMana.begin(),
		dayStartServer.lastNewTurn->heroesMana.end(), [this](const auto & change)
		{
			return change.hid == attackerSideHero->id;
		});
	ASSERT_NE(mana, dayStartServer.lastNewTurn->heroesMana.end());
	EXPECT_EQ(mana->operation, SetMana::Operation::SET_NORMAL);
	EXPECT_EQ(mana->amount, 50);
	expectPools(attackerSideHero, 50, 11, 100);
}

TEST_F(SpellPointCapacityWizardTest, MeditationUsesExactMovementThresholdAddsToBaselineAndCapsMissingNormal)
{
	setKnowledge(attackerSideHero, 100);
	setNormal(attackerSideHero, 0);
	grantBuffer(attackerSideHero, 23);
	ASSERT_TRUE(advanceWisdomAndSelectPerkThroughLegalOffers(attackerSideHero,
		INTELLIGENCE_PERK_ID, MEDITATION_PERK_ID));
	ASSERT_EQ(attackerSideHero->manaLimit(), 130);

	// The optional prior limit is the same snapshot used for heroes whose
	// Movement maximum changes as the tavern pool expires daily bonuses.
	setMovement(attackerSideHero, 49);
	EXPECT_EQ(attackerSideHero->getManaNewTurn(true, 200), 1);
	setMovement(attackerSideHero, 50);
	EXPECT_EQ(attackerSideHero->getManaNewTurn(true, 200), 20); // 1 base + floor(130 * 15%).
	expectPools(attackerSideHero, 0, 23, 130); // Forecasting changes neither pool.

	setNormal(attackerSideHero, 125);
	EXPECT_EQ(attackerSideHero->getManaNewTurn(true, 200), 130);
	expectPools(attackerSideHero, 125, 23, 130); // Only missing Normal is restored; Buffer is untouched.
}

TEST_F(SpellPointCapacityWizardTest, NewTurnPacketExcludesInitialDayAndAddsMeditationAfterMysticismAndStrongerRegen)
{
	setKnowledge(attackerSideHero, 100);
	setNormal(attackerSideHero, 0);
	grantBuffer(attackerSideHero, 11);
	ASSERT_TRUE(advanceWisdomAndSelectPerkThroughLegalOffers(attackerSideHero,
		MYSTICISM_PERK_ID, MEDITATION_PERK_ID));
	ASSERT_EQ(attackerSideHero->manaRegain(), 10);

	const int movementLimit = attackerSideHero->movementPointsLimit();
	ASSERT_GT(movementLimit, 0);
	const int atLeastQuarterMovement = (movementLimit + 3) / 4;
	setMovement(attackerSideHero, atLeastQuarterMovement);
	EXPECT_EQ(attackerSideHero->getManaNewTurn(true), 25); // Completed-day forecast includes both perks.

	NewTurnRecordingGameServer dayStartServer;
	dayStartServer.gameState = gameState();
	CGameHandler dayStartHandler(dayStartServer, gameState());
	ASSERT_EQ(gameState()->day, 0u);
	dayStartHandler.onNewTurn();
	ASSERT_TRUE(dayStartServer.lastNewTurn.has_value());
	ASSERT_EQ(dayStartServer.lastNewTurn->day, 1u);
	auto findMana = [this](const NewTurn & turn)
	{
		return std::find_if(turn.heroesMana.begin(), turn.heroesMana.end(), [this](const auto & change)
		{
			return change.hid == attackerSideHero->id;
		});
	};
	const auto firstDayMana = findMana(*dayStartServer.lastNewTurn);
	ASSERT_NE(firstDayMana, dayStartServer.lastNewTurn->heroesMana.end());
	EXPECT_EQ(firstDayMana->amount, 10); // Day zero has no completed Movement day for Meditation.
	EXPECT_EQ(firstDayMana->operation, SetMana::Operation::SET_NORMAL);

	setNormal(attackerSideHero, 0);
	setMovement(attackerSideHero, atLeastQuarterMovement);
	auto strongerRegeneration = std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::MANA_REGENERATION, BonusSource::OTHER, 25, BonusSourceID());
	attackerSideHero->addNewBonus(strongerRegeneration);
	ASSERT_EQ(attackerSideHero->manaRegain(), 26);
	dayStartHandler.onNewTurn();
	ASSERT_TRUE(dayStartServer.lastNewTurn.has_value());
	ASSERT_EQ(dayStartServer.lastNewTurn->day, 2u);
	const auto secondDayMana = findMana(*dayStartServer.lastNewTurn);
	ASSERT_NE(secondDayMana, dayStartServer.lastNewTurn->heroesMana.end());
	EXPECT_EQ(secondDayMana->amount, 41); // Previous Movement qualifies before day-start refresh.
	EXPECT_EQ(secondDayMana->operation, SetMana::Operation::SET_NORMAL);
	expectPools(attackerSideHero, 41, 11, 100);
	EXPECT_EQ(attackerSideHero->movementPointsRemaining(), attackerSideHero->movementPointsLimit());
}

TEST_F(SpellPointCapacityWizardTest, AdvancedMeditationSelectionSurvivesSaveAndRankLossDisablesIt)
{
	setKnowledge(attackerSideHero, 100);
	setNormal(attackerSideHero, 0);
	grantBuffer(attackerSideHero, 11);
	ASSERT_TRUE(advanceWisdomAndSelectPerkThroughLegalOffers(attackerSideHero,
		INTELLIGENCE_PERK_ID, MEDITATION_PERK_ID));
	setMovement(attackerSideHero, 50);

	const auto saved = gameState()->saveToMemory();
	CGameState restored;
	restored.preInit(LIBRARY);
	restored.loadFromMemory(saved);
	const auto * restoredHero = restored.getHero(attackerSideHero->id);
	ASSERT_NE(restoredHero, nullptr);
	EXPECT_TRUE(restoredHero->hasActivePerk(WISDOM_SKILL_ID, MEDITATION_PERK_ID));
	EXPECT_EQ(restoredHero->getManaNewTurn(true, 200), 20);
	EXPECT_EQ(restoredHero->getNormalSpellPoints(), 0);
	EXPECT_EQ(restoredHero->getBufferSpellPoints(), 11);

	setWisdomRank(attackerSideHero, MasteryLevel::BASIC);
	EXPECT_EQ(attackerSideHero->getManaNewTurn(true, 200), 1); // The saved choice needs current Advanced Wisdom.
	expectPools(attackerSideHero, 0, 11, 130);
}

TEST_F(SpellPointCapacityTest, TavernPoolRecoveryUsesPreExpiryMovementLimitBeforeRefreshingPoints)
{
	const int wisdomId = SecondarySkill::decode(WISDOM_SKILL_ID);
	ASSERT_GE(wisdomId, 0);
	const auto wisdom = SecondarySkill(wisdomId);
	HeroTypeID pooledWisdomHero = HeroTypeID::NONE;
	for(const auto heroType : gameState()->getMap().getHeroesInPool())
	{
		auto * candidate = gameState()->getMap().tryGetFromHeroPool(heroType);
		if(candidate && (candidate->getSecSkillLevel(wisdom) > 0 || candidate->canLearnSkill(wisdom)))
		{
			pooledWisdomHero = heroType;
			break;
		}
	}
	ASSERT_NE(pooledWisdomHero, HeroTypeID::NONE);
	auto * pooledHero = gameState()->getMap().tryGetFromHeroPool(pooledWisdomHero);
	ASSERT_NE(pooledHero, nullptr);

	// Pool heroes are not map objects, so build both choices from their own
	// legal offers and apply those selected candidates directly to that pool entry.
	pooledHero->setSecSkillLevel(wisdom, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	ASSERT_TRUE(selectWisdomPerkFromPoolOffer(pooledHero, INTELLIGENCE_PERK_ID));
	pooledHero->setSecSkillLevel(wisdom, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
	ASSERT_TRUE(selectWisdomPerkFromPoolOffer(pooledHero, MEDITATION_PERK_ID));
	pooledHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
	pooledHero->initializeSpellPoints(0, 17);
	ASSERT_EQ(pooledHero->manaLimit(), 130);

	const int unmodifiedMovementLimit = pooledHero->movementPointsLimit();
	ASSERT_GT(unmodifiedMovementLimit, 0);
	const auto landMovement = BonusSubtypeID(BonusCustomSubtype::heroMovementLand);
	const auto addExpiringMovementPenalty = [pooledHero, landMovement]()
	{
		auto penalty = std::make_shared<Bonus>(BonusDuration::ONE_DAY, BonusType::MOVEMENT,
			BonusSource::OTHER, -50, BonusSourceID(), landMovement, BonusValueType::PERCENT_TO_BASE);
		pooledHero->addNewBonus(penalty);
	};

	addExpiringMovementPenalty();
	const int previousMovementLimit = pooledHero->movementPointsLimit();
	ASSERT_GT(previousMovementLimit, 0);
	ASSERT_LT(previousMovementLimit, unmodifiedMovementLimit);
	const int exactQuarter = (previousMovementLimit + 3) / 4;
	ASSERT_LT(exactQuarter, (unmodifiedMovementLimit + 3) / 4);
	pooledHero->setMovementPoints(exactQuarter);

	CSimpleArmy noArmy;
	gameState()->heroesPool->setHeroForPlayer(PlayerColor(0), TavernHeroSlot::RANDOM,
		pooledWisdomHero, noArmy, TavernSlotRole::SINGLE_UNIT, false);
	gameState()->heroesPool->onNewDay(true);
	EXPECT_EQ(pooledHero->getNormalSpellPoints(), 20); // Prior max qualifies after its one-day penalty expires.
	EXPECT_EQ(pooledHero->getBufferSpellPoints(), 17);
	EXPECT_EQ(pooledHero->movementPointsLimit(), unmodifiedMovementLimit);
	EXPECT_EQ(pooledHero->movementPointsRemaining(), unmodifiedMovementLimit);

	// If the pool refreshed Movement before calculating Mana, a spent-out
	// hero would incorrectly satisfy Meditation against the freshly filled pool.
	pooledHero->setNormalSpellPoints(0);
	addExpiringMovementPenalty();
	pooledHero->setMovementPoints(0);
	gameState()->heroesPool->onNewDay(true);
	EXPECT_EQ(pooledHero->getNormalSpellPoints(), 1);
	EXPECT_EQ(pooledHero->getBufferSpellPoints(), 17);
	EXPECT_EQ(pooledHero->movementPointsLimit(), unmodifiedMovementLimit);
	EXPECT_EQ(pooledHero->movementPointsRemaining(), unmodifiedMovementLimit);
}

TEST_F(SpellPointCapacityPlannedMeditationTest, SavedPlannedMeditationCannotBeOfferedOrApplied)
{
	setKnowledge(attackerSideHero, 100);
	setNormal(attackerSideHero, 0);
	grantBuffer(attackerSideHero, 9);
	setWisdomRank(attackerSideHero, MasteryLevel::BASIC);
	ASSERT_TRUE(selectWisdomPerkThroughLegalOffer(attackerSideHero, INTELLIGENCE_PERK_ID));
	const int wisdomId = SecondarySkill::decode(WISDOM_SKILL_ID);
	ASSERT_GE(wisdomId, 0);
	const auto wisdom = SecondarySkill(wisdomId);
	gameHandler->levelUpHero(attackerSideHero, wisdom, false);
	ASSERT_EQ(attackerSideHero->getSecSkillLevel(wisdom), MasteryLevel::ADVANCED);
	const auto rankLookup = [this](const std::string & skillId)
	{
		return attackerSideHero->getPerkSkillRank(skillId);
	};
	for(uint64_t seed = 0; seed < 128; ++seed)
	{
		const auto offer = attackerSideHero->getPerkState().prepareOffer(rankLookup, seed, attackerSideHero->getPerkOfferWeightContext());
		EXPECT_TRUE(std::none_of(offer.begin(), offer.end(), [](const auto & candidate)
		{
			return candidate.selection.perkId == MEDITATION_PERK_ID;
		}));
	}
	EXPECT_THROW(attackerSideHero->applyPerkSelection({WISDOM_SKILL_ID, MEDITATION_PERK_ID}), std::runtime_error);
	setMovement(attackerSideHero, 50);

	const auto saved = gameState()->saveToMemory();
	CGameState restored;
	restored.preInit(LIBRARY);
	restored.loadFromMemory(saved);
	const auto * restoredHero = restored.getHero(attackerSideHero->id);
	ASSERT_NE(restoredHero, nullptr);
	EXPECT_FALSE(restoredHero->hasActivePerk(WISDOM_SKILL_ID, MEDITATION_PERK_ID));
	EXPECT_EQ(restoredHero->getManaNewTurn(true, 200), 1);
	EXPECT_EQ(restoredHero->getNormalSpellPoints(), 0);
	EXPECT_EQ(restoredHero->getBufferSpellPoints(), 9);
}

TEST_F(SpellPointCapacityMageGuildTest, MageGuildFullRefillTakesPrecedenceOverMysticism)
{
	const auto towns = findAll<CGTownInstance>();
	ASSERT_EQ(towns.size(), 1u);
	auto * town = towns.front();
	town->addBuilding(BuildingID::MAGES_GUILD_1);
	defenderSideHero->setVisitedTown(town, false);
	setKnowledge(defenderSideHero, 100);
	setNormal(defenderSideHero, 2);
	ASSERT_TRUE(selectMysticismThroughLegalOffer(defenderSideHero));

	EXPECT_EQ(defenderSideHero->getManaNewTurn(), 100);
	expectPools(defenderSideHero, 2, 0, 100);
}

TEST_F(SpellPointCapacityTest, AcknowledgementsDoNotScheduleCapacityReconciliation)
{
	GameStatePackVisitor visitor(*gameState());
	PackageReceived received;
	received.visit(visitor);
	EXPECT_FALSE(visitor.needsSpellPointReconciliation());
	PackageApplied applied;
	applied.visit(visitor);
	EXPECT_FALSE(visitor.needsSpellPointReconciliation());
	visitor.reconcileSpellPointCapacity();

	// Exercise the real apply path, including the recruitment pool. Record rather
	// than assert wall-clock timing so loaded CI hosts do not cause flaky tests.
	const auto start = std::chrono::steady_clock::now();
	for(int i = 0; i < 1000; ++i)
		gameState()->apply(received);
	const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
		std::chrono::steady_clock::now() - start).count();
	RecordProperty("acknowledgement_1000_us", std::to_string(elapsed));
}

TEST_F(SpellPointCapacityTest, KnowledgeChangeSchedulesDeferredCapacityReconciliation)
{
	setKnowledge(attackerSideHero, 100);
	setNormal(attackerSideHero, 100);
	grantBuffer(attackerSideHero, 50);
	GameStatePackVisitor visitor(*gameState());
	SetPrimarySkill change;
	change.id = attackerSideHero->id;
	change.which = PrimarySkill::KNOWLEDGE;
	change.val = 80;
	change.mode = ChangeValueMode::ABSOLUTE;
	change.visit(visitor);
	EXPECT_TRUE(visitor.needsSpellPointReconciliation());
	EXPECT_EQ(attackerSideHero->getNormalSpellPoints(), 100);
	visitor.reconcileSpellPointCapacity();
	expectPools(attackerSideHero, 80, 50, 80);
}

TEST_F(SpellPointCapacityTest, MovementAndCapacityIncreasesDoNotScheduleReconciliation)
{
	setKnowledge(attackerSideHero, 100);
	setNormal(attackerSideHero, 80);
	grantBuffer(attackerSideHero, 50);
	GameStatePackVisitor visitor(*gameState());
	SetMovePoints movement;
	movement.hid = attackerSideHero->id;
	movement.val = 100;
	movement.visit(visitor);
	EXPECT_FALSE(visitor.needsSpellPointReconciliation());
	SetPrimarySkill increase;
	increase.id = attackerSideHero->id;
	increase.which = PrimarySkill::KNOWLEDGE;
	increase.val = 120;
	increase.mode = ChangeValueMode::ABSOLUTE;
	increase.visit(visitor);
	EXPECT_FALSE(visitor.needsSpellPointReconciliation());
	visitor.reconcileSpellPointCapacity();
	expectPools(attackerSideHero, 80, 50, 120);
}

TEST_F(SpellPointCapacityTest, InvalidManaMutationPayloadsAreRejectedWithoutChangingEitherPool)
{
	setKnowledge(attackerSideHero, 80);
	setNormal(attackerSideHero, 40);
	grantBuffer(attackerSideHero, 30);
	expectPools(attackerSideHero, 40, 30, 80);

	SetMana legacyWithTypedAmount;
	legacyWithTypedAmount.hid = attackerSideHero->id;
	legacyWithTypedAmount.amount = 1;
	EXPECT_THROW(gameState()->apply(legacyWithTypedAmount), std::runtime_error);
	expectPools(attackerSideHero, 40, 30, 80);

	SetMana invalidMode;
	invalidMode.hid = attackerSideHero->id;
	invalidMode.mode = static_cast<ChangeValueMode>(99);
	EXPECT_THROW(gameState()->apply(invalidMode), std::runtime_error);
	expectPools(attackerSideHero, 40, 30, 80);
}

TEST_F(ArcanePreparationCapacityTest, StartSpendSaveAndResultsUseExistingTemporaryBufferLifecycle)
{
	ASSERT_NO_FATAL_FAILURE(activateIntelligence());
	setKnowledge(attackerSideHero, 100);
	attackerSideHero->initializeSpellPoints(5, 2);
	EXPECT_EQ(attackerSideHero->manaLimit(), 130);
	ASSERT_EQ(attackerSideHero->getArcanePreparationBufferBonus(), 9);
	startBattle();
	expectPools(attackerSideHero, 5, 11, 130);
	EXPECT_EQ(battle()->getSide(BattleSide::ATTACKER).temporaryBufferRemaining, 9);
	gameHandler->grantBufferSpellPoints(attackerSideHero->id, 6);
	gameHandler->spendSpellPoints(attackerSideHero->id, 4);
	expectPools(attackerSideHero, 5, 13, 130);
	EXPECT_EQ(battle()->getSide(BattleSide::ATTACKER).temporaryBufferRemaining, 5);
	const auto savedBattle = CMemorySerializer::deepCopy(*battle(), gameState().get());
	EXPECT_EQ(savedBattle->getSide(BattleSide::ATTACKER).temporaryBufferRemaining, 5);
	BattleResultsApplied completed;
	completed.battleID = BattleID(0);
	gameState()->apply(completed);
	expectPools(attackerSideHero, 5, 8, 130);
}

TEST_F(ArcanePreparationCapacityTest, FullTemporarySpendPreservesPermanentBufferAtResults)
{
	ASSERT_NO_FATAL_FAILURE(activateIntelligence());
	setKnowledge(attackerSideHero, 100);
	attackerSideHero->initializeSpellPoints(5, 2);
	startBattle();
	gameHandler->grantBufferSpellPoints(attackerSideHero->id, 6);
	gameHandler->spendSpellPoints(attackerSideHero->id, 9);
	expectPools(attackerSideHero, 5, 8, 130);
	EXPECT_EQ(battle()->getSide(BattleSide::ATTACKER).temporaryBufferRemaining, 0);
	BattleResultsApplied completed;
	completed.battleID = BattleID(0);
	gameState()->apply(completed);
	expectPools(attackerSideHero, 5, 8, 130);
}

TEST_F(ArcanePreparationCapacityTest, InactiveIntelligenceAndArtifactManaModifiersCannotCreateSpecialtyBuffer)
{
	setKnowledge(attackerSideHero, 100);
	EXPECT_EQ(attackerSideHero->getArcanePreparationBufferBonus(), 0);
	ASSERT_NO_FATAL_FAILURE(activateIntelligence());
	EXPECT_EQ(attackerSideHero->getArcanePreparationBufferBonus(), 9);
	Bonus capacity;
	capacity.type = BonusType::MANA_PER_KNOWLEDGE_PERCENTAGE;
	capacity.source = BonusSource::ARTIFACT;
	capacity.val = 100;
	GiveBonus grant(GiveBonus::ETarget::OBJECT, attackerSideHero->id, capacity);
	gameHandler->sendAndApply(grant);
	EXPECT_EQ(attackerSideHero->getArcanePreparationBufferBonus(), 9);
	setKnowledge(attackerSideHero, 0);
	EXPECT_EQ(attackerSideHero->getArcanePreparationBufferBonus(), 0);
}

TEST_F(ArcanePreparationCapacityTest, CancellationRestoresOriginalPoolsAndNextBattleGetsOneFreshGrant)
{
	ASSERT_NO_FATAL_FAILURE(activateIntelligence());
	setKnowledge(attackerSideHero, 100);
	attackerSideHero->initializeSpellPoints(5, 2);
	startBattle();
	auto freshDescriptor = battleStartFixture::snapshot(*battle(), gameState().get());
	gameHandler->spendSpellPoints(attackerSideHero->id, 4);
	BattleCancelled cancelled;
	cancelled.battleID = BattleID(0);
	gameState()->apply(cancelled);
	expectPools(attackerSideHero, 5, 2, 130);
	BattleStart next;
	const BattleID nextID = gameState()->nextBattleID;
	next.battleID = nextID;
	next.info = std::move(freshDescriptor);
	gameHandler->sendAndApply(next);
	expectPools(attackerSideHero, 5, 11, 130);
	const auto * freshBattle = gameState()->getBattle(nextID);
	ASSERT_NE(freshBattle, nullptr);
	EXPECT_EQ(freshBattle->getSide(BattleSide::ATTACKER).temporaryBufferRemaining, 9);
}

TEST_F(ArcanePreparationAbsentCapacityTest, OlderCapturedAbsenceDoesNotGrantTemporaryBuffer)
{
	ASSERT_NO_FATAL_FAILURE(activateIntelligence());
	setKnowledge(attackerSideHero, 100);
	attackerSideHero->initializeSpellPoints(5, 2);
	EXPECT_EQ(attackerSideHero->getArcanePreparationBufferBonus(), 0);
	startBattle();
	expectPools(attackerSideHero, 5, 2, 130);
	EXPECT_EQ(battle()->getSide(BattleSide::ATTACKER).temporaryBufferRemaining, 0);
}

TEST_F(ArcanePreparationCapacityTest, ExistingBufferNearIntegerLimitBoundsTheBattleGrant)
{
	ASSERT_NO_FATAL_FAILURE(activateIntelligence());
	setKnowledge(attackerSideHero, 100);
	attackerSideHero->initializeSpellPoints(5, std::numeric_limits<int32_t>::max() - 2);
	startBattle();
	EXPECT_EQ(attackerSideHero->getNormalSpellPoints(), 5);
	EXPECT_EQ(attackerSideHero->getBufferSpellPoints(), std::numeric_limits<int32_t>::max());
	EXPECT_EQ(battle()->getSide(BattleSide::ATTACKER).temporaryBufferRemaining, 2);
}

TEST_F(SpellPointCapacityTest, SnapshotRestoreDuringBattleIsRejectedAtomically)
{
	setKnowledge(attackerSideHero, 10);
	attackerSideHero->initializeSpellPoints(5, 2);

	Bonus combatMana;
	combatMana.type = BonusType::COMBAT_MANA_BONUS;
	combatMana.val = 8;
	GiveBonus grantCombatMana(GiveBonus::ETarget::OBJECT, attackerSideHero->id, combatMana);
	gameHandler->sendAndApply(grantCombatMana);
	startBattle();

	auto & attackerSide = battle()->getSide(BattleSide::ATTACKER);
	ASSERT_EQ(attackerSide.temporaryBufferRemaining, 8);
	expectPools(attackerSideHero, 5, 10, 10);

	SetMana snapshot(attackerSideHero->id, SetMana::Operation::RESTORE_SNAPSHOT, 2, 7);
	EXPECT_THROW(gameState()->apply(snapshot), std::runtime_error);

	expectPools(attackerSideHero, 5, 10, 10);
	EXPECT_EQ(attackerSide.temporaryBufferRemaining, 8);
}

TEST_F(SpellPointCapacityTest, RejectedManaDrainLeavesPoolsTemporaryProvenanceAndTriggerFlagUnchanged)
{
	setKnowledge(attackerSideHero, 10);
	setNormal(attackerSideHero, 5);
	grantBuffer(attackerSideHero, 2);

	Bonus combatMana;
	combatMana.type = BonusType::COMBAT_MANA_BONUS;
	combatMana.val = 8;
	GiveBonus grantCombatMana(GiveBonus::ETarget::OBJECT, attackerSideHero->id, combatMana);
	gameHandler->sendAndApply(grantCombatMana);
	startBattle();

	auto & attackerSide = battle()->getSide(BattleSide::ATTACKER);
	ASSERT_EQ(attackerSide.temporaryBufferRemaining, 8);
	ASSERT_EQ(attackerSideHero->getBufferSpellPoints(), 10);
	grantBuffer(attackerSideHero, 3);
	ASSERT_EQ(attackerSideHero->getBufferSpellPoints(), 13);
	beginCombat();

	ASSERT_NE(battle()->battleActiveUnit(), nullptr);
	const auto * triggerStack = battle()->battleGetStackByID(battle()->battleActiveUnit()->unitId());
	ASSERT_NE(triggerStack, nullptr);
	ASSERT_FALSE(triggerStack->drainedMana);
	for(const int amount : {-1, 19}) // negative and greater than all available Normal + Buffer
	{
		BattleTriggerEffect drain;
		drain.battleID = BattleID(0);
		drain.stackID = triggerStack->unitId();
		drain.effect = BonusType::MANA_DRAIN;
		drain.val = amount;
		drain.additionalInfo = static_cast<int>(attackerSideHero->id.getNum());
		EXPECT_THROW(gameState()->apply(drain), std::runtime_error) << amount;

		expectPools(attackerSideHero, 5, 13, 10);
		EXPECT_EQ(attackerSide.temporaryBufferRemaining, 8);
		EXPECT_FALSE(triggerStack->drainedMana);
	}
}

TEST_F(SpellPointCapacityTest, BattleStartRejectsInvalidSecondSideSnapshotBeforeMutationOrAttachment)
{
	setKnowledge(attackerSideHero, 80);
	setKnowledge(defenderSideHero, 80);
	setNormal(attackerSideHero, 30);
	setNormal(defenderSideHero, 25);
	grantBuffer(attackerSideHero, 40);
	grantBuffer(defenderSideHero, 50);
	const auto beforeBattle = gameState()->saveToMemory();

	startBattle();
	auto replica = std::make_shared<CGameState>();
	replica->preInit(LIBRARY);
	replica->loadFromMemory(beforeBattle);
	auto * replicaAttacker = replica->getHero(attackerSideHero->id);
	auto * replicaDefender = replica->getHero(defenderSideHero->id);
	ASSERT_NE(replicaAttacker, nullptr);
	ASSERT_NE(replicaDefender, nullptr);
	ASSERT_EQ(replicaAttacker->battle, nullptr);
	ASSERT_EQ(replicaDefender->battle, nullptr);
	expectPools(replicaAttacker, 30, 40, 80);
	expectPools(replicaDefender, 25, 50, 80);

	BattleStart malformed;
	malformed.battleID = BattleID(0);
	malformed.info = battleStartFixture::snapshot(*battle(), replica.get());
	auto & attackerSnapshot = malformed.info->getSide(BattleSide::ATTACKER);
	attackerSnapshot.initialNormalSpellPoints = 11;
	attackerSnapshot.initialBufferSpellPoints = 7;
	auto & defenderSnapshot = malformed.info->getSide(BattleSide::DEFENDER);
	RecordingGameServer restoredServer;
	restoredServer.gameState = replica;
	auto handler = std::make_shared<CGameHandler>(restoredServer, replica);
	const std::array<std::array<int32_t, 3>, 4> invalidPools = {{
		{{-1, 50, 0}}, {{25, -1, 0}}, {{25, 50, -1}},
		{{25, 50, std::numeric_limits<int32_t>::max() - 49}}
	}};
	for(const auto & pools : invalidPools)
	{
		defenderSnapshot.initialNormalSpellPoints = pools[0];
		defenderSnapshot.initialBufferSpellPoints = pools[1];
		defenderSnapshot.temporaryBufferRemaining = pools[2];
		EXPECT_THROW(handler->sendAndApply(malformed), std::runtime_error);
		EXPECT_TRUE(replica->currentBattles.empty());
		EXPECT_EQ(replicaAttacker->battle, nullptr);
		EXPECT_EQ(replicaDefender->battle, nullptr);
		expectPools(replicaAttacker, 30, 40, 80);
		expectPools(replicaDefender, 25, 50, 80);
	}
}

TEST_F(SpellPointCapacityTest, HeroSerializationPreservesDistinctNormalAndBufferPools)
{
	setKnowledge(attackerSideHero, 100);
	attackerSideHero->initializeSpellPoints(30, 50);
	const auto restored = CMemorySerializer::deepCopy(*attackerSideHero, gameState().get());
	ASSERT_NE(restored, nullptr);
	EXPECT_TRUE(restored->areSpellPointsInitialized());
	EXPECT_EQ(restored->getNormalSpellPoints(), 30);
	EXPECT_EQ(restored->getBufferSpellPoints(), 50);
	EXPECT_EQ(restored->getManaAvailable(), 80);
}

TEST_F(SpellPointCapacityTest, LegacyHeroSerializationDoesNotManufactureBuffer)
{
	// A historical codec fixture must not inherit the current world's captured
	// spell/perk policies. Keep the current-world refusal as a separate control.
	CMemorySerializer current;
	current.oser.version = ESerializationVersion::NEW_HORIZONS_RANDOM_ARTIFACT_POOL;
	EXPECT_THROW(current.oser & attackerSideHero, std::runtime_error);
	auto legacyHero = std::make_unique<CGHeroInstance>(nullptr);
	legacyHero->setHeroType(attackerSideHero->getHeroTypeID());
	legacyHero->pushPrimSkill(PrimarySkill::KNOWLEDGE, 100);
	ASSERT_TRUE(legacyHero->getMagicRules().isNull());
	ASSERT_TRUE(legacyHero->getPerkState().rules.isNull());
	ASSERT_TRUE(legacyHero->getPrimaryGrowthRules().isNull());
	legacyHero->initializeSpellPoints(80, 0);
	CMemorySerializer memory;
	memory.oser.version = ESerializationVersion::NEW_HORIZONS_RANDOM_ARTIFACT_POOL;
	memory.iser.version = ESerializationVersion::NEW_HORIZONS_RANDOM_ARTIFACT_POOL;
	memory.oser & legacyHero;
	std::unique_ptr<CGHeroInstance> restored;
	memory.iser & restored;
	ASSERT_NE(restored, nullptr);
	EXPECT_TRUE(restored->areSpellPointsInitialized());
	EXPECT_EQ(restored->getNormalSpellPoints(), 80);
	EXPECT_EQ(restored->getBufferSpellPoints(), 0);
}

TEST_F(SpellPointCapacityTest, LegacyHeroSerializationRejectsLossOfBuffer)
{
	auto legacyHero = std::make_unique<CGHeroInstance>(nullptr);
	legacyHero->setHeroType(attackerSideHero->getHeroTypeID());
	legacyHero->pushPrimSkill(PrimarySkill::KNOWLEDGE, 100);
	ASSERT_TRUE(legacyHero->getMagicRules().isNull());
	ASSERT_TRUE(legacyHero->getPerkState().rules.isNull());
	ASSERT_TRUE(legacyHero->getPrimaryGrowthRules().isNull());
	legacyHero->initializeSpellPoints(30, 50);
	CMemorySerializer memory;
	memory.oser.version = ESerializationVersion::NEW_HORIZONS_RANDOM_ARTIFACT_POOL;
	try
	{
		memory.oser & legacyHero;
		FAIL() << "An older format must not discard Buffer Spell Points";
	}
	catch(const std::runtime_error & error)
	{
		EXPECT_STREQ(error.what(), "Cannot discard Buffer Spell Points in an older save format");
	}
	EXPECT_EQ(legacyHero->getNormalSpellPoints(), 30);
	EXPECT_EQ(legacyHero->getBufferSpellPoints(), 50);
}

TEST_F(SpellPointCapacityTest, BufferGrantedDuringCombatPreservesNormalAndTemporaryProvenance)
{
	setKnowledge(attackerSideHero, 10);
	attackerSideHero->initializeSpellPoints(5, 2);
	Bonus combatMana;
	combatMana.type = BonusType::COMBAT_MANA_BONUS;
	combatMana.val = 8;
	GiveBonus grant(GiveBonus::ETarget::OBJECT, attackerSideHero->id, combatMana);
	gameHandler->sendAndApply(grant);
	startBattle();
	auto & side = battle()->getSide(BattleSide::ATTACKER);
	ASSERT_EQ(side.temporaryBufferRemaining, 8);
	gameHandler->grantBufferSpellPoints(attackerSideHero->id, 6);
	expectPools(attackerSideHero, 5, 16, 10);
	EXPECT_EQ(side.temporaryBufferRemaining, 8);
	gameHandler->spendSpellPoints(attackerSideHero->id, 9);
	expectPools(attackerSideHero, 5, 7, 10);
	EXPECT_EQ(side.temporaryBufferRemaining, 0);
	const auto restored = CMemorySerializer::deepCopy(*battle(), gameState().get());
	ASSERT_NE(restored, nullptr);
	EXPECT_EQ(restored->getSide(BattleSide::ATTACKER).temporaryBufferRemaining, 0);
}

TEST_F(SpellPointCapacityTest, BattleSerializationPreservesInitialPoolsAndTemporaryBuffer)
{
	setKnowledge(attackerSideHero, 100);
	attackerSideHero->initializeSpellPoints(30, 50);
	Bonus combatMana;
	combatMana.type = BonusType::COMBAT_MANA_BONUS;
	combatMana.val = 8;
	GiveBonus grant(GiveBonus::ETarget::OBJECT, attackerSideHero->id, combatMana);
	gameHandler->sendAndApply(grant);
	startBattle();
	SetMana spend(attackerSideHero->id, SetMana::Operation::SPEND, 3);
	gameState()->apply(spend);
	const auto restored = CMemorySerializer::deepCopy(*battle(), gameState().get());
	ASSERT_NE(restored, nullptr);
	const auto & side = restored->getSide(BattleSide::ATTACKER);
	EXPECT_EQ(side.initialNormalSpellPoints, 30);
	EXPECT_EQ(side.initialBufferSpellPoints, 50);
	EXPECT_EQ(side.temporaryBufferRemaining, 5);
}

TEST_F(BottomlessMindCapacityTest, ActualAndraAddsFlatLevelCapacityWithoutRefillOrForeignBenefit)
{
	EXPECT_EQ(attackerSideHero->getSecSkillLevel(SecondarySkill(SecondarySkill::decode(WISDOM_SKILL_ID))), MasteryLevel::BASIC);
	EXPECT_NE(attackerSideHero->getArt(ArtifactPosition::SPELLBOOK), nullptr);
	EXPECT_TRUE(attackerSideHero->spellbookContainsSpell(SpellID::DISPEL));
	setKnowledge(attackerSideHero, 100);
	setNormal(attackerSideHero, 10);
	grantBuffer(attackerSideHero, 17);
	const auto rngBytes = [&]()
	{
		CMemorySerializer wire;
		wire.oser & *gameHandler->randomizer;
		return wire.extractBuffer();
	};
	const auto beforeQueries = rngBytes();
	for(const auto level : {1u, 5u, 20u})
	{
		SCOPED_TRACE(level);
		attackerSideHero->level = level;
		EXPECT_EQ(attackerSideHero->getBottomlessMindManaBonus(), 2 * level);
		expectPools(attackerSideHero, 10, 17, 130 + 2 * level);
	}
	EXPECT_EQ(rngBytes(), beforeQueries);
	ASSERT_TRUE(defenderSideHero->hasActivePerk(WISDOM_SKILL_ID, INTELLIGENCE_PERK_ID));
	EXPECT_EQ(defenderSideHero->getBottomlessMindManaBonus(), 0);
	// This follows the existing capacity contract, not multiplicative artifact
	// mana scaling: the flat specialty remains +40 at L20.
	Bonus artifact;
	artifact.type = BonusType::MANA_PER_KNOWLEDGE_PERCENTAGE;
	artifact.source = BonusSource::ARTIFACT;
	artifact.val = 100;
	GiveBonus grant(GiveBonus::ETarget::OBJECT, attackerSideHero->id, artifact);
	gameHandler->sendAndApply(grant);
	expectPools(attackerSideHero, 10, 17, 170);
	setNormal(attackerSideHero, 170);
	setWisdomActive(false);
	EXPECT_EQ(attackerSideHero->getBottomlessMindManaBonus(), 0);
	expectPools(attackerSideHero, 100, 17, 100);
	setWisdomActive(true);
	expectPools(attackerSideHero, 100, 17, 170); // reactivation does not refill.
}

TEST_F(BottomlessMindCapacityTest, ReservoirIsUnscaledAndWorldCodecKeepsPolicyLevelAndPools)
{
	const SecondarySkill wisdom(SecondarySkill::decode(WISDOM_SKILL_ID));
	gameHandler->levelUpHero(attackerSideHero, wisdom, false);
	ASSERT_TRUE(selectWisdomPerkThroughLegalOffer(attackerSideHero, DEEP_KNOWLEDGE_PERK_ID));
	gameHandler->levelUpHero(attackerSideHero, wisdom, false);
	ASSERT_TRUE(selectWisdomPerkThroughLegalOffer(attackerSideHero, ARCANE_RESERVOIR_PERK_ID));
	attackerSideHero->level = 20;
	setKnowledge(attackerSideHero, 100);
	setNormal(attackerSideHero, 10);
	grantBuffer(attackerSideHero, 17);
	expectPools(attackerSideHero, 10, 17, 195); // 130 + unchanged25 + flat40.
	const auto policy = attackerSideHero->getPrimaryGrowthRules();
	const auto bytes = gameState()->saveToMemory();
	CGameState restored;
	restored.preInit(LIBRARY);
	restored.loadFromMemory(bytes);
	const auto * hero = restored.getHero(attackerSideHero->id);
	ASSERT_NE(hero, nullptr);
	EXPECT_EQ(hero->getPrimaryGrowthRules(), policy);
	EXPECT_EQ(hero->getBottomlessMindManaBonus(), 40);
	expectPools(hero, 10, 17, 195);
	const auto reject = [](auto & value)
	{
		CMemorySerializer previous;
		previous.oser.version = ESerializationVersion::NEW_HORIZONS_FORTUNE_CHILD_AND_ARCANE_PREPARATION;
		EXPECT_THROW(value.serialize(previous.oser), std::runtime_error);
		EXPECT_TRUE(previous.extractBuffer().empty());
	};
	reject(*attackerSideHero);
	GameSettings settings;
	settings.addOverride(EGameSettings::HEROES_NEW_HORIZONS, gameState()->getHeroDevelopmentRules());
	reject(settings);
	reject(gameState()->getMap());
	reject(*gameState());
	LobbyStartGame lobby;
	lobby.initializedStartInfo = std::make_shared<StartInfo>(*gameState()->getStartInfo());
	lobby.initializedGameState = gameState();
	reject(lobby);
}

TEST_F(BottomlessMindAbsentCapacityTest, PreviousCapturedAbsenceStaysInertThroughPreviousWorldCodec)
{
	attackerSideHero->level = 20;
	setKnowledge(attackerSideHero, 100);
	setNormal(attackerSideHero, 10);
	grantBuffer(attackerSideHero, 17);
	EXPECT_EQ(attackerSideHero->getBottomlessMindManaBonus(), 0);
	expectPools(attackerSideHero, 10, 17, 130);
	CMemorySerializer wire;
	wire.oser.version = ESerializationVersion::NEW_HORIZONS_FORTUNE_CHILD_AND_ARCANE_PREPARATION;
	wire.iser.version = wire.oser.version;
	ASSERT_NO_THROW(wire.oser & *gameState());
	CGameState restored;
	restored.preInit(LIBRARY);
	wire.iser.loadingGamestate = true;
	wire.iser.cb = &restored;
	ASSERT_NO_THROW(wire.iser & restored);
	const auto * hero = restored.getHero(attackerSideHero->id);
	ASSERT_NE(hero, nullptr);
	EXPECT_FALSE(hero->getPrimaryGrowthRules().Struct().contains("bottomlessMindSpecialty"));
	EXPECT_EQ(hero->getBottomlessMindManaBonus(), 0);
	expectPools(hero, 10, 17, 130);
}
