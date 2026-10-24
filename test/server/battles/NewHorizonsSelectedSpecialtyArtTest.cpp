/*
 * NewHorizonsSelectedSpecialtyArtTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include "../../../client/windows/HeroSpecialtyPresentation.h"
#include "../../../lib/entities/hero/NewHorizonsSpecialtyArtwork.h"
#include "../../../lib/entities/hero/NewHorizonsRecoverySpecialties.h"
#include "../../../lib/entities/hero/NewHorizonsHerbalSurgeon.h"
#include "../../../lib/entities/hero/NewHorizonsLearningExchangeSpecialties.h"
#include "../../../lib/entities/hero/NewHorizonsSwampSeer.h"
#include "../../../lib/entities/hero/NewHorizonsReclaimer.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/ResourceSet.h"
#include <array>

namespace
{
struct SelectedArtCase
{
	const char * hero;
	const char * policy;
	const char * row;
	const char * skill;
	const char * stem;
};
constexpr std::array<SelectedArtCase, 10> selectedArtCases = {{
	{"core:neela", "skillSpecialties", "", "core:armorer", "NeelaArmorerCore"},
	{"core:gunnar", "skillSpecialties", "", "core:logistics", "GunnarLogisticsCore"},
	{"core:axsis", "recoverySpecialties", "infernalWellspring", "new-horizons:wisdom", "AxsisInfernalWellspring"},
	{"core:jaegar", "recoverySpecialties", "ritualReserve", "new-horizons:wisdom", "JaegarRitualReserve"},
	{"core:rosic", "recoverySpecialties", "deepTrance", "new-horizons:wisdom", "RosicDeepTrance"},
	{"core:verdish", "recoverySpecialties", "herbalSurgeon", "new-horizons:warMachines", "VerdishHerbalSurgeon"},
	{"core:sanya", "learningExchangeSpecialties", "ecumenicalScholar", "new-horizons:learning", "SanyaEcumenicalScholar"},
	{"core:geon", "learningExchangeSpecialties", "occultScholar", "new-horizons:learning", "GeonOccultScholar"},
	{"core:tiva", "swampSeerSpecialty", "", "new-horizons:learning", "TivaSwampSeer"},
	{"core:vidomina", "reclamationSpecialty", "", "new-horizons:necromancy", "VidominaDarkReclaimer"}
}};

const CHero & prototype(const char * id)
{
	return *HeroTypeID(HeroTypeID::decode(id)).toHeroType();
}

void removeSelectedPolicy(JsonNode & development, const SelectedArtCase & row)
{
	if(row.row[0])
		development[row.policy].Struct().erase(row.row);
	else
		development.Struct().erase(row.policy);
}

bool resolveSelectedProvider(size_t index, const CHero & hero, const JsonNode & development,
	const JsonNode & magic, const CGHeroInstance * capturedHero, newHorizonsHeroes::SpecialtyPresentation & result)
{
	using namespace newHorizonsHeroes;
	switch(index)
	{
		case 2: case 3: case 4:
			return resolveRecoverySpecialtyPresentation(hero, development, capturedHero, result);
		case 5:
			return resolveHerbalSurgeonPresentation(hero, development, magic, capturedHero, result);
		case 6: case 7:
			return resolveLearningExchangePresentation(hero, development, magic, capturedHero, result);
		case 8:
			return resolveSwampSeerPresentation(hero, development, capturedHero, result);
		case 9:
			return resolveReclaimerPresentation(hero, development, capturedHero, result);
		default:
			throw std::invalid_argument("Expected an individual presentation provider");
	}
}

void expectSelectedArt(const CHero & hero, const newHorizonsHeroes::SpecialtyPresentation & metadata,
	const SelectedArtCase & row)
{
	const std::string artwork = "newHorizons/heroSpecialties/" + std::string(row.stem);
	EXPECT_EQ(metadata.artwork, artwork);
	ASSERT_TRUE(metadata.skill);
	EXPECT_EQ(metadata.skill->toEntity(LIBRARY)->getJsonKey(), row.skill);
	const auto view = heroSpecialtyPresentation(hero, metadata);
	EXPECT_EQ(view.animation().getOriginalName(), artwork + "/44");
	EXPECT_EQ(view.animation(true).getOriginalName(), artwork + "/32");
	EXPECT_EQ(view.frame(), 0u);
	EXPECT_EQ(view.name, metadata.name);
	EXPECT_EQ(view.description, metadata.description);
	EXPECT_EQ(view.skill, metadata.skill);
}
}

class NewHorizonsSelectedSpecialtyArtTest : public HeroCommandFixture,
	public testing::WithParamInterface<size_t>
{
protected:
	bool historical = false;

	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires New Horizons content";
	}

	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		JsonNode development(JsonPath::builtin("config/newHorizonsHeroes"));
		if(historical)
			removeSelectedPolicy(development, selectedArtCases.at(GetParam()));
		development.setOverrideFlag(true);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, development);
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
	}

	void prepare()
	{
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).playerActive(PlayerColor(0)).playerActive(PlayerColor(1))
			.hero({5, 5, 0}, HeroTypeID(HeroTypeID::decode(selectedArtCases.at(GetParam()).hero)), PlayerColor(0))
			.hero({24, 24, 0}, HeroTypeID(HeroTypeID::decode("core:christian")), PlayerColor(1));
		startWithMap(std::move(builder));
	}
};

TEST_P(NewHorizonsSelectedSpecialtyArtTest, LiveAndDetachedViewsUseSelectedPairsWithoutMutation)
{
	prepare();
	const auto & row = selectedArtCases.at(GetParam());
	SCOPED_TRACE(row.hero);
	const auto * hero = findHeroAt({5, 5, 0});
	ASSERT_NE(hero, nullptr);
	const auto beforeRules = hero->getPrimaryGrowthRules();
	const auto beforeMagic = hero->getMagicRules();
	const auto beforeBonuses = hero->getExportedBonusList().toJsonNode();
	const auto beforeIncome = hero->dailyIncome();
	const auto live = hero->getSpecialtyPresentation();
	expectSelectedArt(*hero->getHeroType(), live, row);
	const auto preview = newHorizonsHeroes::resolveSpecialtyPresentation(*hero->getHeroType(),
		hero->getPrimaryGrowthRules(), hero->getMagicRules(), std::nullopt);
	expectSelectedArt(*hero->getHeroType(), preview, row);
	if(GetParam() >= 2)
	{
		newHorizonsHeroes::SpecialtyPresentation provider;
		ASSERT_TRUE(resolveSelectedProvider(GetParam(), *hero->getHeroType(),
			hero->getPrimaryGrowthRules(), hero->getMagicRules(), hero, provider));
		EXPECT_EQ(provider.name, live.name);
		EXPECT_EQ(provider.description, live.description);
		EXPECT_EQ(provider.artwork, live.artwork);
	}
	EXPECT_EQ(hero->getPrimaryGrowthRules(), beforeRules);
	EXPECT_EQ(hero->getMagicRules(), beforeMagic);
	EXPECT_EQ(hero->getExportedBonusList().toJsonNode(), beforeBonuses);
	EXPECT_EQ(hero->dailyIncome(), beforeIncome);
}

TEST_P(NewHorizonsSelectedSpecialtyArtTest, AbsentCapturedPolicyDoesNotSelectInstalledArt)
{
	historical = true;
	prepare();
	const auto * hero = findHeroAt({5, 5, 0});
	ASSERT_NE(hero, nullptr);
	EXPECT_FALSE(hero->getSpecialtyPresentation().artwork);
	EXPECT_FALSE(newHorizonsHeroes::resolveSpecialtyPresentation(*hero->getHeroType(),
		hero->getPrimaryGrowthRules(), hero->getMagicRules(), std::nullopt).artwork);
	EXPECT_FALSE(newHorizonsHeroes::resolveSpecialtyPresentation(*hero->getHeroType(),
		JsonNode(), hero->getMagicRules(), std::nullopt).artwork);
}

TEST_P(NewHorizonsSelectedSpecialtyArtTest, WrongIdentityOrResolvedSkillCannotClaimSelectedArt)
{
	using namespace newHorizonsHeroes;
	using namespace newHorizonsHeroes::detail;
	const JsonNode development(JsonPath::builtin("config/newHorizonsHeroes"));
	const JsonNode magic(JsonPath::builtin("config/newHorizonsMagic"));
	const auto & row = selectedArtCases.at(GetParam());
	if(GetParam() < 2)
	{
		class ChangedSkillPrototype final : public CHero
		{
			const CHero & source;

		public:
			explicit ChangedSkillPrototype(const CHero & original) : CHero(), source(original)
			{
				// Copy presentation metadata only; Entity and registered heroes stay untouched.
				imageIndex = source.imageIndex;
				secondarySkillSpecialtyAlias = source.secondarySkillSpecialtyAlias;
				for(const auto & bonus : source.specialty)
					specialty.push_back(bonus);
			}
			std::string getJsonKey() const override { return source.getJsonKey(); }
			HeroTypeID getId() const override { return source.getId(); }
			std::string getSpecialtyNameTranslated() const override { return source.getSpecialtyNameTranslated(); }
			std::string getSpecialtyDescriptionTranslated() const override { return source.getSpecialtyDescriptionTranslated(); }
		};
		const auto & original = prototype(row.hero);
		const auto before = original.specialty.toJsonNode();
		ChangedSkillPrototype changed(original);
		ASSERT_TRUE(changed.secondarySkillSpecialtyAlias);
		changed.secondarySkillSpecialtyAlias->skill = SecondarySkill(SecondarySkill::OFFENCE);
		const auto wrong = resolveSpecialtyPresentation(changed, development, magic, std::nullopt);
		EXPECT_EQ(wrong.skill, SecondarySkill(SecondarySkill::OFFENCE));
		EXPECT_FALSE(wrong.artwork);
		EXPECT_FALSE(resolvedSpecialtyArtwork(row.hero, SpecialtyArtworkFamily::Skill, "core:offence"));
		EXPECT_FALSE(resolvedSpecialtyArtwork(row.hero, SpecialtyArtworkFamily::Spell, row.skill));
		EXPECT_FALSE(resolvedSpecialtyArtwork("core:christian", SpecialtyArtworkFamily::Skill, row.skill));
		EXPECT_EQ(original.secondarySkillSpecialtyAlias->skill, SecondarySkill(SecondarySkill::decode(row.skill)));
		EXPECT_EQ(original.specialty.toJsonNode(), before);
	}
	else
	{
		SpecialtyPresentation untouched;
		untouched.name = "prior name";
		untouched.description = "prior description";
		untouched.artwork = "prior artwork";
		EXPECT_FALSE(resolveSelectedProvider(GetParam(), prototype("core:christian"),
			development, magic, nullptr, untouched));
		EXPECT_EQ(untouched.name, "prior name");
		EXPECT_EQ(untouched.description, "prior description");
		EXPECT_EQ(untouched.artwork, "prior artwork");
		auto absent = development;
		removeSelectedPolicy(absent, row);
		EXPECT_FALSE(resolveSelectedProvider(GetParam(), prototype(row.hero), absent, magic, nullptr, untouched));
		EXPECT_EQ(untouched.artwork, "prior artwork");
	}
}

INSTANTIATE_TEST_SUITE_P(SelectedIndividualAndSkillMechanics, NewHorizonsSelectedSpecialtyArtTest,
	testing::Range(size_t{0}, selectedArtCases.size()));
