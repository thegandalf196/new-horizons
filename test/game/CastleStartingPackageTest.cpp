/*
 * CastleStartingPackageTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "../mock/TinyMapGameTest.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/CCreatureHandler.h"
#include "../../lib/entities/creature/NewHorizonsAssimilation.h"
#include "../../lib/bonuses/Bonus.h"
#include "../../lib/bonuses/BonusParameters.h"
#include "../../lib/bonuses/Limiters.h"
#include "../../lib/entities/hero/CHeroClass.h"
#include "../../lib/entities/hero/CHeroHandler.h"
#include "../../lib/entities/hero/HeroStartingProjection.h"
#include "../../lib/entities/hero/NewHorizonsCommandArmy.h"
#include "../../lib/gameState/TavernHeroesPool.h"
#include "../../lib/json/JsonUtils.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/mapObjects/army/CSimpleArmy.h"
#include "../../lib/mapObjects/army/CStackInstance.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include <tuple>

namespace
{
using namespace newHorizonsHeroes;

StartingHeroContext configured()
{
	StartingHeroContext context;
	context.development = JsonNode(JsonPath::builtin("config/newHorizonsHeroes"));
	context.capabilities = JsonNode(JsonPath::builtin("config/newHorizonsCapabilities"));
	context.perks = JsonNode(JsonPath::builtin("config/newHorizonsPerks"));
	context.magic = JsonNode(JsonPath::builtin("config/newHorizonsMagic"));
	return context;
}
const CHero & prototype(const char * key)
{
	return *HeroTypeID(HeroTypeID::decode(key)).toHeroType();
}
CreatureID unit(const char * key)
{
	return CreatureID(CreatureID::decode(key));
}
class PackagePrototype final : public CHero
{
	const CHero & original;
public:
	explicit PackagePrototype(const CHero & source) : original(source)
	{
		heroClass = source.heroClass;
		secSkillsInit = source.secSkillsInit;
		startingPerks = source.startingPerks;
		spells = source.spells;
		haveSpellBook = source.haveSpellBook;
		creatureLineSpecialtyAlias = source.creatureLineSpecialtyAlias;
	}
	HeroTypeID getId() const override { return original.getId(); }
	std::string getJsonKey() const override { return original.getJsonKey(); }
	std::string getSpecialtyNameTranslated() const override { return original.getSpecialtyNameTranslated(); }
	std::string getSpecialtyDescriptionTranslated() const override { return original.getSpecialtyDescriptionTranslated(); }
};
CreatureID replacement(CreatureID source, bool specialist)
{
	if(!specialist && source == unit("core:griffin"))
		return unit("core:swordsman");
	if(!specialist && source == unit("core:royalGriffin"))
		return unit("core:crusader");
	return source;
}
std::map<CreatureID, TQuantity> troops(const CGHeroInstance & hero)
{
	std::map<CreatureID, TQuantity> result;
	for(const auto & [slot, stack] : hero.Slots())
		result[stack->getCreatureID()] += stack->getCount();
	return result;
}
void expectRows(const std::vector<CHero::InitialArmyStack> & actual,
	const std::vector<CHero::InitialArmyStack> & original, bool specialist)
{
	ASSERT_EQ(actual.size(), original.size());
	for(size_t index = 0; index < original.size(); ++index)
	{
		SCOPED_TRACE(index);
		EXPECT_EQ(actual[index].minAmount, original[index].minAmount);
		EXPECT_EQ(actual[index].maxAmount, original[index].maxAmount);
		EXPECT_EQ(actual[index].creature, replacement(original[index].creature, specialist));
	}
}

class CastleStartingPackageTest : public TinyMapGameTest,
	public ::testing::WithParamInterface<const char *>
{
protected:
	bool authored = false;
	bool poolHero = false;
	std::vector<int> chances{100, 100, 100};
	StartingHeroOverrides loaderOverrides;
	std::vector<CHero::InitialArmyStack> originalRows;
	Services * gameServices() override { return LIBRARY; }
	void configureWorldInitialization(StartInfo &, GameRandomizer & rng) const override { rng.setSeed(1337); }
	HeroTypeID type() const { return HeroTypeID(HeroTypeID::decode(GetParam())); }
	bool specialist() const { return std::string(GetParam()) == "core:edric"; }
	CGHeroInstance * actor() const
	{
		return poolHero ? map()->tryGetFromHeroPool(type()) : map()->getHero(type());
	}
	void mapLoaded(CMap * loaded) override
	{
		TinyMapGameTest::mapLoaded(loaded);
		const auto context = configured();
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, context.development);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_CAPABILITIES, context.capabilities);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, context.perks);
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, context.magic);
		const JsonNode combat(JsonPath::builtin("config/newHorizonsCombat"));
		loaded->overrideGameSetting(EGameSettings::COMBAT_HERO_COMMANDS, combat["combat"]["heroCommands"]);
		JsonNode rolls;
		for(const auto chance : chances)
			rolls.Vector().emplace_back(chance);
		loaded->overrideGameSetting(EGameSettings::HEROES_STARTING_STACKS_CHANCES, rolls);
		if(!poolHero)
			loaderOverrides = authoredStartingHeroOverrides(*loaded->getHero(type()), loaded);
	}
	void prepare()
	{
		originalRows = type().toHeroType()->initialArmy;
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).playerActive(PlayerColor(0)).playerActive(PlayerColor(1))
			.hero({5, 5, 0}, poolHero ? HeroTypeID(HeroTypeID::decode("core:ivor")) : type(), PlayerColor(0))
			.heroExperience(authored ? LIBRARY->heroh->reqExp(9) : 0);
		if(authored)
			builder.heroGarrison({{unit("core:griffin"), 7}, {unit("core:royalGriffin"), 5}, {unit("core:pikeman"), 17}});
		builder.hero({8, 8, 0}, HeroTypeID(HeroTypeID::decode("core:solmyr")), PlayerColor(1))
			.heroGarrison({{unit("core:pikeman"), 1}});
		startWithMap(std::move(builder));
		ASSERT_NE(actor(), nullptr);
		ASSERT_TRUE(actor()->getArmyCommand());
	}
	void expectDefaultArmy()
	{
		const auto view = projectStartingHero(*type().toHeroType(), configured());
		ASSERT_NO_FATAL_FAILURE(expectRows(view.army, originalRows, specialist()));
		ASSERT_EQ(actor()->stacksCount(), originalRows.size());
		for(size_t index = 0; index < originalRows.size(); ++index)
		{
			const SlotID slot(index);
			ASSERT_NE(actor()->getCreature(slot), nullptr);
			EXPECT_EQ(actor()->getCreature(slot)->getId(), view.army[index].creature);
			EXPECT_GE(actor()->getStackCount(slot), originalRows[index].minAmount);
			EXPECT_LE(actor()->getStackCount(slot), originalRows[index].maxAmount);
		}
		EXPECT_EQ(startingArmyChances(view, {}, chances), chances);
		EXPECT_EQ(newHorizonsCreatures::isCommandSpecialistCreature(*actor(), unit("core:griffin")), specialist());
		EXPECT_EQ(newHorizonsCreatures::isCommandSpecialistCreature(*actor(), unit("core:royalGriffin")), specialist());
		ASSERT_NO_FATAL_FAILURE(expectRows(type().toHeroType()->initialArmy, originalRows, true));
	}
};

TEST_P(CastleStartingPackageTest, GeneratedMapDefaultAndFreshProjectionAgree)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_FALSE(loaderOverrides.army);
	ASSERT_NO_FATAL_FAILURE(expectDefaultArmy());
}

TEST_P(CastleStartingPackageTest, FreshTavernPoolDefaultAndHiringPreviewAgree)
{
	poolHero = true;
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(expectDefaultArmy());
	const auto before = troops(*actor());
	CRandomGenerator rng(41);
	CSimpleArmy preview;
	actor()->initArmy(rng, &preview);
	EXPECT_EQ(troops(*actor()), before);
	for(const auto & [slot, row] : preview.army)
		EXPECT_EQ(row.first, replacement(originalRows.at(slot.getNum()).creature, specialist()));
	auto hired = gameState()->heroesPool->takeHeroFromPool(type());
	ASSERT_NE(hired, nullptr);
	EXPECT_EQ(troops(*hired), before);
}

TEST_P(CastleStartingPackageTest, OriginalStackSelectionCountRollsAndFollowingRngAreExact)
{
	chances = {100, 88, 25};
	ASSERT_NO_FATAL_FAILURE(prepare());
	for(int seed = 1; seed <= 128; ++seed)
	{
		SCOPED_TRACE(seed);
		CRandomGenerator actual(seed), reference(seed);
		const auto selection = reference.nextInt(1, 100);
		TSimpleSlots expected;
		for(size_t index = 0; index < std::min(chances.size(), originalRows.size()); ++index)
		{
			const auto & source = originalRows[index];
			ASSERT_EQ(source.creature.toCreature()->warMachine, ArtifactID::NONE);
			if(selection <= chances[index])
				expected.emplace(SlotID(index), std::make_pair(replacement(source.creature, specialist()),
					static_cast<TQuantity>(reference.nextInt(source.minAmount, source.maxAmount))));
		}
		CSimpleArmy generated;
		actor()->initArmy(actual, &generated);
		EXPECT_EQ(generated.army, expected);
		for(int next = 0; next < 8; ++next)
			EXPECT_EQ(actual.nextInt(), reference.nextInt());
	}
	ASSERT_NO_FATAL_FAILURE(expectRows(type().toHeroType()->initialArmy, originalRows, true));
}

TEST_P(CastleStartingPackageTest, AuthoredBaseAndUpgradeTroopsSurviveProjectionReinitAndWorldLoad)
{
	authored = true; // Level nine admits both Elite rows without changing placement policy.
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(loaderOverrides.army);
	const auto view = projectStartingHero(*type().toHeroType(), configured(), loaderOverrides);
	ASSERT_NO_FATAL_FAILURE(expectRows(view.army, *loaderOverrides.army, true));
	const std::map<CreatureID, TQuantity> expected{
		{unit("core:griffin"), 7}, {unit("core:royalGriffin"), 5}, {unit("core:pikeman"), 17}};
	EXPECT_EQ(troops(*actor()), expected);
	GameRandomizer randomizer(*gameState());
	ASSERT_NO_THROW(actor()->initHero(randomizer));
	EXPECT_EQ(troops(*actor()), expected);
	CGameState restored;
	restored.preInit(LIBRARY);
	restored.loadFromMemory(gameState()->saveToMemory());
	auto * saved = restored.getMap().getHero(type());
	ASSERT_NE(saved, nullptr);
	EXPECT_EQ(troops(*saved), expected);
	GameRandomizer reloadedRandomizer(restored);
	ASSERT_NO_THROW(saved->initHero(reloadedRandomizer));
	EXPECT_EQ(troops(*saved), expected);
	EXPECT_EQ(saved->getCapabilityRules()["armyCommand"]["defaultStartingArmyVersion"].Integer(), 1);
}

INSTANTIATE_TEST_SUITE_P(OrdinaryCastleAndGriffinSpecialist, CastleStartingPackageTest,
	::testing::Values("core:orrin", "core:adela", "core:valeska", "core:edric"),
	[](const auto & info) { return std::string(info.param).substr(5); });

TEST(CastleStartingPackagePolicy, CapturedOptInIsStrictAndOlderCapturesRetainOriginalRows)
{
	auto context = configured();
	const auto & hero = prototype("core:orrin");
	EXPECT_TRUE(JsonUtils::validate(context.capabilities, "vcmi:newHorizonsCapabilities", "starting package"));
	const auto resolved = resolveCapabilityRules(context.capabilities, hero.heroClass->getId());
	EXPECT_EQ(resolved["armyCommand"]["defaultStartingArmyVersion"].Integer(), 1);
	CMemorySerializer memory;
	memory.oser & resolved;
	JsonNode restored;
	memory.iser & restored;
	EXPECT_EQ(restored, resolved);
	EXPECT_EQ(defaultStartingArmyCreature(hero, restored, unit("core:griffin"), false), unit("core:swordsman"));
	auto captured = context;
	captured.resolved = true;
	captured.development = resolveHeroRules(context.development, hero.heroClass->getId());
	captured.capabilities = restored;
	ASSERT_NO_FATAL_FAILURE(expectRows(projectStartingHero(hero, captured).army, hero.initialArmy, false));
	context.capabilities["armyCommand"].Struct().erase("defaultStartingArmyVersion");
	EXPECT_NO_THROW(validateCapabilityRules(context.capabilities, true));
	const auto old = resolveCapabilityRules(context.capabilities, hero.heroClass->getId());
	EXPECT_EQ(defaultStartingArmyCreature(hero, old, unit("core:griffin"), false), unit("core:griffin"));
	EXPECT_EQ(defaultStartingArmyCreature(hero, JsonNode(), unit("core:royalGriffin"), false), unit("core:royalGriffin"));
	const auto oldView = projectStartingHero(hero, context);
	ASSERT_NO_FATAL_FAILURE(expectRows(oldView.army, hero.initialArmy, true));
	captured.capabilities = old;
	ASSERT_NO_FATAL_FAILURE(expectRows(projectStartingHero(hero, captured).army, hero.initialArmy, true));
	for(const int version : {0, 2})
	{
		auto invalid = resolved;
		invalid["armyCommand"]["defaultStartingArmyVersion"].Integer() = version;
		EXPECT_THROW(validateResolvedCapabilityRules(invalid), std::runtime_error);
		EXPECT_THROW(defaultStartingArmyCreature(hero, invalid, unit("core:griffin"), false), std::runtime_error);
	}
}

TEST(CastleStartingPackagePolicy, BaseUpgradeFormsUseResolvedSpecialtyAndPreserveEveryOtherProjectionField)
{
	const auto context = configured();
	for(const char * key : {"core:orrin", "core:adela", "core:valeska", "core:edric", "core:pasis"})
	{
		SCOPED_TRACE(key);
		PackagePrototype hero(prototype(key)); // Detached metadata, never the library prototype.
		hero.initialArmy = {{3, 7, unit("core:griffin")}, {2, 4, unit("core:royalGriffin")},
			{9, 12, unit("core:archer")}};
		auto oldContext = context;
		oldContext.capabilities["armyCommand"].Struct().erase("defaultStartingArmyVersion");
		const auto before = projectStartingHero(hero, oldContext);
		const auto after = projectStartingHero(hero, context);
		const bool retain = std::string(key) == "core:edric" || std::string(key) == "core:pasis";
		ASSERT_NO_FATAL_FAILURE(expectRows(after.army, hero.initialArmy, retain));
		EXPECT_EQ(after.primary, before.primary);
		EXPECT_EQ(after.skills, before.skills);
		EXPECT_EQ(after.perks, before.perks);
		EXPECT_EQ(after.spells, before.spells);
		EXPECT_EQ(after.spellBook, before.spellBook);
		EXPECT_EQ(after.firstAidTentChance, before.firstAidTentChance);
		EXPECT_EQ(after.army.back().creature, unit("core:archer"));
	}
	const auto & specialist = prototype("core:edric");
	const auto development = resolveHeroRules(context.development, specialist.heroClass->getId());
	const auto capabilities = resolveCapabilityRules(context.capabilities, specialist.heroClass->getId());
	for(const auto source : {unit("core:griffin"), unit("core:royalGriffin")})
	{
		EXPECT_TRUE(newHorizonsCreatures::isCommandSpecialistCreature(
			specialist, development, context.magic, unit("core:griffin"), source));
		const auto retargeted = newHorizonsCreatures::isCommandSpecialistCreature(
			specialist, development, context.magic, unit("core:swordsman"), source);
		EXPECT_FALSE(retargeted); // A name/alias exemption would incorrectly keep these rows.
		EXPECT_EQ(defaultStartingArmyCreature(specialist, capabilities, source, retargeted),
			replacement(source, false));
	}
}

TEST(CommandStartingPackagePolicy, SpecialUpgradeSpecialtiesPermitTargetsWithoutGrantingAllSources)
{
	const auto context = configured();
	for(const auto & [key, target, sources] : std::vector<std::tuple<const char *, const char *, std::vector<const char *>>>{
		{"core:dracon", "core:enchanter", {"core:mage", "core:archMage", "core:monk", "core:zealot"}},
		{"core:gelu", "core:sharpshooter", {"core:archer", "core:marksman", "core:woodElf", "core:grandElf"}}})
	{
		SCOPED_TRACE(key);
		const auto & hero = prototype(key);
		const auto development = resolveHeroRules(context.development, hero.heroClass->getId());
		const auto matches = [&](const char * creature)
		{
			return newHorizonsCreatures::isCommandSpecialistCreature(
				hero, development, context.magic, std::nullopt, unit(creature));
		};
		EXPECT_TRUE(matches(target));
		for(const auto * source : sources)
			EXPECT_FALSE(matches(source)) << source;
		EXPECT_FALSE(matches("core:angel"));
		EXPECT_FALSE(matches("core:griffin"));
		EXPECT_FALSE(newHorizonsCreatures::hasIntrinsicAssimilation(unit(target)));
		// Identical producer on another identity proves this is not a name exemption.
		PackagePrototype detached(prototype("core:orrin"));
		detached.specialty = hero.specialty;
		EXPECT_TRUE(newHorizonsCreatures::isCommandSpecialistCreature(
			detached, development, context.magic, std::nullopt, unit(target)));
	}
}

TEST(CommandStartingPackagePolicy, FixedCampaignCreatureLimitersRespectUpgradesAndCapturedRetargeting)
{
	const auto context = configured();
	for(const auto & [key, base, upgrade] : std::vector<std::tuple<const char *, const char *, const char *>>{
		{"core:kilgor", "core:behemoth", "core:ancientBehemoth"},
		{"core:undeadHaart", "core:blackKnight", "core:dreadKnight"},
		{"core:xeron", "core:devil", "core:archDevil"}})
	{
		SCOPED_TRACE(key);
		const auto & hero = prototype(key);
		const auto development = resolveHeroRules(context.development, hero.heroClass->getId());
		for(const auto * creature : {base, upgrade})
		{
			EXPECT_TRUE(newHorizonsCreatures::isCommandSpecialistCreature(
				hero, development, context.magic, std::nullopt, unit(creature)));
			EXPECT_FALSE(newHorizonsCreatures::isCommandSpecialistCreature(
				hero, development, context.magic, unit("core:pikeman"), unit(creature)));
		}
		// The original fixed producer remains legitimate without a conversion alias.
		PackagePrototype fixed(hero);
		fixed.creatureLineSpecialtyAlias.reset();
		fixed.specialty = hero.specialty;
		EXPECT_TRUE(newHorizonsCreatures::isCommandSpecialistCreature(
			fixed, development, context.magic, std::nullopt, unit(base)));
		EXPECT_TRUE(newHorizonsCreatures::isCommandSpecialistCreature(
			fixed, development, context.magic, std::nullopt, unit(upgrade)));
		EXPECT_FALSE(newHorizonsCreatures::isCommandSpecialistCreature(
			fixed, development, context.magic, std::nullopt, unit("core:angel")));
	}
}

TEST(CommandStartingPackagePolicy, FixedPermissionRequiresCreatureLimiterAndAuthoredUpgradeFlag)
{
	const auto context = configured();
	PackagePrototype hero(prototype("core:orrin"));
	const auto development = resolveHeroRules(context.development, hero.heroClass->getId());
	auto bonus = std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::STACKS_SPEED,
		BonusSource::HERO_SPECIAL, 1, BonusSourceID(hero.getId()));
	auto limiter = std::make_shared<CCreatureTypeLimiter>(*unit("core:griffin").toCreature(), false);
	bonus->limiter = limiter;
	hero.specialty.push_back(bonus);
	const auto matches = [&](const char * creature)
	{
		return newHorizonsCreatures::isCommandSpecialistCreature(
			hero, development, context.magic, std::nullopt, unit(creature));
	};
	EXPECT_TRUE(matches("core:griffin"));
	EXPECT_FALSE(matches("core:royalGriffin"));
	limiter->includeUpgrades = true;
	EXPECT_TRUE(matches("core:royalGriffin"));
	bonus->source = BonusSource::ARTIFACT;
	EXPECT_FALSE(matches("core:griffin"));
	bonus->source = BonusSource::HERO_SPECIAL;
	bonus->limiter.reset(); // Sir Mullich's army-wide specialty is not a creature line.
	EXPECT_FALSE(matches("core:griffin"));
	EXPECT_FALSE(newHorizonsCreatures::isCommandSpecialistCreature(
		hero, development, context.magic, std::nullopt, CreatureID::NONE));
	bonus->type = BonusType::SPECIAL_UPGRADE;
	bonus->subtype = BonusSubtypeID(unit("core:mage"));
	bonus->parameters = std::make_shared<BonusParameters>(unit("core:griffin"));
	EXPECT_TRUE(matches("core:griffin"));
	EXPECT_TRUE(matches("core:royalGriffin"));
	EXPECT_FALSE(matches("core:mage"));
}

void expectPrototypeCommandLegality(const CHero & hero, const StartingHeroContext & context,
	const newHorizonsCreatures::CreatureCategoryRules & categories)
{
	const auto original = hero.initialArmy;
	const auto projection = projectStartingHero(hero, context);
	const auto development = resolveHeroRules(context.development, hero.heroClass->getId());
	const auto capabilities = resolveCapabilityRules(context.capabilities, hero.heroClass->getId());
	const auto rules = capturedCommandRules(capabilities);
	ASSERT_TRUE(rules);
	const SecondarySkill authority(SecondarySkill::decode("new-horizons:authority"));
	int rank = 0;
	for(const auto & [skill, mastery] : projection.skills)
		if(skill == authority) rank = mastery;
	const auto command = commandValue(*rules, 1, rank);
	ASSERT_EQ(command.natural, 1); // No extra starting Command to hide an invalid package.
	ASSERT_GE(projection.army.size(), original.size()); // Guaranteed tents may append equipment.
	for(size_t index = 0; index < original.size(); ++index)
	{
		EXPECT_EQ(projection.army[index].minAmount, original[index].minAmount);
		EXPECT_EQ(projection.army[index].maxAmount, original[index].maxAmount);
		EXPECT_EQ(hero.initialArmy[index].creature, original[index].creature);
		EXPECT_EQ(hero.initialArmy[index].minAmount, original[index].minAmount);
		EXPECT_EQ(hero.initialArmy[index].maxAmount, original[index].maxAmount);
	}
	// Every possible stack-selection subset, at both count endpoints. Machines
	// still consume their original roll but never an army slot.
	ASSERT_LT(projection.army.size(), 8);
	for(size_t mask = 0; mask < (size_t{1} << projection.army.size()); ++mask)
		for(const bool maximum : {false, true})
		{
			std::vector<CommandPlacementStack> rows;
			for(size_t index = 0; index < projection.army.size(); ++index)
			{
				const auto & row = projection.army[index];
				if(!(mask & (size_t{1} << index)) || row.creature.toCreature()->warMachine != ArtifactID::NONE)
					continue;
				CommandCreatureTraits traits;
				if(const auto category = newHorizonsCreatures::creatureCategoryView(categories, row.creature))
					traits.category = category->category;
				traits.assimilation = newHorizonsCreatures::hasIntrinsicAssimilation(row.creature);
				if(newHorizonsCreatures::isCommandSpecialistCreature(hero, development, context.magic,
					defaultCreatureLineTarget(development, hero.getId()), row.creature))
					traits.category = newHorizonsCreatures::CreatureCategory::CORE;
				rows.push_back({row.creature, maximum ? row.maxAmount : row.minAmount, traits, SlotID(rows.size())});
			}
			const auto plan = planCommandPlacement(command.effective(), rows);
			ASSERT_TRUE(plan) << "stack selection " << mask << ", maximum counts " << maximum;
			EXPECT_EQ(plan->size(), rows.size());
		}
}

TEST(CommandStartingPackagePolicy, EveryRegisteredPrototypeDefaultIsLegalAtCapturedLevelOne)
{
	const auto context = configured();
	const newHorizonsCreatures::CreatureCategoryRules categories(
		JsonNode(JsonPath::builtin("config/newHorizonsCreatureCategories")));
	size_t checked = 0;
	for(const auto & hero : LIBRARY->heroh->objects)
		if(hero)
		{
			SCOPED_TRACE(hero->getJsonKey());
			// No special/campaign/portrait/Chronicles filter: consume the whole live registry.
			EXPECT_NO_THROW({ EXPECT_NO_FATAL_FAILURE(expectPrototypeCommandLegality(*hero, context, categories)); });
			++checked;
		}
	EXPECT_GE(checked, 163);
}

class CommandStartingPackageWorldTest : public TinyMapGameTest
{
protected:
	Services * gameServices() override { return LIBRARY; }
	void configureWorldInitialization(StartInfo &, GameRandomizer & rng) const override { rng.setSeed(1337); }
	void mapLoaded(CMap * loaded) override
	{
		TinyMapGameTest::mapLoaded(loaded);
		const auto context = configured();
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, context.development);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_CAPABILITIES, context.capabilities);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, context.perks);
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, context.magic);
		const JsonNode combat(JsonPath::builtin("config/newHorizonsCombat"));
		loaded->overrideGameSetting(EGameSettings::COMBAT_HERO_COMMANDS, combat["combat"]["heroCommands"]);
		// Deliberately leave all map/pool hero instances and allowed-hero bits untouched.
	}
	void prepare()
	{
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).playerActive(PlayerColor(0)).playerActive(PlayerColor(1))
			.hero({5, 5, 0}, prototype("core:ivor").getId(), PlayerColor(0))
			.heroGarrison({{unit("core:pikeman"), 157}})
			.hero({8, 8, 0}, prototype("core:solmyr").getId(), PlayerColor(1))
			.heroGarrison({{unit("core:pikeman"), 163}});
		startWithMap(std::move(builder));
	}
};

TEST_F(CommandStartingPackageWorldTest, TinyNewGameInitializesUntouchedPoolAndKeepsAuthoredTroops)
{
	ASSERT_NO_FATAL_FAILURE(prepare()); // Previously throws for core:dracon before the test body.
	EXPECT_EQ(map()->getHero(prototype("core:ivor").getId())->getStackCount(SlotID(0)), 157);
	EXPECT_EQ(map()->getHero(prototype("core:solmyr").getId())->getStackCount(SlotID(0)), 163);
	for(const auto * key : {"core:dracon", "core:gelu"})
	{
		const auto * hero = map()->tryGetFromHeroPool(prototype(key).getId());
		ASSERT_NE(hero, nullptr) << key;
		ASSERT_TRUE(hero->getArmyCommand());
		EXPECT_EQ(hero->level, 1);
		EXPECT_EQ(hero->getArmyCommand()->natural, 1);
		EXPECT_EQ(hero->getArmyCommand()->effective(), 1);
		const auto target = unit(std::string(key) == "core:dracon" ? "core:enchanter" : "core:sharpshooter");
		EXPECT_TRUE(newHorizonsCreatures::isCommandSpecialistCreature(*hero, target));
	}
	for(const auto & type : LIBRARY->heroh->objects)
	{
		if(!type) continue;
		const auto * hero = map()->tryGetFromHeroPool(type->getId());
		if(!hero) continue; // On-map heroes and unavailable portraits are covered above/purely.
		SCOPED_TRACE(type->getJsonKey());
		for(const auto & [slot, stack] : hero->Slots())
			EXPECT_EQ(hero->getCommandSlotEligibility(slot, stack->getCreatureID()), CommandPlacementError::NONE);
	}
}

TEST_F(CommandStartingPackageWorldTest, EveryPooledDefaultPreservesSelectionCountRollsAndFollowingRng)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto context = configured();
	const auto chances = map()->getSettings().getVector(EGameSettings::HEROES_STARTING_STACKS_CHANCES);
	for(const auto & type : LIBRARY->heroh->objects)
	{
		if(!type) continue;
		auto * hero = map()->tryGetFromHeroPool(type->getId());
		if(!hero) continue;
		SCOPED_TRACE(type->getJsonKey());
		const auto projection = projectStartingHero(*type, context);
		const auto before = troops(*hero);
		for(int seed = 1; seed <= 16; ++seed)
		{
			SCOPED_TRACE(seed);
			CRandomGenerator actual(seed), reference(seed);
			const auto selection = reference.nextInt(1, 100);
			std::map<CreatureID, TQuantity> expected;
			size_t expectedStacks = 0;
			const auto & original = type->initialArmy;
			for(size_t index = 0; index < std::min(chances.size(), original.size()); ++index)
				if(selection <= chances[index])
				{
					const auto count = reference.nextInt(original[index].minAmount, original[index].maxAmount);
					const auto creature = projection.army[index].creature;
					if(creature.toCreature()->warMachine == ArtifactID::NONE)
					{
						expected[creature] += count;
						++expectedStacks;
					}
				}
			CSimpleArmy generated;
			ASSERT_NO_THROW(hero->initArmy(actual, &generated));
			EXPECT_EQ(generated.army.size(), expectedStacks);
			std::map<CreatureID, TQuantity> observed;
			for(const auto & [slot, row] : generated.army)
			{
				observed[row.first] += row.second;
				EXPECT_EQ(hero->getCommandSlotEligibility(slot, row.first), CommandPlacementError::NONE);
			}
			EXPECT_EQ(observed, expected);
			for(int next = 0; next < 8; ++next)
				EXPECT_EQ(actual.nextInt(), reference.nextInt());
		}
		EXPECT_EQ(troops(*hero), before); // Hiring preview never changes the actual pool army.
	}
}
}
