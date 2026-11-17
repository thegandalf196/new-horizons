/*
 * NewHorizonsHasteSpecialtyTest.cpp, part of VCMI engine
 * License: GNU General Public License v2 or later; see license.txt
 */
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/bonuses/BonusParameters.h"
#include "../../../lib/entities/hero/CHero.h"
#include "../../../lib/entities/hero/HeroStartingProjection.h"
#include "../../../lib/entities/hero/NewHorizonsHeroRules.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/networkPacks/PacksForLobby.h"
#include "../../../lib/StartInfo.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/ISpellMechanics.h"
#include "../../../lib/spells/NewHorizonsMagic.h"
#include "../../../lib/spells/NewHorizonsSorceryRevision.h"
#include "../../../lib/battle/NewHorizonsFocusMagic.h"

namespace
{
constexpr auto SORCERY = "new-horizons:sorceryMagic";
constexpr auto SPELLCRAFT = "new-horizons:spellcraft";

const Bonus * hastePrototype(const CGHeroInstance * hero)
{
	for(const auto & bonus : hero->getHeroType()->specialty)
		if(bonus->type == BonusType::SPECIAL_PECULIAR_ENCHANT
			&& bonus->subtype == BonusSubtypeID(SpellID(SpellID::HASTE)))
			return bonus.get();
	return nullptr;
}

class NewHorizonsHasteSpecialtyTest : public HeroCommandFixture
{
protected:
	bool currentRoster = false;
	CGHeroInstance * ordinary = nullptr;
	CStack * target = nullptr;
	CStack * controlTarget = nullptr;

	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires New Horizons content";
	}
	void mapLoaded(CMap * loaded) override
	{
		// Haste effect controls use the admitted current issuance policy. An old
		// supported-spell list cannot restore retired hero specialty prototypes.
		useRevisedOrders = true;
		HeroCommandFixture::mapLoaded(loaded);
		JsonNode magic(JsonPath::builtin("config/newHorizonsMagic"));
		// Existing Haste cases are captures from before its hero-roster retirement.
		if(!currentRoster)
			magic["spells"]["core:haste"].Struct().erase("heroAccess");
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, magic);
		JsonNode rules(JsonPath::builtin("config/newHorizonsHeroes"));
		if(currentRoster)
		{
			rules.setOverrideFlag(true);
			loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, rules);
			return;
		}
		rules.setOverrideFlag(true);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, rules);
	}
	void expectMalformedHasteAbsenceRejected()
	{
		auto malformed = gameState()->getHeroDevelopmentRules();
		auto & spells = malformed["nonDamageSpellSpecialties"]["spells"].Vector();
		std::erase_if(spells, [](const JsonNode & spell) { return spell.String() == "core:haste"; });
		ASSERT_FALSE(newHorizonsHeroes::hasHasteSpecialtyRules(malformed));
		EXPECT_THROW(newHorizonsHeroes::validateRemainingSpellSpecialtySerialization(malformed, true), std::runtime_error);
		JsonNode incoming;
		incoming["heroes"]["newHorizons"] = malformed;
		CMemorySerializer raw;
		raw.oser & incoming;
		GameSettings decoded;
		ASSERT_NO_THROW(decoded.loadBase(LIBRARY->settingsHandler->getFullConfig()));
		const auto before = decoded.getFullConfig();
		EXPECT_THROW(decoded.serialize(raw.iser), std::runtime_error);
		EXPECT_EQ(decoded.getFullConfig(), before);
		EXPECT_TRUE(newHorizonsHeroes::hasHasteSpecialtyRules(gameState()->getHeroDevelopmentRules()));
	}
	void prepare(const std::string & specialist = "core:cyra", int rawSpellPower = 67)
	{
		const CreatureID pikeman(CreatureID::decode(currentRoster ? "core:archer" : "core:pikeman"));
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).playerActive(PlayerColor(0)).playerActive(PlayerColor(1))
			.hero({5, 5, 0}, HeroTypeID(HeroTypeID::decode(specialist)), PlayerColor(0))
			.heroGarrison({{pikeman, 100}})
			.hero({7, 7, 0}, HeroTypeID(HeroTypeID::decode("core:solmyr")), PlayerColor(1))
			.heroGarrison({{pikeman, 100}});
		startWithMap(std::move(builder));
		server.gameState = gameState();
		gameHandler = std::make_shared<CGameHandler>(server, gameState());
		gameHandler->randomizer->setSeed(seed);
		attackerSideHero = findHeroAt({5, 5, 0});
		ordinary = findHeroAt({7, 7, 0});
		defenderSideHero = ordinary;
		ASSERT_NE(attackerSideHero, nullptr);
		ASSERT_NE(ordinary, nullptr);
		for(auto * hero : {attackerSideHero, ordinary})
		{
			if(!hero->getArt(ArtifactPosition::SPELLBOOK))
				giveArtifact(hero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
			if(!currentRoster)
				hero->addSpellToSpellbook(SpellID::HASTE);
			hero->setPrimarySkill(PrimarySkill::SPELL_POWER, rawSpellPower, ChangeValueMode::ABSOLUTE);
			hero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode(SORCERY)),
				MasteryLevel::NONE, ChangeValueMode::ABSOLUTE);
			hero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode(SPELLCRAFT)),
				MasteryLevel::NONE, ChangeValueMode::ABSOLUTE);
			if(currentRoster)
				hero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:warcasting")),
					MasteryLevel::NONE, ChangeValueMode::ABSOLUTE);
			setTestSpellPointTotal(hero, 100);
		}
		startBattle();
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
		target = addStack(BattleSide::ATTACKER, pikeman, BattleHex(3, 5), 100);
		controlTarget = addStack(BattleSide::DEFENDER, pikeman, BattleHex(12, 5), 100);
		beginCombat();
		ASSERT_NE(target, nullptr);
		ASSERT_EQ(battle()->battleGetOwner(battle()->battleActiveUnit()), PlayerColor(0));
	}
	int duration(const CGHeroInstance * hero)
	{
		const auto * spell = SpellID(SpellID::HASTE).toSpell();
		spells::BattleCast cast(battle(), hero, spells::Mode::HERO, spell);
		return spell->battleMechanics(&cast)->getEffectDuration();
	}
	int expectedDuration(const CGHeroInstance * hero, int specialty)
	{
		const auto * spell = SpellID(SpellID::HASTE).toSpell();
		const int64_t power = hero->getEffectPower(spell);
		const int64_t divisor = hero->getEffectPowerDivisor(spell);
		const int64_t coefficient = newHorizonsMagic::spellPowerCoefficientBasisPoints(
			battle()->getMagicRules(), hero, SpellID::HASTE);
		const int64_t flat = hero->getEnchantPower(spell) - std::max<int64_t>(1, power / divisor);
		return static_cast<int>(std::max<int64_t>(1, power * coefficient * (100 + specialty)
			/ (divisor * 1'000'000)) + flat);
	}
	void expectPaidCast(int expectedSpeed)
	{
		const auto * spell = SpellID(SpellID::HASTE).toSpell();
		spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, spell);
		const auto mechanics = spell->battleMechanics(&cast);
		spells::Target aim;
		aim.emplace_back(target);
		ASSERT_TRUE(mechanics->canBeCastAt(aim));
		const int predicted = mechanics->getEffectDuration();
		const int cost = battle()->battleGetSpellCost(spell, attackerSideHero);
		const auto before = attackerSideHero->getManaAvailable();
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = BattleSide::ATTACKER;
		action.spell = SpellID::HASTE;
		action.aimToUnit(target);
		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
		EXPECT_EQ(before - attackerSideHero->getManaAvailable(), cost);
		const auto effects = target->getAllBonuses(Selector::source(
			BonusSource::SPELL_EFFECT, BonusSourceID(SpellID(SpellID::HASTE))));
		bool found = false;
		for(const auto & effect : *effects)
			if(effect->type == BonusType::STACKS_SPEED)
			{
				found = true;
				EXPECT_EQ(effect->val, expectedSpeed);
				EXPECT_EQ(effect->turnsRemain, predicted);
			}
		EXPECT_TRUE(found);
	}
};


