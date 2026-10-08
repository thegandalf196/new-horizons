/*
 * NewHorizonsHeroSpellAccessTest.cpp, part of VCMI engine
 *
 * License: GNU General Public License v2 or later; see license.txt
 */
#include "StdInc.h"
#include "BattleTestFixture.h"

#include "../../../lib/GameConstants.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/bonuses/BonusSelector.h"
#include "../../../lib/callback/GameRandomizer.h"
#include "../../../lib/entities/hero/CHero.h"
#include "../../../lib/gameState/CGameState.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/mapObjects/CGTownInstance.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/ISpellMechanics.h"
#include "../../../lib/spells/NewHorizonsMagic.h"
#include "../../../lib/spells/NewHorizonsSpellAvailability.h"
#include "../../../server/CGameHandler.h"
#include "../../../server/battles/BattleProcessor.h"

#include <algorithm>
#include <array>
#include <string>
#include <string_view>

namespace
{
constexpr std::array<std::string_view, 35> HERO_RESTRICTED_SPELLS = {
	"core:stoneSkin", "core:bloodlust", "core:prayer",
	"core:precision", "core:slayer", "core:disruptingRay",
	"core:airElemental", "core:antiMagic", "core:blind", "core:counterstrike",
	"core:deathRipple", "core:destroyUndead", "core:disguise", "core:earthElemental",
	"core:fireElemental", "core:fireShield", "core:forceField", "core:fortune",
	"core:frenzy", "core:hypnotize", "core:magicMirror", "core:mirth",
	"core:protectAir", "core:protectEarth", "core:protectFire", "core:protectWater",
	"core:removeObstacle", "core:sacrifice", "core:scuttleBoat", "core:viewAir",
	"core:viewEarth", "core:visions", "core:waterElemental",
	"core:shield", "core:airShield"
};

constexpr std::array<std::string_view, 4> CONFLUX_PROTECTION_SPELLS = {
	"core:protectAir", "core:protectEarth", "core:protectFire", "core:protectWater"
};

struct StartingSpellHero
{
	std::string_view hero;
	std::string_view spell;
};

constexpr std::array<StartingSpellHero, 13> HERO_STARTING_SPELLS = {{
	{"core:labetha", "core:stoneSkin"},
	{"core:inteus", "core:bloodlust"},
	{"core:loynis", "core:prayer"},
	{"core:zubin", "core:precision"},
	{"core:coronius", "core:slayer"},
	{"core:aenain", "core:disruptingRay"},
	{"core:piquedram", "core:shield"},
	{"core:neela", "core:shield"},
	{"core:theodorus", "core:shield"},
	{"core:styg", "core:shield"},
	{"core:galthran", "core:shield"},
	{"core:nimbus", "core:shield"},
	{"core:jaegar", "core:shield"}
}};

SpellID spellNamed(std::string_view identifier)
{
	return SpellID(SpellID::decode(std::string(identifier)));
}

HeroTypeID heroTypeNamed(std::string_view identifier)
{
	return HeroTypeID(HeroTypeID::decode(std::string(identifier)));
}

bool hasEffect(const CStack * stack, SpellID spell)
{
	return stack->hasBonus(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(spell)));
}

class NewHorizonsHeroSpellAccessTest : public BattleTestFixture
{
protected:
	bool useOlderRowsWithoutHeroAccess = false;

	void SetUp() override
	{
		BattleTestFixture::SetUp();
		const auto & activeMods = LIBRARY->modh->getActiveMods();
		if(std::find(activeMods.begin(), activeMods.end(), GameConstants::NEW_HORIZONS_MOD_SCOPE) == activeMods.end())
			GTEST_SKIP() << "Requires the activated New Horizons preset";
	}

