/*
 * NewHorizonsMagicStateTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include "../../spells/NewHorizonsMagicProfileFixture.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/modding/ActiveModsInSaveList.h"
#include "../../../lib/modding/ModDescription.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/ISpellMechanics.h"
#include "../../../lib/spells/NewHorizonsMagic.h"
#include "../../../lib/constants/StringConstants.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../lib/battle/CObstacleInstance.h"
#include "../../../lib/bonuses/BonusParameters.h"
#include "../../../lib/bonuses/Limiters.h"
#include "../../../lib/bonuses/Propagators.h"
#include "../../../lib/bonuses/Updaters.h"

namespace
{
JsonNode magicRulesForVersion(int version)
{
	JsonNode rules(JsonPath::builtin("config/newHorizonsMagic"));
	if(version == newHorizonsMagic::CURRENT_RULESET_VERSION)
		return rules;

	rules["rulesetVersion"].Integer() = version;
	rules.Struct().erase("schoolRankPowerCoefficientPercent");
	if(version == newHorizonsMagic::DIRECT_DAMAGE_RULESET_VERSION)
	{
		newHorizonsMagic::validateRules(rules);
		return rules;
	}

	rules.Struct().erase("spellPoints");
	rules.Struct().erase("mageGuildGeneration");
	rules.Struct().erase("physicalDamageReductionCapPercent");
	rules.Struct().erase("warcasting");
	auto & spells = rules["spells"].Struct();
	for(auto it = spells.begin(); it != spells.end();)
	{
		if(it->first.starts_with(GameConstants::NEW_HORIZONS_MOD_SCOPE + ':'))
			it = spells.erase(it);
		else
			++it;
	}
	for(auto & [factionId, faction] : rules["factions"].Struct())
	{
		(void)factionId;
		faction["major"] = faction["preferredA"];
		faction["minor"] = faction["preferredB"];
		faction.Struct().erase("preferredA");
		faction.Struct().erase("preferredB");
	}
	for(auto & [name, spell] : rules["spells"].Struct())
	{
		(void)name;
		spell.Struct().erase("active");
		spell.Struct().erase("directDamage");
		spell.Struct().erase("cureAfflictions");
	}
	newHorizonsMagic::validateRules(rules);
	return rules;
}
}

class NewHorizonsMagicStateTest : public HeroCommandFixture
{
protected:
	bool useMagic = true;
	int magicVersion = newHorizonsMagic::CURRENT_RULESET_VERSION;
	std::unique_ptr<newHorizonsTest::MagicV1Baseline> baseline;

	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires separate native curated testModSettings preset; baseline profile remains legacy";
		baseline = std::make_unique<newHorizonsTest::MagicV1Baseline>();
	}

	void TearDown() override
	{
		HeroCommandFixture::TearDown();
		baseline.reset();
	}

	void mapLoaded(CMap * map) override
	{
		HeroCommandFixture::mapLoaded(map);
		JsonNode rules;
		if(useMagic)
			rules = magicRulesForVersion(magicVersion);
		map->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, rules);
	}

	void startSkilledHero()
	{
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).playerActive(PlayerColor(0))
			.hero({5, 5, 0}, HeroTypeID(0), PlayerColor(0))
			.heroExperience(0).heroPrimary(2, 2, 3, 10)
			.heroSecondarySkills({{SecondarySkill::AIR_MAGIC, 1}})
			.heroGarrison({{CreatureID(0), 10}})
			.heroEquipped({{ArtifactPosition::SPELLBOOK, ArtifactID::SPELLBOOK}})
			.heroSpells({SpellID::HASTE});
		startWithMap(builder);
	}

	void roundTripMagicProfileBattle()
	{
		startGame();
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		for(const SpellID spell : {SpellID(SpellID::BERSERK), SpellID(SpellID::DISPEL),
			SpellID(SpellID::CHAIN_LIGHTNING), SpellID(SpellID::ICE_BOLT)})
			attackerSideHero->addSpellToSpellbook(spell);
		setTestSpellPointTotal(attackerSideHero, 100);
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 100, ChangeValueMode::ABSOLUTE);

		const auto rules = gameState()->getMagicRules();
		ASSERT_EQ(rules["rulesetVersion"].Integer(), magicVersion);
		const auto saveBytes = gameState()->saveToMemory();
		auto restoredWorld = std::make_shared<CGameState>();
		restoredWorld->preInit(LIBRARY);
		restoredWorld->loadFromMemory(saveBytes);
		ASSERT_EQ(restoredWorld->getMagicRules(), rules);

		startBattle();
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);

		auto * testAttacker = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(2, 5), 1);
		ASSERT_NE(testAttacker, nullptr);
		const auto testAttackerId = testAttacker->unitId();
		auto * target = addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), BattleHex(12, 5), 10000);
		ASSERT_NE(target, nullptr);
		const auto targetId = target->unitId();
		auto * adjacent = addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), BattleHex(13, 5), 10000);
		ASSERT_NE(adjacent, nullptr);
		const auto adjacentId = adjacent->unitId();
		std::vector<uint32_t> chainTargetIds{targetId, adjacentId};
		for(int x = 14; x <= 16; ++x)
		{
			auto * chainTarget = addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), BattleHex(x, 5), 10000);
			ASSERT_NE(chainTarget, nullptr);
			chainTargetIds.push_back(chainTarget->unitId());
		}
		beginCombat();

		const bool legacyMagicRules = magicVersion < newHorizonsMagic::SCHOOL_RANK_POWER_COEFFICIENT_RULESET_VERSION;
		const auto expectedChainLength = legacyMagicRules ? 4 : newHorizonsMagic::CHAIN_LIGHTNING_FIXED_TARGET_COUNT_V3;
		const auto * originalChainSpell = SpellID(SpellID::CHAIN_LIGHTNING).toSpell();
		spells::BattleCast originalChainCast(battle(), attackerSideHero, spells::Mode::HERO, originalChainSpell);
		originalChainCast.setSpellLevel(MasteryLevel::NONE);
		const auto originalChainMechanics = originalChainSpell->battleMechanics(&originalChainCast);
		spells::Target originalChainAim;
		originalChainAim.emplace_back(battle()->battleGetUnitByID(targetId));
		EXPECT_EQ(originalChainMechanics->getAffectedStacks(originalChainAim).size(), static_cast<size_t>(expectedChainLength))
			<< "before BattleStart serialization";

		BattleStart outgoing;
		outgoing.battleID = BattleID(0);
		outgoing.info = CMemorySerializer::deepCopy(*battle(), restoredWorld.get());
		CMemorySerializer wire;
		wire.oser & outgoing;
		wire.iser.cb = restoredWorld.get();
		BattleStart incoming;
		wire.iser & incoming;
		ASSERT_NE(incoming.info, nullptr);
		ASSERT_EQ(incoming.info->getMagicRules(), rules);

		RecordingGameServer receiver;
		receiver.gameState = restoredWorld;
		CGameHandler handler(receiver, restoredWorld);
		handler.sendAndApply(incoming);
		const auto * restoredBattle = restoredWorld->getBattle(BattleID(0));
		ASSERT_NE(restoredBattle, nullptr);
		ASSERT_EQ(restoredBattle->getMagicRules(), rules);

		// BattleStart::localInit reconstructs the original army stacks from the
		// separately loaded world. They are fixture defaults, not part of the
		// synthetic battlefield that was removed and rebuilt above.
		BattleUnitsChanged removeRestoredArmyStacks;
		removeRestoredArmyStacks.battleID = BattleID(0);
		for(const auto * unit : restoredBattle->battleGetAllUnits(false))
		{
			if(unit->alive() && unit->unitId() != testAttackerId && !vstd::contains(chainTargetIds, unit->unitId()))
				removeRestoredArmyStacks.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		}
		ASSERT_FALSE(removeRestoredArmyStacks.changedStacks.empty());
		handler.sendAndApply(removeRestoredArmyStacks);
		for(const auto * unit : restoredBattle->battleGetAllUnits(false))
			EXPECT_TRUE(!unit->alive() || unit->unitId() == testAttackerId || vstd::contains(chainTargetIds, unit->unitId()))
				<< "unexpected restored army stack id " << unit->unitId();
		const auto * restoredHero = restoredWorld->getHero(attackerSideHero->id);
		ASSERT_NE(restoredHero, nullptr);

		enum class EarlierProfileTargeting
		{
			CORE_EXPERT_MASS,
			CURE_V2_SINGLE_TARGET,
			SLOW_SINGLE_TARGET
		};
		struct ExpertRangeExpectation
		{
			const char * name;
			SpellID spell;
			MasteryLevel::Type v3Range;
			MasteryLevel::Type v3Effect;
			EarlierProfileTargeting earlierProfileTargeting = EarlierProfileTargeting::CORE_EXPERT_MASS;
		};
		const std::array<ExpertRangeExpectation, 23> expertRangeExpectations{{
			{"Cure", SpellID(SpellID::CURE), MasteryLevel::NONE, MasteryLevel::EXPERT,
				EarlierProfileTargeting::CURE_V2_SINGLE_TARGET},
			{"Bless", SpellID(SpellID::BLESS), MasteryLevel::ADVANCED, MasteryLevel::EXPERT},
			{"Curse", SpellID(SpellID::CURSE), MasteryLevel::ADVANCED, MasteryLevel::EXPERT},
			{"Slow", SpellID(SpellID::SLOW), MasteryLevel::ADVANCED, MasteryLevel::EXPERT,
				EarlierProfileTargeting::SLOW_SINGLE_TARGET},
			{"Dispel", SpellID(SpellID::DISPEL), MasteryLevel::ADVANCED, MasteryLevel::ADVANCED},
			{"Shield", SpellID(SpellID::SHIELD), MasteryLevel::ADVANCED, MasteryLevel::EXPERT},
			{"Air Shield", SpellID(SpellID::AIR_SHIELD), MasteryLevel::ADVANCED, MasteryLevel::EXPERT},
			{"Protection from Air", SpellID(SpellID::PROTECTION_FROM_AIR), MasteryLevel::ADVANCED, MasteryLevel::EXPERT},
			{"Protection from Fire", SpellID(SpellID::PROTECTION_FROM_FIRE), MasteryLevel::ADVANCED, MasteryLevel::EXPERT},
			{"Protection from Water", SpellID(SpellID::PROTECTION_FROM_WATER), MasteryLevel::ADVANCED, MasteryLevel::EXPERT},
			{"Protection from Earth", SpellID(SpellID::PROTECTION_FROM_EARTH), MasteryLevel::ADVANCED, MasteryLevel::EXPERT},
			{"Bloodlust", SpellID(SpellID::BLOODLUST), MasteryLevel::ADVANCED, MasteryLevel::EXPERT},
			{"Precision", SpellID(SpellID::PRECISION), MasteryLevel::ADVANCED, MasteryLevel::EXPERT},
			{"Weakness", SpellID(SpellID::WEAKNESS), MasteryLevel::ADVANCED, MasteryLevel::EXPERT},
			{"Stone Skin", SpellID(SpellID::STONE_SKIN), MasteryLevel::ADVANCED, MasteryLevel::EXPERT},
			{"Prayer", SpellID(SpellID::PRAYER), MasteryLevel::ADVANCED, MasteryLevel::EXPERT},
			{"Mirth", SpellID(SpellID::MIRTH), MasteryLevel::ADVANCED, MasteryLevel::EXPERT},
			{"Sorrow", SpellID(SpellID::SORROW), MasteryLevel::ADVANCED, MasteryLevel::EXPERT},
			{"Fortune", SpellID(SpellID::FORTUNE), MasteryLevel::ADVANCED, MasteryLevel::EXPERT},
			{"Misfortune", SpellID(SpellID::MISFORTUNE), MasteryLevel::ADVANCED, MasteryLevel::EXPERT},
			{"Haste", SpellID(SpellID::HASTE), MasteryLevel::ADVANCED, MasteryLevel::EXPERT},
			{"Counterstrike", SpellID(SpellID::COUNTERSTRIKE), MasteryLevel::ADVANCED, MasteryLevel::EXPERT},
			{"Forgetfulness", SpellID(SpellID::FORGETFULNESS), MasteryLevel::ADVANCED, MasteryLevel::EXPERT},
		}};
		for(const auto & expected : expertRangeExpectations)
		{
			SCOPED_TRACE(expected.name);
			const auto * spell = expected.spell.toSpell();
			ASSERT_NE(spell, nullptr);
			spells::BattleCast expertCast(restoredBattle, restoredHero, spells::Mode::HERO, spell);
			expertCast.setSpellLevel(MasteryLevel::EXPERT);
			const auto mechanics = spell->battleMechanics(&expertCast);
			ASSERT_NE(mechanics, nullptr);

			auto expectedRange = expected.v3Range;
			auto expectedEffect = expected.v3Effect;
			bool expectedMass = false;
			if(legacyMagicRules)
			{
				expectedRange = MasteryLevel::EXPERT;
				expectedEffect = MasteryLevel::EXPERT;
				expectedMass = true;
				if(expected.earlierProfileTargeting == EarlierProfileTargeting::CURE_V2_SINGLE_TARGET
					&& magicVersion >= newHorizonsMagic::DIRECT_DAMAGE_RULESET_VERSION)
				{
					expectedRange = MasteryLevel::NONE;
					expectedMass = false;
				}
				else if(expected.earlierProfileTargeting == EarlierProfileTargeting::SLOW_SINGLE_TARGET)
				{
					expectedRange = MasteryLevel::ADVANCED;
					expectedMass = false;
				}
			}
			const std::vector<spells::AimType> expectedTargets{
				expectedMass ? spells::AimType::NOTHING : spells::AimType::CREATURE};
			EXPECT_EQ(mechanics->getRangeLevel(), expectedRange);
			EXPECT_EQ(mechanics->isMassive(), expectedMass);
			EXPECT_EQ(mechanics->getTargetTypes(), expectedTargets);
			EXPECT_EQ(mechanics->getEffectLevel(), expectedEffect);
		}

		auto * restoredTarget = restoredBattle->battleGetUnitByID(targetId);
		ASSERT_NE(restoredTarget, nullptr);
		auto * restoredAdjacent = restoredBattle->battleGetUnitByID(adjacentId);
		ASSERT_NE(restoredAdjacent, nullptr);
		for(size_t index = 0; index < chainTargetIds.size(); ++index)
		{
			auto * chainTarget = restoredBattle->battleGetUnitByID(chainTargetIds[index]);
			ASSERT_NE(chainTarget, nullptr);
			EXPECT_TRUE(chainTarget->alive());
			EXPECT_EQ(chainTarget->getPosition(), BattleHex(12 + static_cast<int>(index), 5));
		}

		const auto * berserk = SpellID(SpellID::BERSERK).toSpell();
		spells::BattleCast berserkCast(restoredBattle, restoredHero, spells::Mode::HERO, berserk);
		berserkCast.setSpellLevel(MasteryLevel::EXPERT);
		const auto berserkMechanics = berserk->battleMechanics(&berserkCast);
		EXPECT_EQ(berserkMechanics->getTargetTypes(), std::vector<spells::AimType>{
			legacyMagicRules ? spells::AimType::LOCATION : spells::AimType::CREATURE});
		EXPECT_EQ(berserkMechanics->getRangeLevel(), legacyMagicRules ? MasteryLevel::EXPERT : MasteryLevel::NONE);

		const auto * dispel = SpellID(SpellID::DISPEL).toSpell();
		spells::BattleCast dispelCast(restoredBattle, restoredHero, spells::Mode::HERO, dispel);
		dispelCast.setSpellLevel(MasteryLevel::EXPERT);
		const auto dispelMechanics = dispel->battleMechanics(&dispelCast);
		EXPECT_EQ(dispelMechanics->isMassive(), legacyMagicRules);
		EXPECT_EQ(dispelMechanics->getTargetTypes(), std::vector<spells::AimType>{
			legacyMagicRules ? spells::AimType::NOTHING : spells::AimType::CREATURE});
		EXPECT_EQ(dispelMechanics->getRangeLevel(), legacyMagicRules ? MasteryLevel::EXPERT : MasteryLevel::ADVANCED);
		EXPECT_EQ(dispelMechanics->getEffectLevel(), legacyMagicRules ? MasteryLevel::EXPERT : MasteryLevel::ADVANCED);

		const auto * chainLightning = SpellID(SpellID::CHAIN_LIGHTNING).toSpell();
		spells::BattleCast chainCast(restoredBattle, restoredHero, spells::Mode::HERO, chainLightning);
		chainCast.setSpellLevel(MasteryLevel::NONE);
		const auto chainMechanics = chainLightning->battleMechanics(&chainCast);
		EXPECT_EQ(newHorizonsMagic::chainLightningTargetCount(
			restoredBattle->getMagicRules(), chainLightning->getId(), 4), expectedChainLength);
		EXPECT_EQ(chainMechanics->getEffectiveChainLength(4), expectedChainLength);
		spells::Target chainAim;
		chainAim.emplace_back(restoredTarget);
		std::vector<uint32_t> affectedChainTargetIds;
		for(const auto * affected : chainMechanics->getAffectedStacks(chainAim))
			affectedChainTargetIds.push_back(affected->unitId());
		std::sort(affectedChainTargetIds.begin(), affectedChainTargetIds.end());
		auto expectedAffectedChainTargetIds = chainTargetIds;
		expectedAffectedChainTargetIds.resize(expectedChainLength);
		std::sort(expectedAffectedChainTargetIds.begin(), expectedAffectedChainTargetIds.end());
		EXPECT_EQ(affectedChainTargetIds, expectedAffectedChainTargetIds);

		const auto * iceBolt = SpellID(SpellID::ICE_BOLT).toSpell();
		const auto targetHealthBefore = restoredTarget->getAvailableHealth();
		const auto adjacentHealthBefore = restoredAdjacent->getAvailableHealth();
		const auto targetMovementBefore = restoredTarget->getMovementRange();
		const auto targetInitiativeBefore = restoredTarget->getInitiative();
		const auto manaBefore = restoredHero->getManaAvailable();
		EXPECT_EQ(restoredHero->getSpellSchoolLevel(iceBolt), MasteryLevel::NONE);
		ASSERT_GE(targetMovementBefore, 2u);
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = BattleSide::ATTACKER;
		action.spell = iceBolt->getId();
		action.aimToUnit(restoredTarget);
		ASSERT_TRUE(handler.battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
		EXPECT_GT(targetHealthBefore - restoredTarget->getAvailableHealth(), 0);
		EXPECT_EQ(restoredAdjacent->getAvailableHealth(), adjacentHealthBefore);
		EXPECT_EQ(restoredTarget->getMovementRange(), targetMovementBefore - (legacyMagicRules ? 2u : 0u));
		EXPECT_EQ(restoredTarget->getInitiative(), targetInitiativeBefore);
		EXPECT_EQ(restoredHero->getManaAvailable(), manaBefore - restoredHero->getSpellCost(iceBolt));
	}
};

TEST_F(NewHorizonsMagicStateTest, LegacyHeaderDoesNotRequireDisablingCuratedModule)
{
	useMagic = false;
	useCommands = false;
	startSkilledHero();
	const auto legacyWorld = gameState();
	ASSERT_FALSE(newHorizonsHeroes::usesRules(legacyWorld->getHeroDevelopmentRules()));
	ASSERT_FALSE(findHeroByOwner(PlayerColor(0))->getPrimaryGrowthView());
	const auto legacyBytes = legacyWorld->saveToMemory();

	// Exact ActiveModsInSaveList framing of a pre-NH save: all its existing
	// gameplay mods, but not the subsequently installed curated module.
	std::vector<TModID> oldMods;
	for(const auto & mod : LIBRARY->modh->getActiveMods())
		if(mod != GameConstants::NEW_HORIZONS_MOD_SCOPE && LIBRARY->modh->getModInfo(mod).affectsGameplay())
			oldMods.push_back(mod);
	CMemorySerializer header;
	header.oser & oldMods;
	for(const auto & mod : oldMods)
	{
		auto info = LIBRARY->modh->getModInfo(mod).getVerificationInfo();
		header.oser & info;
	}
	ActiveModsInSaveList incoming;
	ASSERT_NO_THROW(header.iser & incoming);
	EXPECT_TRUE(vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE));
	auto restored = std::make_shared<CGameState>();
	restored->preInit(LIBRARY);
	restored->loadFromMemory(legacyBytes);
	EXPECT_EQ(restored->getMagicRules(), legacyWorld->getMagicRules());
	EXPECT_EQ(restored->getActiveSpellSchools().size(), 4u);
	const auto * oldHero = restored->getHero(findHeroByOwner(PlayerColor(0))->id);
	ASSERT_NE(oldHero, nullptr);
	EXPECT_EQ(oldHero->getSecSkillLevel(SecondarySkill::AIR_MAGIC), 1);

	TearDown();
	SetUp();
	useMagic = true;
	useCommands = true;
	startSkilledHero();
	EXPECT_EQ(gameState()->getActiveSpellSchools().size(), 6u);
	EXPECT_EQ(legacyWorld->getActiveSpellSchools().size(), 4u);
}

TEST_F(NewHorizonsMagicStateTest, ActualSchoolRankCostAndServerCastUseSavedClassification)
{
	prepareCommands(true);
	const auto sorcery = SpellSchool::fromSerializationKey("new-horizons:sorcery");
	const SecondarySkill skill(SecondarySkill::decode("new-horizons:sorceryMagic"));
	attackerSideHero->setSecSkillLevel(skill, 3, ChangeValueMode::ABSOLUTE);
	const auto * haste = SpellID(SpellID::HASTE).toSpell();
	const auto originalSchools = haste->schools;
	const auto originalLevel = haste->getLevel();
	SpellSchool best;
	ASSERT_EQ(attackerSideHero->getSpellSchoolLevel(haste, &best), 3);
	EXPECT_EQ(best, sorcery);
	EXPECT_EQ(gameState()->getActiveSpellSchools().size(), 6u);
	EXPECT_EQ(gameState()->getActiveSpellSchools(), battle()->battleGetActiveSpellSchools());
	EXPECT_EQ(attackerSideHero->getSpellSchools(haste), battle()->battleGetSpellSchools(haste->getId()));
	EXPECT_EQ(attackerSideHero->getSpellLevel(haste), battle()->battleGetSpellLevel(haste->getId()));
	const auto cost = attackerSideHero->getSpellCost(haste);
	ASSERT_EQ(cost, newHorizonsMagic::spellCost(gameState()->getMagicRules(), haste->getId(), 3));
	const auto speed = battle()->battleActiveUnit()->getMovementRange();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), heroAction(0)));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), 100 - cost);
	EXPECT_GT(battle()->battleActiveUnit()->getMovementRange(), speed);
	EXPECT_FALSE(battle()->battleCanUseHeroCommand(BattleSide::ATTACKER, HeroCommand::CHARGE));
	EXPECT_EQ(haste->schools, originalSchools);
	EXPECT_EQ(haste->getLevel(), originalLevel);
}

TEST_F(NewHorizonsMagicStateTest, StartingRanksConvertAndNewSkillsCanBeOffered)
{
	startSkilledHero();
	const auto schoolSkills = newHorizonsMagic::schoolSkills(gameState()->getMagicRules());
	EXPECT_EQ(schoolSkills.size(), 6u);
	EXPECT_EQ(std::set<SecondarySkill>(schoolSkills.begin(), schoolSkills.end()).size(), 6u);

	const auto * hero = findHeroByOwner(PlayerColor(0));
	ASSERT_NE(hero, nullptr);
	const SecondarySkill sorcery(SecondarySkill::decode("new-horizons:sorceryMagic"));
	const SecondarySkill light(SecondarySkill::decode("new-horizons:lightMagic"));
	EXPECT_EQ(hero->getSecSkillLevel(SecondarySkill::AIR_MAGIC), 0);
	EXPECT_EQ(hero->getSecSkillLevel(sorcery), 1);
	EXPECT_FALSE(hero->canLearnSkill(SecondarySkill::AIR_MAGIC));
	EXPECT_TRUE(hero->canLearnSkill(light));
	EXPECT_EQ(hero->getSpellSchoolLevel(SpellID(SpellID::HASTE).toSpell()), 1);
}

TEST_F(NewHorizonsMagicStateTest, LegacyWorldKeepsOriginalRankAndCannotOfferNewSkills)
{
	useMagic = false;
	useCommands = false;
	startSkilledHero();
	const auto * hero = findHeroByOwner(PlayerColor(0));
	ASSERT_NE(hero, nullptr);
	EXPECT_FALSE(hero->getPrimaryGrowthView());
	EXPECT_EQ(hero->getSecSkillLevel(SecondarySkill::AIR_MAGIC), 1);
	EXPECT_EQ(hero->getSpellSchoolLevel(SpellID(SpellID::HASTE).toSpell()), 1);
	EXPECT_EQ(gameState()->getActiveSpellSchools().size(), 4u);
	EXPECT_FALSE(hero->canLearnSkill(SecondarySkill(SecondarySkill::decode("new-horizons:lightMagic"))));
}

TEST_F(NewHorizonsMagicStateTest, ActualGameAndBattlePacketRetainSavedRules)
{
	startGame();
	giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
	attackerSideHero->addSpellToSpellbook(SpellID::HASTE);
	setTestSpellPointTotal(attackerSideHero, 100);
	attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:sorceryMagic")), 3, ChangeValueMode::ABSOLUTE);
	const auto rules = gameState()->getMagicRules();
	ASSERT_EQ(rules["rulesetVersion"].Integer(), 3);
	const auto bytes = gameState()->saveToMemory();
	auto restored = std::make_shared<CGameState>();
	restored->preInit(LIBRARY);
	restored->loadFromMemory(bytes);
	EXPECT_EQ(restored->getMagicRules(), rules);
	EXPECT_EQ(restored->getActiveSpellSchools(), gameState()->getActiveSpellSchools());

	startBattle();
	beginCombat();
	BattleStart outgoing;
	outgoing.battleID = BattleID(0);
	outgoing.info = CMemorySerializer::deepCopy(*battle(), restored.get());
	CMemorySerializer wire;
	wire.oser & outgoing;
	wire.iser.cb = restored.get();
	BattleStart incoming;
	wire.iser & incoming;
	ASSERT_NE(incoming.info, nullptr);
	EXPECT_EQ(incoming.info->getMagicRules(), rules);
	EXPECT_EQ(incoming.info->battleGetActiveSpellSchools(), gameState()->getActiveSpellSchools());
	RecordingGameServer receiver;
	receiver.gameState = restored;
	CGameHandler handler(receiver, restored);
	handler.sendAndApply(incoming);
	ASSERT_NE(restored->getBattle(BattleID(0)), nullptr);
	const auto * restoredBattle = restored->getBattle(BattleID(0));
	EXPECT_EQ(restoredBattle->getMagicRules(), rules);
	const auto * hero = restored->getHero(attackerSideHero->id);
	ASSERT_NE(hero, nullptr);
	const auto * spell = SpellID(SpellID::HASTE).toSpell();
	const auto cost = hero->getSpellCost(spell);
	EXPECT_EQ(hero->getSpellSchoolLevel(spell), 3);
	BattleAction action;
	action.actionType = EActionType::HERO_SPELL;
	action.side = BattleSide::ATTACKER;
	action.spell = SpellID::HASTE;
	action.aimToUnit(restoredBattle->battleActiveUnit());
	ASSERT_TRUE(handler.battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_EQ(hero->getManaAvailable(), 100 - cost);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), 100);
}

TEST_F(NewHorizonsMagicStateTest, Version1MagicSaveAndBattlePacketRetainLegacySpellBehavior)
{
	magicVersion = newHorizonsMagic::RULESET_VERSION;
	roundTripMagicProfileBattle();
}

TEST_F(NewHorizonsMagicStateTest, Version2MagicSaveAndBattlePacketRetainLegacySpellBehavior)
{
	magicVersion = newHorizonsMagic::DIRECT_DAMAGE_RULESET_VERSION;
	roundTripMagicProfileBattle();
}

TEST_F(NewHorizonsMagicStateTest, Version3MagicSaveAndBattlePacketKeepNewSpellBehavior)
{
	magicVersion = newHorizonsMagic::CURRENT_RULESET_VERSION;
	roundTripMagicProfileBattle();
}

TEST_F(NewHorizonsMagicStateTest, AdventureSpellUsesOneSharedDailyOpportunityAndRoundTrips)
{
	startGame();
	giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
	const SpellID fly(SpellID::FLY);
	const SpellID waterWalk(SpellID::WATER_WALK);
	const SpellID townPortal(SpellID::TOWN_PORTAL);
	attackerSideHero->addSpellToSpellbook(fly);
	attackerSideHero->addSpellToSpellbook(waterWalk);
	attackerSideHero->addSpellToSpellbook(townPortal);
	setTestSpellPointTotal(attackerSideHero, 200);

	auto cast = [&](SpellID spell) {
		AdventureSpellCastParameters parameters;
		parameters.caster = attackerSideHero;
		parameters.pos = int3();
		auto * environment = dynamic_cast<SpellCastEnvironment *>(gameHandler->spellcastEnvironment());
		EXPECT_NE(environment, nullptr);
		return spell.toSpell()->adventureCast(environment, parameters);
	};

	// No controlled town exists on this map, so Town Portal cancels before
	// applying effects. A cancellation must not consume the shared opportunity.
	const auto manaBeforeCancel = attackerSideHero->getManaAvailable();
	EXPECT_TRUE(cast(townPortal));
	EXPECT_FALSE(attackerSideHero->hasNewHorizonsAdventureSpellCastToday());
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBeforeCancel);

	ASSERT_TRUE(cast(fly));
	EXPECT_TRUE(attackerSideHero->hasNewHorizonsAdventureSpellCastToday());
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBeforeCancel - 60);

	// A different neutral Adventure Spell is rejected by the shared gate, so it
	// cannot spend mana or apply its ordinary adventure effect.
	const auto manaBeforeSecondCast = attackerSideHero->getManaAvailable();
	EXPECT_FALSE(cast(waterWalk));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBeforeSecondCast);
	EXPECT_TRUE(attackerSideHero->hasNewHorizonsAdventureSpellCastToday());

	const auto bytes = gameState()->saveToMemory();
	auto restored = std::make_shared<CGameState>();
	restored->preInit(LIBRARY);
	restored->loadFromMemory(bytes);
	const auto * restoredHero = restored->getHero(attackerSideHero->id);
	ASSERT_NE(restoredHero, nullptr);
	EXPECT_TRUE(restoredHero->hasNewHorizonsAdventureSpellCastToday());

	NewTurn nextDay;
	nextDay.day = restored->day + 1;
	restored->apply(nextDay);
	EXPECT_FALSE(restoredHero->hasNewHorizonsAdventureSpellCastToday());

	NewTurn originalNextDay;
	originalNextDay.day = gameState()->day + 1;
	gameState()->apply(originalNextDay);
	EXPECT_FALSE(attackerSideHero->hasNewHorizonsAdventureSpellCastToday());
	EXPECT_TRUE(cast(waterWalk));
	EXPECT_TRUE(attackerSideHero->hasNewHorizonsAdventureSpellCastToday());
}