class CurrentHasteSuccessorTest : public NewHorizonsHasteSpecialtyTest,
	public testing::WithParamInterface<const char *> {};

TEST_P(CurrentHasteSuccessorTest, DefaultsPreviewSpecialtyAndPaidFocusCastAgree)
{
	currentRoster = true;
	useRevisedOrders = true;
	ASSERT_NO_FATAL_FAILURE(prepare(GetParam(), 100));
	const SpellID focus(SpellID::decode("new-horizons:focusMagic"));
	const bool specialist = std::string_view(GetParam()) != "core:fafner";
	const auto * type = attackerSideHero->getHeroType();
	EXPECT_FALSE(attackerSideHero->spellbookContainsSpell(SpellID::HASTE));
	EXPECT_TRUE(attackerSideHero->spellbookContainsSpell(focus));
	const auto context = newHorizonsHeroes::startingHeroContext(gameState()->getSettings());
	const auto projected = newHorizonsHeroes::projectStartingHero(*type, context);
	EXPECT_FALSE(projected.spells.contains(SpellID::HASTE));
	EXPECT_TRUE(projected.spells.contains(focus));
	EXPECT_EQ(newHorizonsSorceryRevision::focusAdditionalRangedPercent(attackerSideHero, focus), specialist ? 10 : 0);
	EXPECT_EQ(attackerSideHero->getNonDamageSpellSpecialtyBonusPercent(focus), 0);
	EXPECT_EQ(attackerSideHero->getNonDamageSpellSpecialtyBonusPercent(SpellID::HASTE), 0);
	if(specialist)
	{
		const auto selected = newHorizonsHeroes::resolveSpecialtyPresentation(*type,
			attackerSideHero->getPrimaryGrowthRules(), attackerSideHero->getMagicRules(), std::nullopt);
		const auto captured = attackerSideHero->getSpecialtyPresentation();
		EXPECT_EQ(selected.spell, focus);
		EXPECT_EQ(captured.spell, focus);
		EXPECT_EQ(selected.name, focus.toSpell()->getNameTranslated());
		EXPECT_EQ(captured.name, selected.name);
		EXPECT_NE(captured.description.find("+10 additional percentage points of ranged damage"), std::string::npos);
		EXPECT_EQ(captured.description.find("Arcane Breach"), std::string::npos);
		EXPECT_EQ(hastePrototype(attackerSideHero), nullptr);
	}
	const auto mana = attackerSideHero->getManaAvailable();
	BattleAction action;
	action.actionType = EActionType::HERO_SPELL;
	action.side = BattleSide::ATTACKER;
	action.spell = focus;
	action.aimToUnit(target);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_LT(attackerSideHero->getManaAvailable(), mana);
	EXPECT_EQ(newHorizonsFocusMagic::charges(*target), 3);
	const auto effects = target->getAllBonuses(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(focus)));
	ASSERT_EQ(effects->size(), 2);
	bool enhanced = false;
	for(const auto & effect : *effects)
	{
		EXPECT_NE(effect->type, BonusType::COMBAT_EVENT_TRIGGER);
		if(effect->type == BonusType::PERCENTAGE_DAMAGE_BOOST)
		{
			enhanced = true;
			EXPECT_EQ(effect->subtype, BonusSubtypeID(BonusCustomSubtype::damageTypeRanged));
			EXPECT_EQ(effect->val, specialist ? 30 : 20);
		}
	}
	EXPECT_TRUE(enhanced);
	CMemorySerializer memory;
	memory.oser & *gameState();
	CGameState copied;
	copied.preInit(LIBRARY);
	memory.iser.cb = &copied;
	memory.iser.loadingGamestate = true;
	memory.iser & copied;
	const auto * restored = copied.getHero(attackerSideHero->id);
	ASSERT_NE(restored, nullptr);
	EXPECT_TRUE(restored->spellbookContainsSpell(focus));
	EXPECT_FALSE(restored->spellbookContainsSpell(SpellID::HASTE));
	EXPECT_EQ(newHorizonsSorceryRevision::focusAdditionalRangedPercent(restored, focus), specialist ? 10 : 0);
	if(specialist) EXPECT_EQ(restored->getSpecialtyPresentation().spell, focus);
}