	void mapLoaded(CMap * loaded) override
	{
		BattleTestFixture::mapLoaded(loaded);
		JsonNode rules(JsonPath::builtin("config/newHorizonsMagic"));
		if(useOlderRowsWithoutHeroAccess)
		{
			for(const auto identifier : HERO_RESTRICTED_SPELLS)
			{
				auto & row = rules["spells"][std::string(identifier)].Struct();
				row.erase("active");
				row.erase("ordinaryAcquisition");
				row.erase("heroAccess");
			}
			// This models the actual saved snapshot rather than merging missing
			// fields back from the currently installed profile.
			rules.setOverrideFlag(true);
		}
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, std::move(rules));

		// Exercise the real guild producer's authored-mandatory and possible-spell
		// filters. A valid preferred-school control remains in the mandatory set.
		for(auto * town : loaded->getObjects<CGTownInstance>())
		{
			town->obligatorySpells = {spellNamed("core:magicArrow")};
			for(const auto identifier : HERO_RESTRICTED_SPELLS)
				town->obligatorySpells.push_back(spellNamed(identifier));
			town->possibleSpells.clear();
		}
	}

	void startRosterGame()
	{
		const CreatureID token(0);
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false)
			.name("NewHorizonsHeroSpellAccess")
			.playerActive(PlayerColor(0))
			.playerActive(PlayerColor(1))
			.town({30, 30, 0}, FactionID::CASTLE, PlayerColor(0))
			.town({8, 24, 0}, FactionID::CONFLUX, PlayerColor(0))
			.hero({5, 5, 0}, heroTypeNamed(HERO_STARTING_SPELLS.front().hero), PlayerColor(0))
			.heroGarrison({{token, 1}});

		for(size_t index = 1; index < HERO_STARTING_SPELLS.size(); ++index)
			builder.hero({5 + static_cast<int32_t>(index % 6) * 2,
				5 + static_cast<int32_t>(index / 6) * 4, 0},
				heroTypeNamed(HERO_STARTING_SPELLS[index].hero), PlayerColor(0));

		builder.hero({25, 8, 0}, heroTypeNamed("core:gem"), PlayerColor(0));
		builder.hero({30, 5, 0}, heroTypeNamed("core:christian"), PlayerColor(1))
			.heroGarrison({{token, 1}});
		startWithMap(std::move(builder));

		server.gameState = gameState();
		gameHandler = std::make_shared<CGameHandler>(server, gameState());
		gameHandler->randomizer->setSeed(seed);
		attackerSideHero = findHeroByOwner(PlayerColor(0));
		defenderSideHero = findHeroByOwner(PlayerColor(1));
		for(auto * candidate : findAll<CGTownInstance>())
		{
			if(candidate->getFactionID() == FactionID::CASTLE)
				town = candidate;
			else if(candidate->getFactionID() == FactionID::CONFLUX)
				wisdomTown = candidate;
		}
		ASSERT_NE(attackerSideHero, nullptr);
		ASSERT_NE(defenderSideHero, nullptr);
		ASSERT_NE(town, nullptr);
		ASSERT_EQ(town->getFactionID(), FactionID::CASTLE);
		ASSERT_NE(wisdomTown, nullptr);
	}

	CGHeroInstance * heroNamed(std::string_view identifier) const
	{
		for(auto * hero : findAll<CGHeroInstance>())
			if(hero->getHeroType()->getJsonKey() == std::string(identifier))
				return hero;
		return nullptr;
	}

	void ensureSpellbook(CGHeroInstance * hero)
	{
		ASSERT_NE(hero, nullptr);
		if(!hero->hasSpellbook())
		{
			ASSERT_TRUE(gameHandler->giveHeroNewArtifact(hero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK));
		}
	}

	CGTownInstance * town = nullptr;
	CGTownInstance * wisdomTown = nullptr;
};
}

