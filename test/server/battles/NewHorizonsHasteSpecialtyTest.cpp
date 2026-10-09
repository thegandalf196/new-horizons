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
#include "../../../lib/entities/hero/NewHorizonsHeroRules.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/networkPacks/PacksForLobby.h"
#include "../../../lib/StartInfo.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/ISpellMechanics.h"
#include "../../../lib/spells/NewHorizonsMagic.h"

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
	bool historicalList = false;
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
		HeroCommandFixture::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
		JsonNode rules(JsonPath::builtin("config/newHorizonsHeroes"));
		// Isolate this feature's admission from the separately authored Thant
		// replacement introduced by a subsequent supported-list boundary.
		std::erase_if(rules["nonDamageSpellSpecialties"]["spells"].Vector(),
			[](const JsonNode & spell) { return spell.String() == "new-horizons:reanimate"
				|| spell.String() == "new-horizons:frailty"; });
		rules.setOverrideFlag(true);
		if(historicalList)
		{
			auto & spells = rules["nonDamageSpellSpecialties"]["spells"].Vector();
			std::erase_if(spells, [](const JsonNode & spell) { return spell.String() == "core:haste"; });
			rules.setOverrideFlag(true);
		}
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, rules);
	}
	void prepare(const std::string & specialist = "core:cyra", int rawSpellPower = 67)
	{
		const CreatureID pikeman(CreatureID::decode("core:pikeman"));
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
			hero->addSpellToSpellbook(SpellID::HASTE);
			hero->setPrimarySkill(PrimarySkill::SPELL_POWER, rawSpellPower, ChangeValueMode::ABSOLUTE);
			hero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode(SORCERY)),
				MasteryLevel::NONE, ChangeValueMode::ABSOLUTE);
			hero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode(SPELLCRAFT)),
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

TEST_F(NewHorizonsHasteSpecialtyTest, CyraPaidCastScalesDurationOnlyAndKeepsPrototype)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto * prototype = hastePrototype(attackerSideHero);
	ASSERT_NE(prototype, nullptr);
	EXPECT_EQ(prototype->parameters, nullptr);
	EXPECT_EQ(attackerSideHero->getNonDamageSpellSpecialtyBonusPercent(SpellID::HASTE), 20);
	EXPECT_EQ(ordinary->getNonDamageSpellSpecialtyBonusPercent(SpellID::HASTE), 0);
	EXPECT_EQ(duration(attackerSideHero), expectedDuration(attackerSideHero, 20));
	EXPECT_GT(duration(attackerSideHero), duration(ordinary));
	EXPECT_NE(attackerSideHero->getSpecialtyDescriptionTranslated().find("Speed bonus is unchanged"), std::string::npos);
	ASSERT_NO_FATAL_FAILURE(expectPaidCast(3));
	EXPECT_EQ(prototype->parameters, nullptr);
}

TEST_F(NewHorizonsHasteSpecialtyTest, BrissaPaidCastUsesTheSameConvertedDuration)
{
	ASSERT_NO_FATAL_FAILURE(prepare("core:brissa"));
	EXPECT_EQ(attackerSideHero->getNonDamageSpellSpecialtyBonusPercent(SpellID::HASTE), 20);
	EXPECT_EQ(duration(attackerSideHero), expectedDuration(attackerSideHero, 20));
	ASSERT_NO_FATAL_FAILURE(expectPaidCast(3));
}

TEST_F(NewHorizonsHasteSpecialtyTest, TerekPaidCastUsesTheSameConvertedDurationWithoutChangingStart)
{
	ASSERT_NO_FATAL_FAILURE(prepare("core:terek"));
	EXPECT_EQ(attackerSideHero->getHeroType()->getJsonKey(), "core:terek");
	EXPECT_EQ(attackerSideHero->getNonDamageSpellSpecialtyBonusPercent(SpellID::HASTE), 20);
	EXPECT_EQ(duration(attackerSideHero), expectedDuration(attackerSideHero, 20));
	ASSERT_NO_FATAL_FAILURE(expectPaidCast(3));
}

TEST_F(NewHorizonsHasteSpecialtyTest, TerekHistoricalListRetainsTheLegacySpeedRider)
{
	historicalList = true;
	ASSERT_NO_FATAL_FAILURE(prepare("core:terek"));
	EXPECT_EQ(attackerSideHero->getNonDamageSpellSpecialtyBonusPercent(SpellID::HASTE), 0);
	EXPECT_EQ(duration(attackerSideHero), expectedDuration(attackerSideHero, 0));
	ASSERT_NO_FATAL_FAILURE(expectPaidCast(6));
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
		EXPECT_EQ(duration(attackerSideHero), expectedDuration(attackerSideHero, 20)) << rank;
	}
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 0, ChangeValueMode::ABSOLUTE);
	EXPECT_EQ(duration(attackerSideHero), 3) << "One-round minimum plus two unscaled flat rounds";
}

TEST_F(NewHorizonsHasteSpecialtyTest, HistoricalSupportedListPreservesLegacySpeedRider)
{
	historicalList = true;
	ASSERT_NO_FATAL_FAILURE(prepare());
	EXPECT_EQ(attackerSideHero->getNonDamageSpellSpecialtyBonusPercent(SpellID::HASTE), 0);
	EXPECT_EQ(duration(attackerSideHero), expectedDuration(attackerSideHero, 0));
	const auto * prototype = hastePrototype(attackerSideHero);
	ASSERT_NE(prototype, nullptr);
	EXPECT_EQ(prototype->parameters, nullptr);
	ASSERT_NO_FATAL_FAILURE(expectPaidCast(6));
}