INSTANTIATE_TEST_SUITE_P(CurrentRetiredHaste, CurrentHasteSuccessorTest,
	testing::Values("core:fafner", "core:cyra", "core:brissa", "core:terek"));

TEST_F(NewHorizonsHasteSpecialtyTest, CyraOrdinaryHasteHasNoRetiredSpecialtyAndKeepsPrototypeAbsent)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto * prototype = hastePrototype(attackerSideHero);
	EXPECT_EQ(prototype, nullptr);
	EXPECT_EQ(attackerSideHero->getNonDamageSpellSpecialtyBonusPercent(SpellID::HASTE), 0);
	EXPECT_EQ(ordinary->getNonDamageSpellSpecialtyBonusPercent(SpellID::HASTE), 0);
	EXPECT_EQ(duration(attackerSideHero), expectedDuration(attackerSideHero, 0));
	EXPECT_EQ(duration(attackerSideHero), duration(ordinary));
	EXPECT_EQ(hastePrototype(attackerSideHero), nullptr);
	ASSERT_NO_FATAL_FAILURE(expectPaidCast(3));
	EXPECT_EQ(hastePrototype(attackerSideHero), nullptr);
}

TEST_F(NewHorizonsHasteSpecialtyTest, BrissaOrdinaryHasteDoesNotInheritRetiredDurationBonus)
{
	ASSERT_NO_FATAL_FAILURE(prepare("core:brissa"));
	EXPECT_EQ(attackerSideHero->getNonDamageSpellSpecialtyBonusPercent(SpellID::HASTE), 0);
	EXPECT_EQ(duration(attackerSideHero), expectedDuration(attackerSideHero, 0));
	ASSERT_NO_FATAL_FAILURE(expectPaidCast(3));
}