TEST_F(NewHorizonsHeroSpellAccessTest, CurrentProfileFiltersGuildHouseAndFreshHeroSpellbookProducers)
{
	startRosterGame();
	const auto & rules = gameState()->getMagicRules();
	const auto magicArrow = spellNamed("core:magicArrow");
	ASSERT_TRUE(gameState()->getMap().allowedSpells.count(magicArrow));
	ASSERT_TRUE(newHorizonsMagic::spellAllowedByHeroRoster(rules, magicArrow));
	ASSERT_TRUE(newHorizonsMagic::spellAvailableForOrdinaryAcquisition(rules, magicArrow));
	EXPECT_TRUE(vstd::contains(town->spells.at(0), magicArrow))
		<< "Castle's preferred Sorcery slot still receives a canonical eligible spell";

	for(const auto identifier : HERO_RESTRICTED_SPELLS)
	{
		const auto spell = spellNamed(identifier);
		ASSERT_TRUE(spell.hasValue()) << identifier;
		EXPECT_TRUE(gameState()->getMap().allowedSpells.count(spell))
			<< "Absence must come from hero/acquisition policy, not a map ban: " << identifier;
		EXPECT_TRUE(newHorizonsMagic::spellAllowedBySavedRoster(rules, spell)) << identifier
			<< "The underlying world spell definition remains available to effects and creature consumers";
		EXPECT_FALSE(newHorizonsMagic::spellAllowedByHeroRoster(rules, spell)) << identifier;
		EXPECT_FALSE(newHorizonsMagic::spellAvailableForOrdinaryAcquisition(rules, spell)) << identifier;
		for(const auto & level : town->spells)
			EXPECT_FALSE(vstd::contains(level, spell)) << identifier;
	}

	wisdomTown->addBuilding(BuildingID::SPECIAL_2);
	const auto scrollOffers = wisdomTown->availableItemsIds(EMarketMode::RESOURCE_SKILL);
	ASSERT_FALSE(scrollOffers.empty());
	for(const auto & offer : scrollOffers)
	{
		const auto offeredSpell = offer.as<SpellID>();
		ASSERT_TRUE(offeredSpell.hasValue());
		EXPECT_TRUE(newHorizonsMagic::spellAllowedByHeroRoster(rules, offeredSpell));
		EXPECT_TRUE(newHorizonsMagic::spellAvailableForOrdinaryAcquisition(rules, offeredSpell));
		for(const auto identifier : HERO_RESTRICTED_SPELLS)
			EXPECT_NE(offeredSpell, spellNamed(identifier));
	}

	for(const auto & starting : HERO_STARTING_SPELLS)
	{
		auto * hero = heroNamed(starting.hero);
		const auto spell = spellNamed(starting.spell);
		ASSERT_NE(hero, nullptr) << starting.hero;
		ASSERT_TRUE(hero->hasSpellbook()) << starting.hero;
		EXPECT_FALSE(hero->spellbookContainsSpell(spell)) << starting.hero << " / " << starting.spell;
		EXPECT_FALSE(hero->isSpellInscribedForCasting(spell));
		EXPECT_TRUE(hero->getSourcesForSpell(spell).empty());
		EXPECT_FALSE(hero->canCastThisSpell(spell.toSpell()));
	}
}

TEST_F(NewHorizonsHeroSpellAccessTest, FreshGemDoesNotReceiveFreeAdventureSpellButKnownInscriptionPersists)
{
	startRosterGame();
	auto * gem = heroNamed("core:gem");
	ASSERT_NE(gem, nullptr);
	ASSERT_TRUE(gem->hasSpellbook());
	const auto summonBoat = spellNamed("core:summonBoat");
	EXPECT_FALSE(gem->spellbookContainsSpell(summonBoat));
	EXPECT_FALSE(gem->canCastThisSpell(summonBoat.toSpell()));
	gem->initHero(*gameHandler->randomizer);
	EXPECT_FALSE(gem->spellbookContainsSpell(summonBoat))
		<< "Reinitializing a captured hero must not restore a free default Adventure grant";
	// An explicit map-authored inscription remains legitimate casting state.
	gem->addSpellToSpellbook(summonBoat);
	EXPECT_TRUE(gem->canCastThisSpell(summonBoat.toSpell()));
	const auto saved = gameState()->saveToMemory();
	CGameState restored;
	restored.preInit(LIBRARY);
	restored.loadFromMemory(saved);
	const auto * restoredGem = restored.getHero(gem->id);
	ASSERT_NE(restoredGem, nullptr);
	EXPECT_TRUE(restoredGem->spellbookContainsSpell(summonBoat));
	EXPECT_TRUE(restoredGem->canCastThisSpell(summonBoat.toSpell()));
}