TEST_F(NewHorizonsHasteSpecialtyTest, CurrentWorldRoundtripRetainsMarkerAndZeroParameterOnlyOnLocalClone)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto heroID = attackerSideHero->id;
	CGameState restored;
	restored.preInit(LIBRARY);
	restored.loadFromMemory(gameState()->saveToMemory());
	const auto * loaded = restored.getHero(heroID);
	ASSERT_NE(loaded, nullptr);
	EXPECT_EQ(loaded->getNonDamageSpellSpecialtyBonusPercent(SpellID::HASTE), 20);
	bool found = false;
	for(const auto & bonus : loaded->getExportedBonusList())
		if(bonus->type == BonusType::SPECIAL_PECULIAR_ENCHANT
			&& bonus->subtype == BonusSubtypeID(SpellID(SpellID::HASTE)))
		{
			found = true;
			ASSERT_NE(bonus->parameters, nullptr);
			EXPECT_EQ(bonus->parameters->toVector(), std::vector<int32_t>{0});
			EXPECT_EQ(bonus->source, BonusSource::HERO_SPECIAL);
			EXPECT_EQ(bonus->sid, BonusSourceID(loaded->getHeroTypeID()));
		}
	EXPECT_TRUE(found);
	ASSERT_NE(hastePrototype(loaded), nullptr);
	EXPECT_EQ(hastePrototype(loaded)->parameters, nullptr);
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
	settings.addOverride(EGameSettings::HEROES_NEW_HORIZONS,
		JsonNode(JsonPath::builtin("config/newHorizonsHeroes")));
	reject(settings);
	// A map with no instantiated heroes still carries its raw authored profile.
	CMap emptyMap(gameState().get());
	emptyMap.overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS,
		JsonNode(JsonPath::builtin("config/newHorizonsHeroes")));
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

TEST_F(NewHorizonsHasteSpecialtyTest, HistoricalListsRemainOldWritableAndRawReadCannotInventHasteAdmission)
{
	historicalList = true;
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto ordinaryOldWriter = [](auto & value)
	{
		CMemorySerializer memory;
		memory.oser.version = ESerializationVersion::NEW_HORIZONS_VETERAN_COHESION;
		EXPECT_NO_THROW(value.serialize(memory.oser));
		EXPECT_FALSE(memory.extractBuffer().empty());
	};
	ordinaryOldWriter(*attackerSideHero);
	ordinaryOldWriter(gameState()->getMap());
	ordinaryOldWriter(*gameState());
	LobbyStartGame lobby;
	lobby.initializedStartInfo = std::make_shared<StartInfo>(*gameState()->getStartInfo());
	lobby.initializedGameState = gameState();
	ordinaryOldWriter(lobby);
	JsonNode captured;
	captured["heroes"]["newHorizons"] = JsonNode(JsonPath::builtin("config/newHorizonsHeroes"));
	std::erase_if(captured["heroes"]["newHorizons"]["nonDamageSpellSpecialties"]["spells"].Vector(),
		[](const JsonNode & spell) { return spell.String() == "new-horizons:reanimate"
			|| spell.String() == "new-horizons:frailty"; });
	CMemorySerializer raw;
	raw.oser & captured; // Simulate old framing carrying a newly supported raw list.
	raw.iser.version = ESerializationVersion::NEW_HORIZONS_VETERAN_COHESION;
	GameSettings decoded;
	try
	{
		decoded.serialize(raw.iser);
		FAIL() << "Old framing must reject a captured Haste list after decoding";
	}
	catch(const std::runtime_error & error)
	{
		EXPECT_NE(std::string(error.what()).find("Haste specialty rules"), std::string::npos);
	}
	std::erase_if(captured["heroes"]["newHorizons"]["nonDamageSpellSpecialties"]["spells"].Vector(),
		[](const JsonNode & spell) { return spell.String() == "core:haste"; });
	CMemorySerializer oldRaw;
	oldRaw.oser & captured;
	oldRaw.iser.version = ESerializationVersion::NEW_HORIZONS_VETERAN_COHESION;
	GameSettings oldDecoded;
	EXPECT_NO_THROW(oldDecoded.serialize(oldRaw.iser));
	EXPECT_FALSE(newHorizonsHeroes::hasHasteSpecialtyRules(
		oldDecoded.getValue(EGameSettings::HEROES_NEW_HORIZONS)));
}

TEST_F(NewHorizonsHasteSpecialtyTest, PreviousVeteranReaderRejectsCapturedHeroAndWorldProfiles)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	CMemorySerializer heroMemory;
	attackerSideHero->serialize(heroMemory.oser);
	heroMemory.iser.version = ESerializationVersion::NEW_HORIZONS_VETERAN_COHESION;
	heroMemory.iser.cb = gameState().get();
	heroMemory.iser.loadingGamestate = true;
	CGHeroInstance restoredHero(gameState().get());
	try
	{
		restoredHero.serialize(heroMemory.iser);
		FAIL() << "Old framing must reject the captured hero Haste profile";
	}
	catch(const std::runtime_error & error)
	{
		EXPECT_NE(std::string(error.what()).find("Haste specialty rules"), std::string::npos);
	}
	CMemorySerializer worldMemory;
	gameState()->serialize(worldMemory.oser);
	worldMemory.iser.version = ESerializationVersion::NEW_HORIZONS_VETERAN_COHESION;
	CGameState restoredWorld;
	restoredWorld.preInit(LIBRARY);
	worldMemory.iser.cb = &restoredWorld;
	worldMemory.iser.loadingGamestate = true;
	try
	{
		restoredWorld.serialize(worldMemory.iser);
		FAIL() << "Old framing must reject a world containing captured Haste rules";
	}
	catch(const std::runtime_error & error)
	{
		EXPECT_NE(std::string(error.what()).find("Haste specialty rules"), std::string::npos);
	}
}
}