TEST_F(NewHorizonsHasteSpecialtyTest, TerekOrdinaryHasteDoesNotInheritRetiredDurationBonus)
{
	ASSERT_NO_FATAL_FAILURE(prepare("core:terek"));
	EXPECT_EQ(attackerSideHero->getHeroType()->getJsonKey(), "core:terek");
	EXPECT_EQ(attackerSideHero->getNonDamageSpellSpecialtyBonusPercent(SpellID::HASTE), 0);
	EXPECT_EQ(duration(attackerSideHero), expectedDuration(attackerSideHero, 0));
	ASSERT_NO_FATAL_FAILURE(expectPaidCast(3));
}

TEST_F(NewHorizonsHasteSpecialtyTest, TerekMalformedHasteAbsenceCannotRecreateRetiredSpeedRider)
{
	ASSERT_NO_FATAL_FAILURE(prepare("core:terek"));
	ASSERT_NO_FATAL_FAILURE(expectMalformedHasteAbsenceRejected());
	EXPECT_EQ(attackerSideHero->getNonDamageSpellSpecialtyBonusPercent(SpellID::HASTE), 0);
	EXPECT_EQ(duration(attackerSideHero), expectedDuration(attackerSideHero, 0));
	ASSERT_NO_FATAL_FAILURE(expectPaidCast(3));
}

TEST_F(NewHorizonsHasteSpecialtyTest, SchoolCoefficientSharesFinalFloorButFlatBonusesDoNotScale)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	attackerSideHero->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::SPELL_DURATION, BonusSource::OTHER, 2, BonusSourceID()));
	for(int rank = 0; rank <= MasteryLevel::EXPERT; ++rank)
	{
		attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode(SORCERY)),
			rank, ChangeValueMode::ABSOLUTE);
		EXPECT_EQ(duration(attackerSideHero), expectedDuration(attackerSideHero, 0)) << rank;
	}
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 0, ChangeValueMode::ABSOLUTE);
	EXPECT_EQ(duration(attackerSideHero), 3) << "One-round minimum plus two unscaled flat rounds";
}

TEST_F(NewHorizonsHasteSpecialtyTest, MalformedHasteAbsenceKeepsAdmittedGenericHasteWithoutRetiredRider)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(expectMalformedHasteAbsenceRejected());
	EXPECT_EQ(attackerSideHero->getNonDamageSpellSpecialtyBonusPercent(SpellID::HASTE), 0);
	EXPECT_EQ(duration(attackerSideHero), expectedDuration(attackerSideHero, 0));
	const auto * prototype = hastePrototype(attackerSideHero);
	EXPECT_EQ(prototype, nullptr);
	ASSERT_NO_FATAL_FAILURE(expectPaidCast(3));
}