TEST_F(NewHorizonsHeroSpellAccessTest, ExplicitKnownSpellAndScrollCannotBypassHeroAccess)
{
	startRosterGame();
	auto * hero = heroNamed("core:labetha");
	ASSERT_NE(hero, nullptr);
	ensureSpellbook(hero);
	const auto stoneSkin = spellNamed("core:stoneSkin");
	hero->addSpellToSpellbook(stoneSkin);
	ASSERT_TRUE(gameHandler->giveHeroNewScroll(hero, stoneSkin, ArtifactPosition::MISC1));

	for(const auto identifier : HERO_RESTRICTED_SPELLS)
	{
		const auto spell = spellNamed(identifier);
		ASSERT_TRUE(spell.hasValue()) << identifier;
		hero->addSpellToSpellbook(spell);
		EXPECT_TRUE(hero->spellbookContainsSpell(spell)) << identifier;
		EXPECT_FALSE(hero->isSpellInscribedForCasting(spell)) << identifier;
		EXPECT_TRUE(hero->getSourcesForSpell(spell).empty()) << identifier;
		EXPECT_FALSE(hero->canCastThisSpell(spell.toSpell())) << identifier;
	}
	EXPECT_TRUE(hero->hasScroll(stoneSkin, false));
	EXPECT_FALSE(hero->getInscribedSpellsForCasting().contains(stoneSkin));

	// An eligible spell can still be explicitly known and castable in this profile.
	const auto magicArrow = spellNamed("core:magicArrow");
	hero->addSpellToSpellbook(magicArrow);
	EXPECT_TRUE(newHorizonsMagic::spellAllowedByHeroRoster(gameState()->getMagicRules(), magicArrow));
	EXPECT_TRUE(hero->isSpellInscribedForCasting(magicArrow));
	EXPECT_FALSE(hero->getSourcesForSpell(magicArrow).empty());
	EXPECT_TRUE(hero->canCastThisSpell(magicArrow.toSpell()));
}

