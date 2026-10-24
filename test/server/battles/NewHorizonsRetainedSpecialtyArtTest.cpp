/*
 * NewHorizonsRetainedSpecialtyArtTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include "../../../client/windows/HeroSpecialtyPresentation.h"
#include "../../../lib/entities/hero/NewHorizonsSpecialtyArtwork.h"
#include "../../../lib/spells/NewHorizonsMagic.h"
#include "../../../lib/spells/NewHorizonsSpellAvailability.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/GameConstants.h"

namespace
{
const CHero & prototype(const char * key)
{
	return *HeroTypeID(HeroTypeID::decode(key)).toHeroType();
}

// Explicit detached metadata; CHero/Entity is noncopyable. Registered entities
// and shared bonus objects are read-only and outlive this local fixture.
class DetachedRetainedPrototype final : public CHero
{
	const CHero & source;
public:
	std::string identity;
	explicit DetachedRetainedPrototype(const CHero & original) : CHero(), source(original), identity(original.getJsonKey())
	{
		imageIndex = source.imageIndex;
		haveSpellBook = source.haveSpellBook;
		spells = source.spells;
		excludedSpells = source.excludedSpells;
		for(const auto & bonus : source.specialty)
			specialty.push_back(bonus);
	}
	std::string getJsonKey() const override { return identity; }
	HeroTypeID getId() const override { return source.getId(); }
	std::string getSpecialtyNameTranslated() const override { return source.getSpecialtyNameTranslated(); }
	std::string getSpecialtyDescriptionTranslated() const override { return source.getSpecialtyDescriptionTranslated(); }
};

void expectRetainedArt(const CHero & hero, const newHorizonsHeroes::SpecialtyPresentation & metadata,
	const char * stem)
{
	const std::string artwork = "newHorizons/heroSpecialties/" + std::string(stem);
	EXPECT_EQ(metadata.artwork, artwork);
	EXPECT_EQ(metadata.name, hero.getSpecialtyNameTranslated());
	EXPECT_EQ(metadata.description, hero.getSpecialtyDescriptionTranslated());
	EXPECT_FALSE(metadata.skill);
	EXPECT_FALSE(metadata.spell);
	EXPECT_FALSE(metadata.creature);
	const auto card = heroSpecialtyPresentation(hero, metadata);
	EXPECT_EQ(card.animation().getOriginalName(), artwork + "/44");
	EXPECT_EQ(card.animation(true).getOriginalName(), artwork + "/32");
	EXPECT_EQ(card.frame(), 0u);
}

void expectRetainedFallback(const CHero & hero, const newHorizonsHeroes::SpecialtyPresentation & metadata)
{
	EXPECT_FALSE(metadata.artwork);
	EXPECT_FALSE(metadata.skill);
	EXPECT_FALSE(metadata.spell);
	EXPECT_FALSE(metadata.creature);
	EXPECT_EQ(metadata.name, hero.getSpecialtyNameTranslated());
	EXPECT_EQ(metadata.description, hero.getSpecialtyDescriptionTranslated());
	const auto card = heroSpecialtyPresentation(hero, metadata);
	EXPECT_EQ(card.animation().getOriginalName(), "UN44");
	EXPECT_EQ(card.animation(true).getOriginalName(), "UN32");
	EXPECT_EQ(card.frame(), hero.imageIndex);
}
}

class NewHorizonsRetainedSpecialtyArtTest : public HeroCommandFixture
{
protected:
	bool historicalDevelopment = false;
	int magicMode = 0;
	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires New Horizons content";
	}
	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		if(historicalDevelopment)
		{
			loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, JsonNode());
			loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, JsonNode());
		}
		JsonNode magic(JsonPath::builtin("config/newHorizonsMagic"));
		if(magicMode == 1)
			magic = JsonNode();
		else if(magicMode == 2)
			magic["spells"].Struct().erase("new-horizons:masterChainLightning");
		else if(magicMode == 3)
			magic["spells"]["new-horizons:masterChainLightning"]["active"].Bool() = false;
		else if(magicMode == 4)
			magic["spells"]["new-horizons:masterChainLightning"]["heroAccess"].Bool() = false;
		magic.setOverrideFlag(true);
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, magic);
	}
	void prepare()
	{
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).playerActive(PlayerColor(0)).playerActive(PlayerColor(1))
			.hero({5, 5, 0}, HeroTypeID(HeroTypeID::decode("core:halon")), PlayerColor(0))
			.hero({6, 5, 0}, HeroTypeID(HeroTypeID::decode("core:solmyr")), PlayerColor(0))
			.hero({24, 24, 0}, HeroTypeID(HeroTypeID::decode("core:christian")), PlayerColor(1));
		startWithMap(std::move(builder));
	}
};

TEST_F(NewHorizonsRetainedSpecialtyArtTest, HalonCapturedAndDetachedKeepActualBonusTextAndState)
{
	prepare();
	const auto * hero = findHeroAt({5, 5, 0});
	ASSERT_NE(hero, nullptr);
	const auto before = hero->getExportedBonusList().toJsonNode();
	const auto capacity = newHorizonsMagic::metamagicCapacity(hero);
	expectRetainedArt(*hero->getHeroType(), hero->getSpecialtyPresentation(), "HalonMetamagicAdept");
	expectRetainedArt(*hero->getHeroType(), newHorizonsHeroes::resolveSpecialtyPresentation(
		*hero->getHeroType(), hero->getPrimaryGrowthRules(), hero->getMagicRules(), std::nullopt), "HalonMetamagicAdept");
	EXPECT_EQ(hero->getExportedBonusList().toJsonNode(), before);
	EXPECT_EQ(newHorizonsMagic::metamagicCapacity(hero), capacity);
}

TEST_F(NewHorizonsRetainedSpecialtyArtTest, HalonAbsentOrWrongCapturedBonusCannotUseInstalledPrototype)
{
	prepare();
	auto * hero = findHeroAt({5, 5, 0});
	ASSERT_NE(hero, nullptr);
	std::vector<std::shared_ptr<Bonus>> originals;
	for(const auto & bonus : hero->getExportedBonusList())
		if(bonus && bonus->type == BonusType::METAMAGIC_USES_PER_COMBAT && bonus->source == BonusSource::HERO_SPECIAL)
			originals.push_back(bonus);
	ASSERT_EQ(originals.size(), 1u);
	const auto original = originals.front();
	hero->removeBonus(original);
	expectRetainedFallback(*hero->getHeroType(), hero->getSpecialtyPresentation());
	for(int mismatch = 0; mismatch < 5; ++mismatch)
	{
		SCOPED_TRACE(mismatch);
		auto changed = std::make_shared<Bonus>(*original);
		switch(mismatch)
		{
			case 0: changed->type = BonusType::LUCK; break;
			case 1: changed->source = BonusSource::SECONDARY_SKILL; break;
			case 2: changed->sid = BonusSourceID(HeroTypeID(HeroTypeID::decode("core:solmyr"))); break;
			case 3: changed->val = 2; break;
			case 4: changed->valType = BonusValueType::ADDITIVE_VALUE; break;
		}
		hero->addNewBonus(changed);
		expectRetainedFallback(*hero->getHeroType(), hero->getSpecialtyPresentation());
		hero->removeBonus(changed);
	}
	expectRetainedArt(*hero->getHeroType(), newHorizonsHeroes::resolveSpecialtyPresentation(
		*hero->getHeroType(), hero->getPrimaryGrowthRules(), hero->getMagicRules(), std::nullopt), "HalonMetamagicAdept");
	hero->addNewBonus(original);
	expectRetainedArt(*hero->getHeroType(), hero->getSpecialtyPresentation(), "HalonMetamagicAdept");
}

TEST_F(NewHorizonsRetainedSpecialtyArtTest, HalonHistoricalRetainedBonusNeedsNoHeroOrPerkPolicy)
{
	historicalDevelopment = true;
	magicMode = 1;
	prepare();
	const auto * hero = findHeroAt({5, 5, 0});
	ASSERT_NE(hero, nullptr);
	EXPECT_TRUE(hero->getPrimaryGrowthRules().isNull());
	EXPECT_TRUE(hero->getPerkState().rules.isNull());
	EXPECT_TRUE(hero->getMagicRules().isNull());
	expectRetainedArt(*hero->getHeroType(), hero->getSpecialtyPresentation(), "HalonMetamagicAdept");
}

TEST_F(NewHorizonsRetainedSpecialtyArtTest, HalonDetachedArtRequiresItsOwnActualTypedBonus)
{
	const auto & source = prototype("core:halon");
	const auto before = source.specialty.toJsonNode();
	DetachedRetainedPrototype hero(source);
	const JsonNode development(JsonPath::builtin("config/newHorizonsHeroes"));
	const JsonNode magic(JsonPath::builtin("config/newHorizonsMagic"));
	hero.identity = "test:unbound-metamagic";
	expectRetainedFallback(hero, newHorizonsHeroes::resolveSpecialtyPresentation(hero, development, magic, std::nullopt));
	hero.identity = source.getJsonKey();
	hero.specialty.clear();
	expectRetainedFallback(hero, newHorizonsHeroes::resolveSpecialtyPresentation(hero, development, magic, std::nullopt));
	EXPECT_EQ(source.specialty.toJsonNode(), before);
}

TEST_F(NewHorizonsRetainedSpecialtyArtTest, SolmyrUsesAuthoredMasterSpellWithoutChangingTextOrScaling)
{
	prepare();
	const auto * hero = findHeroAt({6, 5, 0});
	ASSERT_NE(hero, nullptr);
	const SpellID master(SpellID::decode("new-horizons:masterChainLightning"));
	ASSERT_TRUE(hero->spellbookContainsSpell(master));
	EXPECT_FALSE(newHorizonsMagic::spellAvailableForOrdinaryAcquisition(hero->getMagicRules(), master));
	const auto before = hero->getExportedBonusList().toJsonNode();
	const auto spells = hero->getSpellsInSpellbook();
	expectRetainedArt(*hero->getHeroType(), hero->getSpecialtyPresentation(), "SolmyrChainLightning");
	expectRetainedArt(*hero->getHeroType(), newHorizonsHeroes::resolveSpecialtyPresentation(
		*hero->getHeroType(), hero->getPrimaryGrowthRules(), hero->getMagicRules(), std::nullopt), "SolmyrChainLightning");
	EXPECT_EQ(hero->getExportedBonusList().toJsonNode(), before);
	EXPECT_EQ(hero->getSpellsInSpellbook(), spells);
	EXPECT_EQ(hero->getDamageSpellSpecialtyBonusPercent(master), 0);
	EXPECT_EQ(hero->getDamageSpellSpecialtyBonusPercent(SpellID::CHAIN_LIGHTNING), 0);
}

TEST_F(NewHorizonsRetainedSpecialtyArtTest, SolmyrLiveArtRequiresRetainedInscriptionAndPhysicalBook)
{
	prepare();
	auto * hero = findHeroAt({6, 5, 0});
	ASSERT_NE(hero, nullptr);
	const SpellID master(SpellID::decode("new-horizons:masterChainLightning"));
	hero->removeSpellFromSpellbook(master);
	expectRetainedFallback(*hero->getHeroType(), hero->getSpecialtyPresentation());
	expectRetainedArt(*hero->getHeroType(), newHorizonsHeroes::resolveSpecialtyPresentation(
		*hero->getHeroType(), hero->getPrimaryGrowthRules(), hero->getMagicRules(), std::nullopt), "SolmyrChainLightning");
	hero->addSpellToSpellbook(master);
	expectRetainedArt(*hero->getHeroType(), hero->getSpecialtyPresentation(), "SolmyrChainLightning");
	ASSERT_TRUE(hero->hasSpellbook());
	hero->removeArtifact(ArtifactPosition::SPELLBOOK);
	expectRetainedFallback(*hero->getHeroType(), hero->getSpecialtyPresentation());
}

class SolmyrSavedSpecialtyArtTest : public NewHorizonsRetainedSpecialtyArtTest,
	public testing::WithParamInterface<int>
{};

TEST_P(SolmyrSavedSpecialtyArtTest, AbsentHistoricalDisabledOrDeniedRosterKeepsAtlas)
{
	magicMode = GetParam();
	prepare();
	const auto * hero = findHeroAt({6, 5, 0});
	ASSERT_NE(hero, nullptr);
	expectRetainedFallback(*hero->getHeroType(), hero->getSpecialtyPresentation());
	expectRetainedFallback(*hero->getHeroType(), newHorizonsHeroes::resolveSpecialtyPresentation(
		*hero->getHeroType(), hero->getPrimaryGrowthRules(), hero->getMagicRules(), std::nullopt));
}
INSTANTIATE_TEST_SUITE_P(CapturedMasterSpellAccess, SolmyrSavedSpecialtyArtTest, testing::Range(1, 5));

TEST_F(NewHorizonsRetainedSpecialtyArtTest, SolmyrDetachedRequiresExactAuthoredMasterPrototype)
{
	const auto & source = prototype("core:solmyr");
	const auto spells = source.spells;
	const auto exclusions = source.excludedSpells;
	const SpellID master(SpellID::decode("new-horizons:masterChainLightning"));
	const JsonNode development(JsonPath::builtin("config/newHorizonsHeroes"));
	const JsonNode magic(JsonPath::builtin("config/newHorizonsMagic"));
	for(int mismatch = 0; mismatch < 5; ++mismatch)
	{
		SCOPED_TRACE(mismatch);
		DetachedRetainedPrototype hero(source);
		switch(mismatch)
		{
			case 0: hero.spells.erase(master); break;
			case 1: hero.excludedSpells.erase(SpellID::CHAIN_LIGHTNING); break;
			case 2: hero.excludedSpells.insert(master); break;
			case 3: hero.haveSpellBook = false; break;
			case 4: hero.identity = "test:unbound-master-chain"; break;
		}
		expectRetainedFallback(hero, newHorizonsHeroes::resolveSpecialtyPresentation(hero, development, magic, std::nullopt));
	}
	EXPECT_EQ(source.spells, spells);
	EXPECT_EQ(source.excludedSpells, exclusions);
}

TEST_F(NewHorizonsRetainedSpecialtyArtTest, RetainedProvidersPreserveMatchingCapturedContextGuard)
{
	prepare();
	const auto * halon = findHeroAt({5, 5, 0});
	const auto * solmyr = findHeroAt({6, 5, 0});
	ASSERT_NE(halon, nullptr);
	ASSERT_NE(solmyr, nullptr);
	for(const auto * hero : {halon, solmyr})
	{
		const auto * foreign = hero == halon ? solmyr : halon;
		EXPECT_THROW(newHorizonsHeroes::resolveSpecialtyPresentation(*hero->getHeroType(),
			hero->getPrimaryGrowthRules(), hero->getMagicRules(), std::nullopt, foreign), std::invalid_argument);
		EXPECT_THROW(newHorizonsHeroes::resolveSpecialtyPresentation(*hero->getHeroType(),
			JsonNode(), hero->getMagicRules(), std::nullopt, hero), std::invalid_argument);
		EXPECT_THROW(newHorizonsHeroes::resolveSpecialtyPresentation(*hero->getHeroType(),
			hero->getPrimaryGrowthRules(), JsonNode(), std::nullopt, hero), std::invalid_argument);
	}
}

TEST_F(NewHorizonsRetainedSpecialtyArtTest, RetainedLookupRequiresExactHeroFamilyAndCurrentTarget)
{
	using namespace newHorizonsHeroes::detail;
	const auto bonus = BonusTypeID::encode(BonusTypeID(BonusType::METAMAGIC_USES_PER_COMBAT).getNum());
	EXPECT_EQ(resolvedSpecialtyArtwork("core:halon", SpecialtyArtworkFamily::Bonus, bonus),
		"newHorizons/heroSpecialties/HalonMetamagicAdept");
	EXPECT_FALSE(resolvedSpecialtyArtwork("core:solmyr", SpecialtyArtworkFamily::Bonus, bonus));
	EXPECT_FALSE(resolvedSpecialtyArtwork("core:halon", SpecialtyArtworkFamily::Skill, "new-horizons:metamagic"));
	EXPECT_FALSE(resolvedSpecialtyArtwork("core:halon", SpecialtyArtworkFamily::Bonus, "MANA_REGENERATION"));
	EXPECT_EQ(resolvedSpecialtyArtwork("core:solmyr", SpecialtyArtworkFamily::Spell, "new-horizons:masterChainLightning"),
		"newHorizons/heroSpecialties/SolmyrChainLightning");
	EXPECT_FALSE(resolvedSpecialtyArtwork("core:solmyr", SpecialtyArtworkFamily::Spell, "core:chainLightning"));
	EXPECT_FALSE(resolvedSpecialtyArtwork("core:halon", SpecialtyArtworkFamily::Spell, "new-horizons:masterChainLightning"));
}