TEST_F(NewHorizonsHasteSpecialtyTest, CurrentWorldRoundtripRetainsUnrelatedPeculiarEnchantWithoutRetiredPrototype)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto unrelated = std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::SPECIAL_PECULIAR_ENCHANT, BonusSource::OTHER, 0, BonusSourceID(),
		BonusSubtypeID(SpellID(SpellID::HASTE)));
	unrelated->parameters = std::make_shared<BonusParameters>(std::vector<int32_t>{4});
	ordinary->addNewBonus(unrelated);
	const auto heroID = ordinary->id;
	CGameState restored;
	restored.preInit(LIBRARY);
	restored.loadFromMemory(gameState()->saveToMemory());
	const auto * loaded = restored.getHero(heroID);
	ASSERT_NE(loaded, nullptr);
	EXPECT_EQ(loaded->getNonDamageSpellSpecialtyBonusPercent(SpellID::HASTE), 0);
	bool found = false;
	for(const auto & bonus : loaded->getExportedBonusList())
		if(bonus->type == BonusType::SPECIAL_PECULIAR_ENCHANT
			&& bonus->subtype == BonusSubtypeID(SpellID(SpellID::HASTE)))
		{
			found = true;
			ASSERT_NE(bonus->parameters, nullptr);
			EXPECT_EQ(bonus->parameters->toVector(), std::vector<int32_t>{4});
			EXPECT_EQ(bonus->source, BonusSource::OTHER);
		}
	EXPECT_TRUE(found);
	EXPECT_EQ(hastePrototype(loaded), nullptr);
	EXPECT_EQ(unrelated->parameters->toVector(), std::vector<int32_t>{4});
}

TEST_F(NewHorizonsHasteSpecialtyTest, ExplicitDurationOverrideAndUnrelatedProducerRemainUnchanged)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto unrelated = std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::SPECIAL_ADD_VALUE_ENCHANT, BonusSource::OTHER, 7, BonusSourceID(),
		BonusSubtypeID(SpellID(SpellID::FIREBALL)));
	attackerSideHero->addNewBonus(unrelated);
	EXPECT_EQ(attackerSideHero->valOfBonuses(BonusType::SPECIAL_ADD_VALUE_ENCHANT,
		BonusSubtypeID(SpellID(SpellID::FIREBALL))), 7);
	const auto * spell = SpellID(SpellID::HASTE).toSpell();
	spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, spell);
	cast.setEffectDuration(2);
	EXPECT_EQ(spell->battleMechanics(&cast)->getEffectDuration(), 2);
}

TEST_F(NewHorizonsHasteSpecialtyTest, UnrelatedHastePeculiarEnchantStillUsesItsAuthoredParameters)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto unrelated = std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::SPECIAL_PECULIAR_ENCHANT, BonusSource::OTHER, 0, BonusSourceID(),
		BonusSubtypeID(SpellID(SpellID::HASTE)));
	unrelated->parameters = std::make_shared<BonusParameters>(std::vector<int32_t>{4});
	ordinary->addNewBonus(unrelated);
	const auto * spell = SpellID(SpellID::HASTE).toSpell();
	spells::BattleCast cast(battle(), ordinary, spells::Mode::HERO, spell);
	spells::Target aim;
	aim.emplace_back(controlTarget);
	cast.applyEffects(gameHandler->spellcastEnvironment(), aim);
	const auto effects = controlTarget->getAllBonuses(Selector::source(
		BonusSource::SPELL_EFFECT, BonusSourceID(SpellID(SpellID::HASTE))));
	bool found = false;
	for(const auto & effect : *effects)
		if(effect->type == BonusType::STACKS_SPEED)
		{
			found = true;
			EXPECT_EQ(effect->val, 7);
		}
	EXPECT_TRUE(found);
	EXPECT_EQ(unrelated->parameters->toVector(), std::vector<int32_t>{4});
	EXPECT_EQ(ordinary->getNonDamageSpellSpecialtyBonusPercent(SpellID::HASTE), 0);
}

TEST_F(NewHorizonsHasteSpecialtyTest, PreviousVeteranWriterRejectsBeforeHeroMapWorldAndLobbyPrefixes)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto reject = [](auto & value)
	{
		CMemorySerializer memory;
		memory.oser.version = ESerializationVersion::NEW_HORIZONS_VETERAN_COHESION;
		EXPECT_THROW(value.serialize(memory.oser), std::runtime_error);
		EXPECT_TRUE(memory.extractBuffer().empty());
	};
	reject(*attackerSideHero);
	reject(gameState()->getMap());
	reject(*gameState());
	LobbyStartGame lobby;
	lobby.initializedStartInfo = std::make_shared<StartInfo>(*gameState()->getStartInfo());
	lobby.initializedGameState = gameState();
	reject(lobby);
	GameSettings settings;
	const auto authoredRules = gameState()->getHeroDevelopmentRules();
	settings.addOverride(EGameSettings::HEROES_NEW_HORIZONS,
		authoredRules);
	reject(settings);
	// A map with no instantiated heroes still carries its raw authored profile.
	CMap emptyMap(gameState().get());
	emptyMap.overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS,
		authoredRules);
	reject(emptyMap);
}