TEST_F(NewHorizonsHeroSpellAccessTest, RejectedHeroActionDoesNotSpendManaOrActionAndCreatureCastRemainsValid)
{
	startRosterGame();
	auto * hero = heroNamed("core:labetha");
	ASSERT_NE(hero, nullptr);
	ensureSpellbook(hero);
	const auto stoneSkin = spellNamed("core:stoneSkin");
	hero->addSpellToSpellbook(stoneSkin);
	hero->setNormalSpellPoints(100);
	startBattle();
	auto * friendly = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(leftHex + 1), 10);
	auto * enemy = addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), BattleHex(rightHex + 10), 10);
	auto * ogreMage = addStack(BattleSide::ATTACKER, creatureByName("core:ogreMage"), BattleHex(leftHex + 2), 2);
	auto * masterGenie = addStack(BattleSide::ATTACKER, creatureByName("core:masterGenie"), BattleHex(leftHex + 3), 2);
	auto * stormElemental = addStack(BattleSide::ATTACKER, creatureByName("core:stormElemental"), BattleHex(leftHex + 4), 2);
	ASSERT_NE(friendly, nullptr);
	ASSERT_NE(enemy, nullptr);
	ASSERT_NE(ogreMage, nullptr);
	ASSERT_NE(masterGenie, nullptr);
	ASSERT_NE(stormElemental, nullptr);
	beginCombat();

	const auto * active = battle()->battleActiveUnit();
	ASSERT_NE(active, nullptr);
	ASSERT_EQ(battle()->battleGetActionController(active), PlayerColor(0));
	const auto manaBefore = hero->getManaAvailable();
	const auto round = battle()->getRound();
	const auto actionsBefore = battle()->getHeroActionAllowances(BattleSide::ATTACKER).remainingCounts(round);
	BattleAction rejected;
	rejected.actionType = EActionType::HERO_SPELL;
	rejected.side = BattleSide::ATTACKER;
	rejected.spell = stoneSkin;
	rejected.aimToUnit(friendly);
	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), rejected));
	EXPECT_EQ(hero->getManaAvailable(), manaBefore);
	EXPECT_EQ(battle()->getHeroActionAllowances(BattleSide::ATTACKER).remainingCounts(round), actionsBefore);
	EXPECT_FALSE(battle()->hasCompletedHeroSpellCast(BattleSide::ATTACKER));

	// Ogre Mage's authored Bloodlust Spellcaster remains a real creature-mode
	// producer. heroAccess is not saved-world membership and must not suppress it.
	const auto bloodlust = spellNamed("core:bloodlust");
	ASSERT_TRUE(newHorizonsMagic::spellAllowedBySavedRoster(gameState()->getMagicRules(), bloodlust));
	EXPECT_FALSE(newHorizonsMagic::spellAllowedByHeroRoster(gameState()->getMagicRules(), bloodlust));
	const auto * definition = bloodlust.toSpell();
	ASSERT_NE(definition, nullptr);
	spells::BattleCast creatureCast(battle(), ogreMage, spells::Mode::CREATURE_ACTIVE, definition);
	const spells::Target creatureTarget{spells::Destination(friendly)};
	const auto mechanics = definition->battleMechanics(&creatureCast);
	ASSERT_NE(mechanics, nullptr);
	ASSERT_TRUE(mechanics->canBeCastAt(creatureTarget));
	creatureCast.cast(gameHandler->spellEnv.get(), creatureTarget);
	EXPECT_TRUE(hasEffect(friendly, bloodlust));

	// These engine effects remain world-active and creature-castable even though
	// their legacy spellbook identities are no longer available to heroes.
	for(const auto identifier : {"core:shield", "core:airShield"})
	{
		const auto spell = spellNamed(identifier);
		ASSERT_TRUE(newHorizonsMagic::spellAllowedBySavedRoster(gameState()->getMagicRules(), spell));
		EXPECT_FALSE(newHorizonsMagic::spellAllowedByHeroRoster(gameState()->getMagicRules(), spell));
		const auto * definition = spell.toSpell();
		ASSERT_NE(definition, nullptr);
		spells::BattleCast cast(battle(), masterGenie, spells::Mode::CREATURE_ACTIVE, definition);
		const auto mechanics = definition->battleMechanics(&cast);
		ASSERT_NE(mechanics, nullptr);
		ASSERT_TRUE(mechanics->canBeCastAt(creatureTarget)) << identifier;
		cast.cast(gameHandler->spellEnv.get(), creatureTarget);
		EXPECT_TRUE(hasEffect(friendly, spell)) << identifier;
	}

	for(const auto identifier : CONFLUX_PROTECTION_SPELLS)
	{
		const auto spell = spellNamed(identifier);
		ASSERT_TRUE(newHorizonsMagic::spellAllowedBySavedRoster(gameState()->getMagicRules(), spell)) << identifier;
		EXPECT_FALSE(newHorizonsMagic::spellAllowedByHeroRoster(gameState()->getMagicRules(), spell)) << identifier;
		EXPECT_FALSE(newHorizonsMagic::spellAvailableForOrdinaryAcquisition(gameState()->getMagicRules(), spell))
			<< identifier;
	}
	const auto protectAir = spellNamed("core:protectAir");
	const auto * protectAirDefinition = protectAir.toSpell();
	ASSERT_NE(protectAirDefinition, nullptr);
	spells::BattleCast confluxProtection(battle(), stormElemental,
		spells::Mode::CREATURE_ACTIVE, protectAirDefinition);
	const auto protectionMechanics = protectAirDefinition->battleMechanics(&confluxProtection);
	ASSERT_NE(protectionMechanics, nullptr);
	ASSERT_TRUE(protectionMechanics->canBeCastAt(creatureTarget));
	confluxProtection.cast(gameHandler->spellEnv.get(), creatureTarget);
	EXPECT_TRUE(hasEffect(friendly, protectAir));
}

