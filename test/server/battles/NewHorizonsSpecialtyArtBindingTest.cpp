/*
 * NewHorizonsSpecialtyArtBindingTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/entities/hero/NewHorizonsSpecialtyArtwork.h"
#include "../../../client/windows/HeroSpecialtyPresentation.h"
#include "../../../lib/GameConstants.h"
#include "../../../lib/modding/CModHandler.h"
#include <array>
#include <tuple>

namespace
{
struct BindingCase
{
	const char * hero;
	const char * policy;
	const char * artwork;
};

constexpr std::array<BindingCase, 7> bindings = {{
	{"core:andra", "bottomlessMindSpecialty", "newHorizons/heroSpecialties/AndraBottomlessMind"},
	{"core:thorgrim", "masterSpellwardSpecialty", "newHorizons/heroSpecialties/ThorgrimMasterSpellward"},
	{"core:serena", "arcanePreparationSpecialty", "newHorizons/heroSpecialties/SerenaArcanePreparation"},
	{"core:gem", "druidSpecialties", "newHorizons/heroSpecialties/GemRestorativeSorceress"},
	{"core:elleshar", "druidSpecialties", "newHorizons/heroSpecialties/EllesharHighLore"},
	{"core:malcom", "druidSpecialties", "newHorizons/heroSpecialties/MalcomNaturalPhilosopher"},
	{"core:daremyth", "fortuneChildSpecialty", "newHorizons/heroSpecialties/DaremythFortunesChild"}
}};

const CHero & prototype(const char * id)
{
	return *HeroTypeID(HeroTypeID::decode(id)).toHeroType();
}
}

class NewHorizonsSpecialtyArtBindingTest : public HeroCommandFixture
{
protected:
	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires New Horizons content";
	}
};

TEST_F(NewHorizonsSpecialtyArtBindingTest, ExplicitPoliciesBindSevenDistinctNativeResourcePairs)
{
	const JsonNode development(JsonPath::builtin("config/newHorizonsHeroes"));
	const JsonNode magic(JsonPath::builtin("config/newHorizonsMagic"));
	for(const auto & row : bindings)
	{
		SCOPED_TRACE(row.hero);
		const auto & hero = prototype(row.hero);
		const auto metadata = newHorizonsHeroes::resolveSpecialtyPresentation(
			hero, development, magic, std::nullopt);
		ASSERT_TRUE(metadata.artwork);
		EXPECT_EQ(*metadata.artwork, row.artwork);
		const auto view = heroSpecialtyPresentation(hero, metadata);
		EXPECT_EQ(view.animation().getOriginalName(), std::string(row.artwork) + "/44");
		EXPECT_EQ(view.animation(true).getOriginalName(), std::string(row.artwork) + "/32");
		EXPECT_EQ(view.frame(), 0u);
		EXPECT_EQ(view.name, metadata.name);
		EXPECT_EQ(view.description, metadata.description);
		EXPECT_EQ(view.skill, metadata.skill);
		EXPECT_EQ(view.spell, metadata.spell);
	}
}

TEST_F(NewHorizonsSpecialtyArtBindingTest, ExplicitArtOutranksBothSkillAndSpellAliases)
{
	for(const auto & row : bindings)
	{
		SCOPED_TRACE(row.hero);
		newHorizonsHeroes::SpecialtyPresentation metadata;
		metadata.name = "captured name";
		metadata.description = "captured description";
		metadata.skill = SecondarySkill(SecondarySkill::WISDOM);
		metadata.spell = SpellID::CURE;
		metadata.artwork = row.artwork;
		const auto view = heroSpecialtyPresentation(prototype(row.hero), metadata);
		EXPECT_EQ(view.animation().getOriginalName(), std::string(row.artwork) + "/44");
		EXPECT_EQ(view.animation(true).getOriginalName(), std::string(row.artwork) + "/32");
		EXPECT_EQ(view.frame(), 0u);
		EXPECT_EQ(view.name, metadata.name);
		EXPECT_EQ(view.description, metadata.description);
		EXPECT_EQ(view.skill, metadata.skill);
		EXPECT_EQ(view.spell, metadata.spell);
	}
}

TEST_F(NewHorizonsSpecialtyArtBindingTest, AbsentCapturedPoliciesDoNotSelectInstalledArt)
{
	const JsonNode configured(JsonPath::builtin("config/newHorizonsHeroes"));
	const JsonNode magic(JsonPath::builtin("config/newHorizonsMagic"));
	for(const auto & row : bindings)
	{
		SCOPED_TRACE(row.hero);
		auto historical = configured;
		historical.Struct().erase(row.policy);
		const auto view = newHorizonsHeroes::resolveSpecialtyPresentation(
			prototype(row.hero), historical, magic, std::nullopt);
		EXPECT_FALSE(view.artwork);
		const auto legacy = newHorizonsHeroes::resolveSpecialtyPresentation(
			prototype(row.hero), JsonNode(), magic, std::nullopt);
		EXPECT_FALSE(legacy.artwork);
		const auto card = heroSpecialtyPresentation(prototype(row.hero), legacy);
		EXPECT_EQ(card.animation().getOriginalName(), "UN44");
		EXPECT_EQ(card.animation(true).getOriginalName(), "UN32");
		EXPECT_EQ(card.frame(), prototype(row.hero).imageIndex);
	}
}

TEST_F(NewHorizonsSpecialtyArtBindingTest, SerenaNeedsActiveCapturedSpellPointRules)
{
	const JsonNode development(JsonPath::builtin("config/newHorizonsHeroes"));
	const auto metadata = newHorizonsHeroes::resolveSpecialtyPresentation(
		prototype("core:serena"), development, JsonNode(), std::nullopt);
	EXPECT_FALSE(metadata.artwork);
}

TEST_F(NewHorizonsSpecialtyArtBindingTest, HistoricalDaremythKeepsHasteSuccessor)
{
	JsonNode development(JsonPath::builtin("config/newHorizonsHeroes"));
	development.Struct().erase("fortuneChildSpecialty");
	const JsonNode magic(JsonPath::builtin("config/newHorizonsMagic"));
	const auto metadata = newHorizonsHeroes::resolveSpecialtyPresentation(
		prototype("core:daremyth"), development, magic, std::nullopt);
	EXPECT_FALSE(metadata.artwork);
	EXPECT_EQ(metadata.spell, SpellID(SpellID::HASTE));
	const auto view = heroSpecialtyPresentation(prototype("core:daremyth"), metadata);
	EXPECT_EQ(view.animation().getOriginalName(), "SPELLSCR");
	EXPECT_EQ(view.animation(true).getOriginalName(), "SPELLSCR");
	EXPECT_EQ(view.frame(), SpellID(SpellID::HASTE).toSpell()->getIconIndex());
}

TEST_F(NewHorizonsSpecialtyArtBindingTest, CreatureArtRequiresMatchingConvertedTargetAndPolicy)
{
	const JsonNode development(JsonPath::builtin("config/newHorizonsHeroes"));
	const JsonNode magic(JsonPath::builtin("config/newHorizonsMagic"));
	const auto wisp = CreatureID(CreatureID::decode("new-horizons:wisp"));
	const auto ballista = CreatureID(CreatureID::BALLISTA);
	for(const auto & row : std::array<std::tuple<const char *, CreatureID, const char *>, 4>{{
		{"core:monere", wisp, "newHorizons/heroSpecialties/MonereWispLine"},
		{"core:pasis", wisp, "newHorizons/heroSpecialties/PasisWispLine"},
		{"core:gerwulf", ballista, "newHorizons/heroSpecialties/GerwulfBallistaLine"},
		{"core:christian", ballista, "newHorizons/heroSpecialties/ChristianBallistaLine"}}})
	{
		const auto & [id, target, artwork] = row;
		SCOPED_TRACE(id);
		const auto & hero = prototype(id);
		const auto metadata = newHorizonsHeroes::resolveSpecialtyPresentation(
			hero, development, magic, target);
		ASSERT_TRUE(metadata.artwork);
		EXPECT_EQ(*metadata.artwork, artwork);
		EXPECT_EQ(metadata.creature, target);
		const auto view = heroSpecialtyPresentation(hero, metadata);
		EXPECT_EQ(view.animation().getOriginalName(), std::string(artwork) + "/44");
		EXPECT_EQ(view.animation(true).getOriginalName(), std::string(artwork) + "/32");
		EXPECT_EQ(view.frame(), 0u);
		auto disabled = development;
		disabled.Struct().erase("creatureLineSpecialties");
		EXPECT_FALSE(newHorizonsHeroes::resolveSpecialtyPresentation(
			hero, disabled, magic, target).artwork);
		EXPECT_FALSE(newHorizonsHeroes::resolveSpecialtyPresentation(
			hero, JsonNode(), magic, target).artwork);
		EXPECT_FALSE(newHorizonsHeroes::resolveSpecialtyPresentation(
			hero, development, magic, CreatureID(CreatureID::ARCHER)).artwork);
	}
	// An absent target never consults installed Wisp defaults for either legacy hero.
	for(const char * id : {"core:monere", "core:pasis"})
		EXPECT_FALSE(newHorizonsHeroes::resolveSpecialtyPresentation(prototype(id),
			development, magic, std::nullopt).artwork);
	// Unselected heroes do not inherit another hero's artwork for the same target.
	EXPECT_FALSE(newHorizonsHeroes::resolveSpecialtyPresentation(prototype("core:valeska"),
		development, magic, ballista).artwork);
}

TEST_F(NewHorizonsSpecialtyArtBindingTest, PasisAndChristianCapturedStartsUseTheirNativePairsWithoutMutation)
{
	TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
	builder.size(36, false).playerActive(PlayerColor(0)).playerActive(PlayerColor(1))
		.hero({5, 5, 0}, HeroTypeID(HeroTypeID::decode("core:pasis")), PlayerColor(0))
		.hero({6, 5, 0}, HeroTypeID(HeroTypeID::decode("core:christian")), PlayerColor(0))
		.hero({24, 24, 0}, HeroTypeID(HeroTypeID::decode("core:orrin")), PlayerColor(1));
	startWithMap(std::move(builder));
	for(const auto & [position, target, artwork] : std::array{
		std::tuple{int3(5, 5, 0), CreatureID(CreatureID::decode("new-horizons:wisp")),
			"newHorizons/heroSpecialties/PasisWispLine"},
		std::tuple{int3(6, 5, 0), CreatureID(CreatureID::BALLISTA),
			"newHorizons/heroSpecialties/ChristianBallistaLine"}})
	{
		const auto * hero = findHeroAt(position);
		ASSERT_NE(hero, nullptr);
		const auto beforeRules = hero->getPrimaryGrowthRules();
		const auto beforeBonuses = hero->getExportedBonusList().toJsonNode();
		const auto metadata = hero->getSpecialtyPresentation();
		ASSERT_TRUE(metadata.artwork);
		EXPECT_EQ(*metadata.artwork, artwork);
		EXPECT_EQ(metadata.creature, target);
		const auto view = heroSpecialtyPresentation(*hero);
		EXPECT_EQ(view.animation().getOriginalName(), std::string(artwork) + "/44");
		EXPECT_EQ(view.animation(true).getOriginalName(), std::string(artwork) + "/32");
		EXPECT_EQ(view.frame(), 0u);
		EXPECT_EQ(hero->getPrimaryGrowthRules(), beforeRules);
		EXPECT_EQ(hero->getExportedBonusList().toJsonNode(), beforeBonuses);
	}
}

TEST_F(NewHorizonsSpecialtyArtBindingTest, AndraAndThorgrimNeedCapturedMagicRules)
{
	const JsonNode development(JsonPath::builtin("config/newHorizonsHeroes"));
	const JsonNode configured(JsonPath::builtin("config/newHorizonsMagic"));
	for(const char * id : {"core:andra", "core:thorgrim"})
		EXPECT_FALSE(newHorizonsHeroes::resolveSpecialtyPresentation(
			prototype(id), development, JsonNode(), std::nullopt).artwork);
	auto noWardSpell = configured;
	noWardSpell["spells"].Struct().erase("new-horizons:holyArmor");
	EXPECT_FALSE(newHorizonsHeroes::resolveSpecialtyPresentation(
		prototype("core:thorgrim"), development, noWardSpell, std::nullopt).artwork);
}

namespace
{
struct ResolvedArtCase
{
	const char * hero;
	const char * policy;
	const char * target;
	newHorizonsHeroes::detail::SpecialtyArtworkFamily family;
	const char * stem;
};
constexpr std::array<ResolvedArtCase, 86> resolvedArtCases = {{
	{"core:melodia", "remainingSpellSpecialtyReplacements", "core:bless", newHorizonsHeroes::detail::SpecialtyArtworkFamily::Spell, "MelodiaBless"},
	{"core:pyre", "creatureLineSpecialties", "core:ballista", newHorizonsHeroes::detail::SpecialtyArtworkFamily::Creature, "PyreBallistaLine"},
	{"core:ash", "remainingSpellSpecialtyReplacements", "core:fireball", newHorizonsHeroes::detail::SpecialtyArtworkFamily::Spell, "AshFireball"},
	{"core:aislinn", "damageSpellSpecialties", "core:meteorShower", newHorizonsHeroes::detail::SpecialtyArtworkFamily::Spell, "AislinnMeteorShower"},
	{"core:jeddite", "nonDamageSpellSpecialties", "core:resurrection", newHorizonsHeroes::detail::SpecialtyArtworkFamily::Spell, "JedditeResurrection"},
	{"core:gurnisson", "creatureLineSpecialties", "core:ballista", newHorizonsHeroes::detail::SpecialtyArtworkFamily::Creature, "GurnissonBallistaLine"},
	{"core:terek", "nonDamageSpellSpecialties", "core:haste", newHorizonsHeroes::detail::SpecialtyArtworkFamily::Spell, "TerekHaste"},
	{"core:mirlanda", "nonDamageSpellSpecialties", "new-horizons:frailty", newHorizonsHeroes::detail::SpecialtyArtworkFamily::Spell, "MirlandaFrailty"},
	{"core:voy", "skillSpecialties", "core:logistics", newHorizonsHeroes::detail::SpecialtyArtworkFamily::Skill, "VoyLogistics"},
	{"core:inteus", "nonDamageSpellSpecialties", "new-horizons:crusade", newHorizonsHeroes::detail::SpecialtyArtworkFamily::Spell, "InteusCrusade"},
	{"core:kalt", "creatureLineSpecialties", "core:waterElemental", newHorizonsHeroes::detail::SpecialtyArtworkFamily::Creature, "KaltWaterElementalLine"},
	{"core:fiur", "creatureLineSpecialties", "core:fireElemental", newHorizonsHeroes::detail::SpecialtyArtworkFamily::Creature, "FiurFireElementalLine"},
	{"core:orrin", "skillSpecialties", "core:archery", newHorizonsHeroes::detail::SpecialtyArtworkFamily::Skill, "OrrinArchery"},
	{"core:valeska", "creatureLineSpecialties", "core:archer", newHorizonsHeroes::detail::SpecialtyArtworkFamily::Creature, "ValeskaArcherLine"},
	{"core:edric", "creatureLineSpecialties", "core:griffin", newHorizonsHeroes::detail::SpecialtyArtworkFamily::Creature, "EdricGriffinLine"},
	{"core:sorsha", "creatureLineSpecialties", "core:swordsman", newHorizonsHeroes::detail::SpecialtyArtworkFamily::Creature, "SorshaSwordsmanLine"},
	{"core:tyris", "creatureLineSpecialties", "core:cavalier", newHorizonsHeroes::detail::SpecialtyArtworkFamily::Creature, "TyrisCavalierLine"},
	{"core:ivor", "creatureLineSpecialties", "core:woodElf", newHorizonsHeroes::detail::SpecialtyArtworkFamily::Creature, "IvorElfLine"},
	{"core:ufretin", "creatureLineSpecialties", "core:dwarf", newHorizonsHeroes::detail::SpecialtyArtworkFamily::Creature, "UfretinDwarfLine"},
	{"core:ryland", "creatureLineSpecialties", "core:dendroidGuard", newHorizonsHeroes::detail::SpecialtyArtworkFamily::Creature, "RylandDendroidLine"},
	{"core:clancy", "creatureLineSpecialties", "core:unicorn", newHorizonsHeroes::detail::SpecialtyArtworkFamily::Creature, "ClancyUnicornLine"},
	{"core:aeris", "creatureLineSpecialties", "core:pegasus", newHorizonsHeroes::detail::SpecialtyArtworkFamily::Creature, "AerisPegasusLine"},
	{"core:ingham", "creatureLineSpecialties", "core:monk", newHorizonsHeroes::detail::SpecialtyArtworkFamily::Creature, "InghamMonkLine"},
	{"core:fiona", "creatureLineSpecialties", "core:hellHound", newHorizonsHeroes::detail::SpecialtyArtworkFamily::Creature, "FionaHellHoundLine"},
	{"core:ignatius", "creatureLineSpecialties", "core:imp", newHorizonsHeroes::detail::SpecialtyArtworkFamily::Creature, "IgnatiusImpLine"},
	{"core:calh", "creatureLineSpecialties", "core:gog", newHorizonsHeroes::detail::SpecialtyArtworkFamily::Creature, "CalhGogLine"},
	{"core:nymus", "creatureLineSpecialties", "core:pitFiend", newHorizonsHeroes::detail::SpecialtyArtworkFamily::Creature, "NymusPitFiendLine"},
	{"core:straker", "creatureLineSpecialties", "core:walkingDead", newHorizonsHeroes::detail::SpecialtyArtworkFamily::Creature, "StrakerWalkingDeadLine"},
	{"core:zubin", "nonDamageSpellSpecialties", "new-horizons:focusMagic", newHorizonsHeroes::detail::SpecialtyArtworkFamily::Spell, "ZubinFocusMagic"},
	{"core:gretchin", "creatureLineSpecialties", "core:goblin", newHorizonsHeroes::detail::SpecialtyArtworkFamily::Creature, "GretchinGoblinLine"},
	{"core:shiva", "creatureLineSpecialties", "core:roc", newHorizonsHeroes::detail::SpecialtyArtworkFamily::Creature, "ShivaRocLine"},
	{"core:tyraxor", "creatureLineSpecialties", "core:goblinWolfRider", newHorizonsHeroes::detail::SpecialtyArtworkFamily::Creature, "TyraxorGoblinWolfRiderLine"},
	{"core:vokial", "creatureLineSpecialties", "core:vampire", newHorizonsHeroes::detail::SpecialtyArtworkFamily::Creature, "VokialVampireLine"},
	{"core:krellion", "creatureLineSpecialties", "core:ogre", newHorizonsHeroes::detail::SpecialtyArtworkFamily::Creature, "KrellionOgreLine"},
	{"core:gundula", "skillSpecialties", "core:offence", newHorizonsHeroes::detail::SpecialtyArtworkFamily::Skill, "GundulaOffenseCore"},
	{"core:dessa", "skillSpecialties", "core:logistics", newHorizonsHeroes::detail::SpecialtyArtworkFamily::Skill, "DessaLogisticsCore"},
	{"core:drakon", "creatureLineSpecialties", "core:gnoll", newHorizonsHeroes::detail::SpecialtyArtworkFamily::Creature, "DrakonGnollLine"},
	{"core:broghild", "creatureLineSpecialties", "core:wyvern", newHorizonsHeroes::detail::SpecialtyArtworkFamily::Creature, "BroghildWyvernLine"},
	{"core:alkin", "creatureLineSpecialties", "core:gorgon", newHorizonsHeroes::detail::SpecialtyArtworkFamily::Creature, "AlkinGorgonLine"},
	{"core:korbac", "creatureLineSpecialties", "core:serpentFly", newHorizonsHeroes::detail::SpecialtyArtworkFamily::Creature, "KorbacSerpentFlyLine"},
	{"core:tazar", "skillSpecialties", "core:armorer", newHorizonsHeroes::detail::SpecialtyArtworkFamily::Skill, "TazarArmorerCore"},
	{"core:wystan", "creatureLineSpecialties", "core:lizardman", newHorizonsHeroes::detail::SpecialtyArtworkFamily::Creature, "WystanLizardmanLine"},
	{"core:moandor", "creatureLineSpecialties", "core:lich", newHorizonsHeroes::detail::SpecialtyArtworkFamily::Creature, "MoandorLichLine"},
	{"core:galthran", "creatureLineSpecialties", "core:skeleton", newHorizonsHeroes::detail::SpecialtyArtworkFamily::Creature, "GalthranSkeletonLine"},
	{"core:dace", "creatureLineSpecialties", "core:minotaur", newHorizonsHeroes::detail::SpecialtyArtworkFamily::Creature, "DaceMinotaurLine"},
	{"core:ajit", "creatureLineSpecialties", "core:beholder", newHorizonsHeroes::detail::SpecialtyArtworkFamily::Creature, "AjitBeholderLine"},
	{"core:shakti", "creatureLineSpecialties", "core:troglodyte", newHorizonsHeroes::detail::SpecialtyArtworkFamily::Creature, "ShaktiTroglodyteLine"},
	{"core:synca", "creatureLineSpecialties", "core:manticore", newHorizonsHeroes::detail::SpecialtyArtworkFamily::Creature, "SyncaManticoreLine"},
	{"core:yog", "creatureLineSpecialties", "core:cyclop", newHorizonsHeroes::detail::SpecialtyArtworkFamily::Creature, "YogCyclopLine"},
	{"core:jabarkas", "creatureLineSpecialties", "core:orc", newHorizonsHeroes::detail::SpecialtyArtworkFamily::Creature, "JabarkasOrcLine"},
	{"core:charna", "creatureLineSpecialties", "core:wight", newHorizonsHeroes::detail::SpecialtyArtworkFamily::Creature, "CharnaWightLine"},
	{"core:tamika", "creatureLineSpecialties", "core:blackKnight", newHorizonsHeroes::detail::SpecialtyArtworkFamily::Creature, "TamikaBlackKnightLine"},
	{"core:bron", "creatureLineSpecialties", "core:basilisk", newHorizonsHeroes::detail::SpecialtyArtworkFamily::Creature, "BronBasiliskLine"},
	{"core:cragHack", "skillSpecialties", "core:offence", newHorizonsHeroes::detail::SpecialtyArtworkFamily::Skill, "CragHackOffenseCore"},
	{"core:uland", "nonDamageSpellSpecialties", "core:cure", newHorizonsHeroes::detail::SpecialtyArtworkFamily::Spell, "UlandCure"},
	{"core:alagar", "damageSpellSpecialties", "core:iceBolt", newHorizonsHeroes::detail::SpecialtyArtworkFamily::Spell, "AlagarIceBolt"},
	{"core:luna", "damageSpellSpecialties", "core:fireWall", newHorizonsHeroes::detail::SpecialtyArtworkFamily::Spell, "LunaFireWall"},
	{"core:ciele", "damageSpellSpecialties", "core:magicArrow", newHorizonsHeroes::detail::SpecialtyArtworkFamily::Spell, "CieleMagicArrow"},
	{"core:adelaide", "damageSpellSpecialties", "core:frostRing", newHorizonsHeroes::detail::SpecialtyArtworkFamily::Spell, "AdelaideFrostRing"},
	{"core:cuthbert", "nonDamageSpellSpecialties", "new-horizons:frailty", newHorizonsHeroes::detail::SpecialtyArtworkFamily::Spell, "CuthbertFrailty"},
	{"core:deemer", "damageSpellSpecialties", "core:meteorShower", newHorizonsHeroes::detail::SpecialtyArtworkFamily::Spell, "DeemerMeteorShower"},
	{"core:xarfax", "damageSpellSpecialties", "core:fireball", newHorizonsHeroes::detail::SpecialtyArtworkFamily::Spell, "XarfaxFireball"},
	{"core:septienna", "remainingSpellSpecialtyReplacements", "new-horizons:plague", newHorizonsHeroes::detail::SpecialtyArtworkFamily::Spell, "SeptiennaPlague"},
	{"core:thant", "nonDamageSpellSpecialties", "new-horizons:reanimate", newHorizonsHeroes::detail::SpecialtyArtworkFamily::Spell, "ThantReanimate"},
	{"core:coronius", "damageSpellSpecialties", "new-horizons:holyWrath", newHorizonsHeroes::detail::SpecialtyArtworkFamily::Spell, "CoroniusHolyWrath"},
	{"core:loynis", "nonDamageSpellSpecialties", "new-horizons:crusade", newHorizonsHeroes::detail::SpecialtyArtworkFamily::Spell, "LoynisCrusade"},
	{"core:adela", "nonDamageSpellSpecialties", "core:bless", newHorizonsHeroes::detail::SpecialtyArtworkFamily::Spell, "AdelaBless"},
	{"core:arlach", "creatureLineSpecialties", "core:ballista", newHorizonsHeroes::detail::SpecialtyArtworkFamily::Creature, "ArlachBallistaLine"},
	{"core:thunar", "creatureLineSpecialties", "core:earthElemental", newHorizonsHeroes::detail::SpecialtyArtworkFamily::Creature, "ThunarEarthElementalLine"},
	{"core:lacus", "creatureLineSpecialties", "core:waterElemental", newHorizonsHeroes::detail::SpecialtyArtworkFamily::Creature, "LacusWaterElementalLine"},
	{"core:ignissa", "creatureLineSpecialties", "core:fireElemental", newHorizonsHeroes::detail::SpecialtyArtworkFamily::Creature, "IgnissaFireElementalLine"},
	{"core:xyron", "damageSpellSpecialties", "core:inferno", newHorizonsHeroes::detail::SpecialtyArtworkFamily::Spell, "XyronInferno"},
	{"core:darkstorn", "remainingSpellSpecialtyReplacements", "new-horizons:hexOfPain", newHorizonsHeroes::detail::SpecialtyArtworkFamily::Spell, "DarkstornHexOfPain"},
	{"core:merist", "nonDamageSpellSpecialties", "new-horizons:hydrasVitality", newHorizonsHeroes::detail::SpecialtyArtworkFamily::Spell, "MeristHydrasVitality"},
	{"core:sylvia", "skillSpecialties", "core:logistics", newHorizonsHeroes::detail::SpecialtyArtworkFamily::Skill, "SylviaLogisticsCore"},
	{"core:lordHaart", "skillSpecialties", "core:estates", newHorizonsHeroes::detail::SpecialtyArtworkFamily::Skill, "LordHaartEstatesCore"},
	{"core:piquedram", "creatureLineSpecialties", "core:stoneGargoyle", newHorizonsHeroes::detail::SpecialtyArtworkFamily::Creature, "PiquedramGargoyleLine"},
	{"core:erdamon", "creatureLineSpecialties", "core:earthElemental", newHorizonsHeroes::detail::SpecialtyArtworkFamily::Creature, "ErdamonEarthElementalLine"},
	{"core:kyrre", "skillSpecialties", "core:logistics", newHorizonsHeroes::detail::SpecialtyArtworkFamily::Skill, "KyrreLogistics"},
	{"core:alamar", "nonDamageSpellSpecialties", "core:resurrection", newHorizonsHeroes::detail::SpecialtyArtworkFamily::Spell, "AlamarResurrection"},
	{"core:mephala", "skillSpecialties", "core:armorer", newHorizonsHeroes::detail::SpecialtyArtworkFamily::Skill, "MephalaArmorer"},
	{"core:vey", "creatureLineSpecialties", "core:ogre", newHorizonsHeroes::detail::SpecialtyArtworkFamily::Creature, "VeyOgreLine"},
	{"core:thane", "creatureLineSpecialties", "core:genie", newHorizonsHeroes::detail::SpecialtyArtworkFamily::Creature, "ThaneGenieLine"},
	{"core:josephine", "creatureLineSpecialties", "core:ironGolem", newHorizonsHeroes::detail::SpecialtyArtworkFamily::Creature, "JosephineGolemLine"},
	{"core:torosar", "creatureLineSpecialties", "core:ballista", newHorizonsHeroes::detail::SpecialtyArtworkFamily::Creature, "TorosarBallistaLine"},
	{"core:fafner", "creatureLineSpecialties", "core:naga", newHorizonsHeroes::detail::SpecialtyArtworkFamily::Creature, "FafnerNagaLine"},
}};

void expectResolvedArt(const CHero & hero, const newHorizonsHeroes::SpecialtyPresentation & metadata,
	const ResolvedArtCase & row)
{
	const std::string artwork = "newHorizons/heroSpecialties/" + std::string(row.stem);
	ASSERT_TRUE(metadata.artwork);
	EXPECT_EQ(*metadata.artwork, artwork);
	using Family = newHorizonsHeroes::detail::SpecialtyArtworkFamily;
	if(row.family == Family::Creature)
	{
		ASSERT_TRUE(metadata.creature);
		EXPECT_EQ(metadata.creature->toCreature()->getJsonKey(), row.target);
	}
	else if(row.family == Family::Spell)
	{
		ASSERT_TRUE(metadata.spell);
		EXPECT_EQ(metadata.spell->toSpell()->getJsonKey(), row.target);
	}
	else
	{
		ASSERT_TRUE(metadata.skill);
		EXPECT_EQ(metadata.skill->toEntity(LIBRARY)->getJsonKey(), row.target);
	}
	const auto view = heroSpecialtyPresentation(hero, metadata);
	EXPECT_EQ(view.animation().getOriginalName(), artwork + "/44");
	EXPECT_EQ(view.animation(true).getOriginalName(), artwork + "/32");
	EXPECT_EQ(view.frame(), 0u);
}
}

TEST_F(NewHorizonsSpecialtyArtBindingTest, ResolvedArtworkLookupRequiresExactHeroFamilyAndTarget)
{
	using namespace newHorizonsHeroes::detail;
	for(const auto & row : resolvedArtCases)
	{
		SCOPED_TRACE(row.hero);
		EXPECT_EQ(resolvedSpecialtyArtwork(row.hero, row.family, row.target),
			"newHorizons/heroSpecialties/" + std::string(row.stem));
		EXPECT_FALSE(resolvedSpecialtyArtwork("core:caitlin", row.family, row.target));
		EXPECT_FALSE(resolvedSpecialtyArtwork(row.hero, row.family, "core:pikeman"));
		const auto wrongFamily = row.family == SpecialtyArtworkFamily::Spell
			? SpecialtyArtworkFamily::Skill : SpecialtyArtworkFamily::Spell;
		EXPECT_FALSE(resolvedSpecialtyArtwork(row.hero, wrongFamily, row.target));
	}
}

class ResolvedSpecialtyArtTest : public NewHorizonsSpecialtyArtBindingTest,
	public testing::WithParamInterface<size_t>
{
protected:
	bool historical = false;

	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		JsonNode development(JsonPath::builtin("config/newHorizonsHeroes"));
		if(historical)
		{
			const std::string_view policy = resolvedArtCases.at(GetParam()).policy;
			development.Struct().erase(std::string(policy));
			// Remove dependent opt-ins too, retaining a valid historical context.
			if(policy == "creatureLineSpecialties")
				development.Struct().erase("defaultCreatureLineReplacements");
			if(policy == "damageSpellSpecialties" || policy == "nonDamageSpellSpecialties")
				development.Struct().erase("remainingSpellSpecialtyReplacements");
		}
		development.setOverrideFlag(true);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, development);
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
	}

	void prepare()
	{
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).playerActive(PlayerColor(0)).playerActive(PlayerColor(1))
			.hero({5, 5, 0}, HeroTypeID(HeroTypeID::decode(resolvedArtCases.at(GetParam()).hero)), PlayerColor(0))
			.hero({24, 24, 0}, HeroTypeID(HeroTypeID::decode("core:christian")), PlayerColor(1));
		startWithMap(std::move(builder));
	}
};

TEST_P(ResolvedSpecialtyArtTest, CapturedMechanicAndStartingProjectionUseSameArtWithoutMutation)
{
	prepare();
	const auto & row = resolvedArtCases.at(GetParam());
	SCOPED_TRACE(row.hero);
	const auto * hero = findHeroAt({5, 5, 0});
	ASSERT_NE(hero, nullptr);
	const auto beforeRules = hero->getPrimaryGrowthRules();
	const auto beforeBonuses = hero->getExportedBonusList().toJsonNode();
	const auto metadata = hero->getSpecialtyPresentation();
	expectResolvedArt(*hero->getHeroType(), metadata, row);
	const auto preview = newHorizonsHeroes::resolveSpecialtyPresentation(*hero->getHeroType(),
		hero->getPrimaryGrowthRules(), hero->getMagicRules(), metadata.creature);
	expectResolvedArt(*hero->getHeroType(), preview, row);
	EXPECT_EQ(metadata.name, preview.name);
	EXPECT_EQ(metadata.description, preview.description);
	EXPECT_EQ(hero->getPrimaryGrowthRules(), beforeRules);
	EXPECT_EQ(hero->getExportedBonusList().toJsonNode(), beforeBonuses);
}

TEST_P(ResolvedSpecialtyArtTest, AbsentCapturedFamilyPolicyKeepsHistoricalFallback)
{
	historical = true;
	prepare();
	const auto & row = resolvedArtCases.at(GetParam());
	SCOPED_TRACE(row.hero);
	const auto * hero = findHeroAt({5, 5, 0});
	ASSERT_NE(hero, nullptr);
	EXPECT_FALSE(hero->getSpecialtyPresentation().artwork);
	EXPECT_FALSE(newHorizonsHeroes::resolveSpecialtyPresentation(*hero->getHeroType(),
		JsonNode(), hero->getMagicRules(), std::nullopt).artwork);
}

INSTANTIATE_TEST_SUITE_P(SelectedCurrentMechanics, ResolvedSpecialtyArtTest,
	testing::Range(size_t{0}, resolvedArtCases.size()));

TEST_F(NewHorizonsSpecialtyArtBindingTest, SelectedCreatureArtworkRejectsResolvedWrongLine)
{
	const JsonNode development(JsonPath::builtin("config/newHorizonsHeroes"));
	const JsonNode magic(JsonPath::builtin("config/newHorizonsMagic"));
	const CreatureID other(CreatureID::decode("core:pikeman"));
	for(const auto & row : resolvedArtCases)
	{
		if(row.family != newHorizonsHeroes::detail::SpecialtyArtworkFamily::Creature)
			continue;
		SCOPED_TRACE(row.hero);
		const auto metadata = newHorizonsHeroes::resolveSpecialtyPresentation(
			prototype(row.hero), development, magic, other);
		EXPECT_EQ(metadata.creature, other);
		EXPECT_FALSE(metadata.artwork);
	}
}

TEST_F(NewHorizonsSpecialtyArtBindingTest, ZubinArtworkRequiresCurrentFocusMagicSuccessor)
{
	const JsonNode configured(JsonPath::builtin("config/newHorizonsHeroes"));
	const JsonNode magic(JsonPath::builtin("config/newHorizonsMagic"));
	auto noSuccessor = configured;
	noSuccessor["nonDamageSpellSpecialties"]["offensiveStartReplacements"].Bool() = false;
	EXPECT_FALSE(newHorizonsHeroes::resolveSpecialtyPresentation(prototype("core:zubin"),
		noSuccessor, magic, std::nullopt).artwork);
	EXPECT_FALSE(newHorizonsHeroes::resolveSpecialtyPresentation(prototype("core:zubin"),
		configured, JsonNode(), std::nullopt).artwork);
}


TEST_F(NewHorizonsSpecialtyArtBindingTest, SyntheticArmorerIdentityRetainsGenericSkillFallback)
{
	class UnboundPrototype final : public CHero
	{
	public:
		explicit UnboundPrototype(const CHero & source) : CHero(source) {}
		std::string getJsonKey() const override { return "test:unbound-armorer"; }
	};
	const UnboundPrototype hero(prototype("core:mephala"));
	ASSERT_TRUE(hero.secondarySkillSpecialtyAlias);
	const auto & alias = *hero.secondarySkillSpecialtyAlias;
	ASSERT_EQ(alias.skill, SecondarySkill(SecondarySkill::ARMORER));
	ASSERT_FALSE(alias.bonuses.empty());
	for(const auto & bonus : alias.bonuses)
	{
		ASSERT_NE(bonus, nullptr);
		EXPECT_TRUE(vstd::contains(hero.specialty, bonus));
	}
	const JsonNode development(JsonPath::builtin("config/newHorizonsHeroes"));
	const JsonNode magic(JsonPath::builtin("config/newHorizonsMagic"));
	const auto before = hero.specialty.toJsonNode();
	const auto metadata = newHorizonsHeroes::resolveSpecialtyPresentation(
		hero, development, magic, std::nullopt);
	EXPECT_FALSE(metadata.artwork);
	EXPECT_EQ(metadata.skill, alias.skill);
	EXPECT_FALSE(metadata.spell);
	EXPECT_FALSE(metadata.creature);
	const auto view = heroSpecialtyPresentation(hero, metadata);
	EXPECT_EQ(view.animation().getOriginalName(), "SECSKILL");
	EXPECT_EQ(view.animation(true).getOriginalName(), "SECSK32");
	EXPECT_EQ(view.frame(), alias.skill.toEntity(LIBRARY)->getIconIndex());
	EXPECT_EQ(view.name, metadata.name);
	EXPECT_EQ(view.description, metadata.description);
	EXPECT_EQ(hero.specialty.toJsonNode(), before);
}