TEST_F(NewHorizonsHasteSpecialtyTest, MapPreflightIncludesNonIndexedObjectsAndOffMapHeroes)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	CMap isolated(gameState().get());
	EXPECT_NO_THROW(isolated.validateNewHorizonsHasteSpecialtySerialization(false));
	const auto objectID = attackerSideHero->id.getNum();
	const auto heroID = attackerSideHero->getHeroTypeID();
	const auto retained = std::dynamic_pointer_cast<CGHeroInstance>(
		gameState()->getMap().objects[objectID]);
	ASSERT_NE(retained, nullptr);
	isolated.objects.push_back(retained);
	// The object roster, including prison heroes, is not the heroesOnMap index.
	EXPECT_THROW(isolated.validateNewHorizonsHasteSpecialtySerialization(false), std::runtime_error);
	isolated.objects.clear();
	isolated.addToHeroPool(retained);
	ASSERT_EQ(isolated.tryGetFromHeroPool(heroID), retained.get());
	ASSERT_TRUE(isolated.objects.empty());
	EXPECT_THROW(isolated.validateNewHorizonsHasteSpecialtySerialization(false), std::runtime_error);
}

TEST_F(NewHorizonsHasteSpecialtyTest, MalformedHasteAbsenceRejectsCurrentReadAndKeepsOldWriterGuard)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	EXPECT_TRUE(newHorizonsMagic::rulesActive(gameState()->getMagicRules()));
	EXPECT_TRUE(newHorizonsHeroes::usesPerkRules(gameState()->getHeroPerkRules()));
	ASSERT_NO_FATAL_FAILURE(expectMalformedHasteAbsenceRejected());
	EXPECT_TRUE(newHorizonsHeroes::hasHasteSpecialtyRules(gameState()->getHeroDevelopmentRules()));
	EXPECT_EQ(hastePrototype(attackerSideHero), nullptr);
	const auto rejectOldWriter = [](auto & value)
	{
		CMemorySerializer memory;
		memory.oser.version = ESerializationVersion::NEW_HORIZONS_VETERAN_COHESION;
		EXPECT_THROW(value.serialize(memory.oser), std::runtime_error);
		EXPECT_TRUE(memory.extractBuffer().empty());
	};
	rejectOldWriter(*attackerSideHero);
	rejectOldWriter(gameState()->getMap());
	rejectOldWriter(*gameState());
	LobbyStartGame lobby;
	lobby.initializedStartInfo = std::make_shared<StartInfo>(*gameState()->getStartInfo());
	lobby.initializedGameState = gameState();
	rejectOldWriter(lobby);
	JsonNode captured;
	captured["heroes"]["newHorizons"] = gameState()->getHeroDevelopmentRules();
	ASSERT_TRUE(newHorizonsHeroes::hasHasteSpecialtyRules(captured["heroes"]["newHorizons"]));
	const auto & withHaste = captured;
	EXPECT_THROW(newHorizonsHeroes::validateHasteSpecialtySerialization(withHaste["heroes"]["newHorizons"], false), std::runtime_error);
	CMemorySerializer raw;
	raw.oser & withHaste;
	raw.iser.version = ESerializationVersion::NEW_HORIZONS_VETERAN_COHESION;
	GameSettings decoded;
	ASSERT_NO_THROW(decoded.loadBase(LIBRARY->settingsHandler->getFullConfig()));
	const auto before = decoded.getFullConfig();
	EXPECT_THROW(decoded.serialize(raw.iser), std::runtime_error);
	EXPECT_EQ(decoded.getFullConfig(), before);
	CMemorySerializer current;
	current.oser & captured;
	ASSERT_NO_THROW(decoded.serialize(current.iser));
	EXPECT_TRUE(newHorizonsHeroes::hasHasteSpecialtyRules(
		decoded.getValue(EGameSettings::HEROES_NEW_HORIZONS)));
}


}