TEST_F(NewHorizonsHeroSpellAccessTest, OlderSavedRowsWithoutNewMarkersRetainKnownSpellAndRoundTrip)
{
	useOlderRowsWithoutHeroAccess = true;
	startRosterGame();
	auto * hero = heroNamed("core:labetha");
	ASSERT_NE(hero, nullptr);
	const auto stoneSkin = spellNamed("core:stoneSkin");
	const auto & oldRules = gameState()->getMagicRules();
	ASSERT_TRUE(oldRules["spells"]["core:stoneSkin"]["heroAccess"].isNull());
	EXPECT_TRUE(newHorizonsMagic::spellAllowedBySavedRoster(oldRules, stoneSkin));
	EXPECT_TRUE(newHorizonsMagic::spellAllowedByHeroRoster(oldRules, stoneSkin));
	EXPECT_TRUE(newHorizonsMagic::spellAvailableForOrdinaryAcquisition(oldRules, stoneSkin));
	EXPECT_TRUE(hero->spellbookContainsSpell(stoneSkin));
	EXPECT_TRUE(hero->isSpellInscribedForCasting(stoneSkin));
	EXPECT_FALSE(hero->getSourcesForSpell(stoneSkin).empty());
	EXPECT_TRUE(hero->canCastThisSpell(stoneSkin.toSpell()));

	// A prior save with no hero-access field retains authored Shield starters.
	auto * shieldStarter = heroNamed("core:piquedram");
	ASSERT_NE(shieldStarter, nullptr);
	const auto shield = spellNamed("core:shield");
	ASSERT_TRUE(oldRules["spells"]["core:shield"]["heroAccess"].isNull());
	EXPECT_TRUE(shieldStarter->spellbookContainsSpell(shield));
	EXPECT_TRUE(shieldStarter->isSpellInscribedForCasting(shield));
	EXPECT_TRUE(shieldStarter->canCastThisSpell(shield.toSpell()));

	const auto saved = gameState()->saveToMemory();
	CGameState restored;
	restored.preInit(LIBRARY);
	restored.loadFromMemory(saved);
	auto * restoredHero = restored.getHero(hero->id);
	ASSERT_NE(restoredHero, nullptr);
	EXPECT_TRUE(restoredHero->getMagicRules()["spells"]["core:stoneSkin"]["heroAccess"].isNull());
	EXPECT_TRUE(restoredHero->spellbookContainsSpell(stoneSkin));
	EXPECT_TRUE(newHorizonsMagic::spellAllowedByHeroRoster(restoredHero->getMagicRules(), stoneSkin));
	EXPECT_TRUE(restoredHero->canCastThisSpell(stoneSkin.toSpell()));
	const auto * restoredShieldStarter = restored.getHero(shieldStarter->id);
	ASSERT_NE(restoredShieldStarter, nullptr);
	EXPECT_TRUE(restoredShieldStarter->spellbookContainsSpell(shield));
	EXPECT_TRUE(restoredShieldStarter->canCastThisSpell(shield.toSpell()));
}

TEST_F(NewHorizonsHeroSpellAccessTest, CurrentV3ParserRejectsNonBooleanHeroAccess)
{
	JsonNode valid(JsonPath::builtin("config/newHorizonsMagic"));
	ASSERT_NO_THROW(newHorizonsMagic::validateRules(valid));
	for(const auto invalidMarker : {JsonNode(), JsonNode("false"), JsonNode(0)})
	{
		JsonNode invalid = valid;
		invalid["spells"]["core:stoneSkin"]["heroAccess"] = invalidMarker;
		EXPECT_THROW(newHorizonsMagic::validateRules(invalid), std::runtime_error);
	}
}
