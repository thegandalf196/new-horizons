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
#include "../../../lib/CPlayerState.h"
#include "../../../lib/CSkillHandler.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/modding/ActiveModsInSaveList.h"
#include "../../../lib/modding/ModDescription.h"
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/entities/hero/CHeroClass.h"
#include "../../../lib/entities/artifact/CArtifactInstance.h"
#include "../../../lib/mapObjects/CGTownInstance.h"
#include "../../../lib/mapObjects/MiscObjects.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/ISpellMechanics.h"
#include "../../../lib/spells/NewHorizonsMagic.h"
#include "../../../lib/spells/Problem.h"
#include "../../../lib/spells/adventure/AdventureSpellEffect.h"
#include "../../../lib/spells/adventure/DimensionDoorEffect.h"
#include "../../../lib/constants/StringConstants.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../lib/battle/CObstacleInstance.h"
#include "../../../lib/bonuses/BonusParameters.h"
#include "../../../lib/bonuses/Limiters.h"
#include "../../../lib/bonuses/Propagators.h"
#include "../../../lib/bonuses/Updaters.h"
#include "../../../server/ServerSpellCastEnvironment.h"
#include "../../../server/queries/QueriesProcessor.h"

#include <algorithm>
#include <array>
#include <vector>

namespace
{
class CountingServerSpellCastEnvironment final : public ServerSpellCastEnvironment
{
public:
	explicit CountingServerSpellCastEnvironment(CGameHandler * gameHandler)
		: ServerSpellCastEnvironment(gameHandler)
	{}

	int prepared = 0;
	int completed = 0;

	std::function<void()> prepareAdventureSpellCastCompletion(const spells::Caster * caster, SpellID spell) override
	{
		auto completion = ServerSpellCastEnvironment::prepareAdventureSpellCastCompletion(caster, spell);
		++prepared;

		return [this, completion = std::move(completion)]() mutable
		{
			++completed;
			if(completion)
				completion();
		};
	}
};

JsonNode magicRulesForVersion(int version)
{
	JsonNode rules(JsonPath::builtin("config/newHorizonsMagic"));
	if(version == newHorizonsMagic::CURRENT_RULESET_VERSION)
		return rules;

	rules["rulesetVersion"].Integer() = version;
	rules.Struct().erase("schoolRankPowerCoefficientPercent");
	rules.Struct().erase("spellcraftEfficiencyPercent");
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
	static constexpr auto WISDOM_SKILL = "new-horizons:wisdom";
	static constexpr auto ARCANE_MEMORY = "new-horizons:wisdom.arcaneMemory";
	enum class ArcaneMemoryFixtureMode
	{
		ProductionStatus,
		ActiveForTest
	};
	ArcaneMemoryFixtureMode arcaneMemoryFixtureMode = ArcaneMemoryFixtureMode::ProductionStatus;
	bool useMagic = true;
	int magicVersion = newHorizonsMagic::CURRENT_RULESET_VERSION;
	bool captureOldHavocSpellSchools = false;
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
		{
			rules = magicRulesForVersion(magicVersion);
			if(captureOldHavocSpellSchools)
			{
				for(const auto * spell : {"core:earthquake", "core:implosion"})
				{
					auto & schools = rules["spells"][spell]["schools"].Vector();
					schools.clear();
					schools.emplace_back(std::string("new-horizons:havoc"));
				}
			}
		}
		map->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, rules);

		JsonNode perkRules(JsonPath::builtin("config/newHorizonsPerks"));
		if(arcaneMemoryFixtureMode == ArcaneMemoryFixtureMode::ActiveForTest)
		{
			for(auto & perk : perkRules["skills"][WISDOM_SKILL]["perks"].Vector())
			{
				if(perk["id"].String() == ARCANE_MEMORY)
				{
					perk["effect"]["status"].String() = "active";
					break;
				}
			}
		}
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, perkRules);
	}

	void startGameWithWizard(bool withGuildTown = false)
	{
		const CreatureID token(0);
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).name("ArcaneMemoryTest")
			.playerActive(PlayerColor(0)).playerActive(PlayerColor(1))
			.hero({5, 5, 0}, HeroTypeID(HeroTypeID::decode("core:solmyr")), PlayerColor(0))
			.heroGarrison({{token, 1}})
			.hero({7, 7, 0}, HeroTypeID(1), PlayerColor(1))
			.heroGarrison({{token, 1}});
		if(withGuildTown)
			builder.town({24, 24, 0}, FactionID::CONFLUX, PlayerColor(0)).townGarrison({});
		startWithMap(std::move(builder));

		server.gameState = gameState();
		gameHandler = std::make_shared<CGameHandler>(server, gameState());
		gameHandler->randomizer->setSeed(seed);
		attackerSideHero = findHeroByOwner(PlayerColor(0));
		defenderSideHero = findHeroByOwner(PlayerColor(1));
		ASSERT_NE(attackerSideHero, nullptr);
		ASSERT_NE(defenderSideHero, nullptr);
		ASSERT_EQ(attackerSideHero->getHeroClass()->getJsonKey(), "core:wizard");
	}

	SecondarySkill wisdomSkill() const
	{
		const int decoded = SecondarySkill::decode(WISDOM_SKILL);
		EXPECT_GE(decoded, 0);
		return SecondarySkill(decoded);
	}

	void activateArcaneMemoryForTest()
	{
		arcaneMemoryFixtureMode = ArcaneMemoryFixtureMode::ActiveForTest;
	}

	void prepareWizardWithArcaneMemory(bool withGuildTown = false)
	{
		activateArcaneMemoryForTest();
		startGameWithWizard(withGuildTown);
		attackerSideHero->setSecSkillLevel(wisdomSkill(), MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		attackerSideHero->applyPerkSelection({WISDOM_SKILL, ARCANE_MEMORY});
		ASSERT_TRUE(attackerSideHero->hasActivePerk(WISDOM_SKILL, ARCANE_MEMORY));
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		setTestSpellPointTotal(attackerSideHero, 1000);
	}

	CountingServerSpellCastEnvironment * installCountingSpellEnvironment()
	{
		auto counting = std::make_unique<CountingServerSpellCastEnvironment>(gameHandler.get());
		auto * result = counting.get();
		gameHandler->spellEnv = std::move(counting);
		return result;
	}

	void prepareSummonBoatCast(bool withNewHorizonsRules)
	{
		useMagic = withNewHorizonsRules;
		startGame();

		const SpellID summonBoat(SpellID::SUMMON_BOAT);
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		attackerSideHero->addSpellToSpellbook(summonBoat);
		setTestSpellPointTotal(attackerSideHero, 200);
		// The Sea Captain's Hat grants Expert Summon Boat in the legacy rules;
		// reproduce its spell-specific level without changing spell probability logic.
		attackerSideHero->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
			BonusType::SPELL, BonusSource::OTHER, 3, BonusSourceID(), BonusSubtypeID(summonBoat)));

		const int3 heroPosition = attackerSideHero->visitablePos();
		for(int dx = -1; dx <= 1; ++dx)
		{
			for(int dy = -1; dy <= 1; ++dy)
			{
				if(dx == 0 && dy == 0)
					continue;
				gameState()->getMap().getTile(heroPosition + int3(dx, dy, 0)).terrainType = ETerrainId::WATER;
			}
		}
		ASSERT_EQ(attackerSideHero->getSpellSchoolLevel(summonBoat.toSpell()), 3);
	}

	void prepareDimensionDoorCast(bool withNewHorizonsRules, int movementPoints = 1200)
	{
		useMagic = withNewHorizonsRules;
		startGame();
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		attackerSideHero->addSpellToSpellbook(SpellID(SpellID::DIMENSION_DOOR));
		setTestSpellPointTotal(attackerSideHero, 1000);
		attackerSideHero->setMovementPoints(movementPoints);
	}

	const DimensionDoorEffect * dimensionDoorEffect() const
	{
		return SpellID(SpellID::DIMENSION_DOOR).toSpell()->getAdventureMechanics()
			.getEffectAs<DimensionDoorEffect>(attackerSideHero);
	}

	bool canCastDimensionDoorAt(const int3 & target)
	{
		spells::detail::ProblemImpl problem;
		return SpellID(SpellID::DIMENSION_DOOR).toSpell()->getAdventureMechanics().canBeCastAt(
			problem, &gameHandler->gameInfo(), attackerSideHero, target);
	}

	bool castDimensionDoorAt(const int3 & target)
	{
		AdventureSpellCastParameters parameters;
		parameters.caster = attackerSideHero;
		parameters.pos = target;
		auto * environment = dynamic_cast<SpellCastEnvironment *>(gameHandler->spellcastEnvironment());
		EXPECT_NE(environment, nullptr);
		return SpellID(SpellID::DIMENSION_DOOR).toSpell()->adventureCast(environment, parameters);
	}

	void setTileVisibility(PlayerColor player, const int3 & tile, bool visible)
	{
		const auto teamId = gameState()->players.at(player).team;
		gameState()->teams.at(teamId).fogOfWarMap[tile] = visible ? 1 : 0;
	}

	const IAdventureSpellEffect * summonBoatEffect() const
	{
		return SpellID(SpellID::SUMMON_BOAT).toSpell()->getAdventureMechanics().getEffectAs<IAdventureSpellEffect>(attackerSideHero);
	}

	bool canCastSummonBoatAt(const int3 & target)
	{
		spells::detail::ProblemImpl problem;
		return SpellID(SpellID::SUMMON_BOAT).toSpell()->getAdventureMechanics().canBeCastAt(
			problem, &gameHandler->gameInfo(), attackerSideHero, target);
	}

	bool castSummonBoat(const int3 & target = int3(-1, -1, -1))
	{
		AdventureSpellCastParameters parameters;
		parameters.caster = attackerSideHero;
		parameters.pos = target;
		auto * environment = dynamic_cast<SpellCastEnvironment *>(gameHandler->spellcastEnvironment());
		EXPECT_NE(environment, nullptr);
		return SpellID(SpellID::SUMMON_BOAT).toSpell()->adventureCast(environment, parameters);
	}

	BattleAction heroSpellAction(SpellID spell, const CStack * target) const
	{
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = BattleSide::ATTACKER;
		action.spell = spell;
		action.aimToUnit(target);
		return action;
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
		if(captureOldHavocSpellSchools)
		{
			const SpellID earthquake(SpellID::EARTHQUAKE);
			const SpellID implosion(SpellID::IMPLOSION);

			const auto havoc = SpellSchool::fromSerializationKey("new-horizons:havoc");
			EXPECT_EQ(attackerSideHero->getSpellSchools(earthquake.toSpell()), std::vector<SpellSchool>{havoc});
			EXPECT_EQ(attackerSideHero->getSpellSchools(implosion.toSpell()), std::vector<SpellSchool>{havoc});

			const auto sorcerySkillId = SecondarySkill::decode("new-horizons:sorceryMagic");
			const auto havocSkillId = SecondarySkill::decode("new-horizons:havocMagic");
			ASSERT_GE(sorcerySkillId, 0);
			ASSERT_GE(havocSkillId, 0);
			attackerSideHero->setSecSkillLevel(SecondarySkill(sorcerySkillId), MasteryLevel::EXPERT,
				ChangeValueMode::ABSOLUTE);
			EXPECT_EQ(attackerSideHero->getSpellSchoolLevel(earthquake.toSpell()), 0);
			EXPECT_EQ(attackerSideHero->getSpellSchoolLevel(implosion.toSpell()), 0);
			EXPECT_FALSE(attackerSideHero->canLearnSpell(earthquake.toSpell()));
			EXPECT_FALSE(attackerSideHero->canLearnSpell(implosion.toSpell()));

			attackerSideHero->setSecSkillLevel(SecondarySkill(havocSkillId), MasteryLevel::BASIC,
				ChangeValueMode::ABSOLUTE);
			EXPECT_EQ(attackerSideHero->getSpellSchoolLevel(earthquake.toSpell()), 1);
			EXPECT_EQ(attackerSideHero->getSpellSchoolLevel(implosion.toSpell()), 1);
			EXPECT_TRUE(attackerSideHero->canLearnSpell(earthquake.toSpell()));
			EXPECT_FALSE(attackerSideHero->canLearnSpell(implosion.toSpell()));
			attackerSideHero->setSecSkillLevel(SecondarySkill(havocSkillId), MasteryLevel::ADVANCED,
				ChangeValueMode::ABSOLUTE);
			EXPECT_EQ(attackerSideHero->getSpellSchoolLevel(implosion.toSpell()), 2);
			EXPECT_TRUE(attackerSideHero->canLearnSpell(implosion.toSpell()));
		}
		setTestSpellPointTotal(attackerSideHero, 100);
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 100, ChangeValueMode::ABSOLUTE);

		const auto rules = gameState()->getMagicRules();
		ASSERT_EQ(rules["rulesetVersion"].Integer(), magicVersion);
		const auto saveBytes = gameState()->saveToMemory();
		auto restoredWorld = std::make_shared<CGameState>();
		restoredWorld->preInit(LIBRARY);
		restoredWorld->loadFromMemory(saveBytes);
		ASSERT_EQ(restoredWorld->getMagicRules(), rules);
		if(captureOldHavocSpellSchools)
		{
			const auto * restoredProfileHero = restoredWorld->getHero(attackerSideHero->id);
			ASSERT_NE(restoredProfileHero, nullptr);
			const SpellID earthquake(SpellID::EARTHQUAKE);
			const SpellID implosion(SpellID::IMPLOSION);
			const auto havoc = SpellSchool::fromSerializationKey("new-horizons:havoc");
			EXPECT_EQ(restoredProfileHero->getSpellSchools(earthquake.toSpell()), std::vector<SpellSchool>{havoc});
			EXPECT_EQ(restoredProfileHero->getSpellSchools(implosion.toSpell()), std::vector<SpellSchool>{havoc});
			EXPECT_EQ(restoredProfileHero->getSpellSchoolLevel(earthquake.toSpell()), 2);
			EXPECT_EQ(restoredProfileHero->getSpellSchoolLevel(implosion.toSpell()), 2);
			EXPECT_TRUE(restoredProfileHero->canLearnSpell(earthquake.toSpell()));
			EXPECT_TRUE(restoredProfileHero->canLearnSpell(implosion.toSpell()));
		}

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
		if(captureOldHavocSpellSchools)
		{
			const auto havoc = SpellSchool::fromSerializationKey("new-horizons:havoc");
			EXPECT_EQ(restoredBattle->battleGetSpellSchools(SpellID(SpellID::EARTHQUAKE)),
				std::vector<SpellSchool>{havoc});
			EXPECT_EQ(restoredBattle->battleGetSpellSchools(SpellID(SpellID::IMPLOSION)),
				std::vector<SpellSchool>{havoc});
		}

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
		EXPECT_EQ(restoredHero->getSpellSchoolLevel(iceBolt),
			captureOldHavocSpellSchools ? MasteryLevel::ADVANCED : MasteryLevel::NONE);
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

TEST_F(NewHorizonsMagicStateTest, FreshProfileImplosionAndEarthquakeSchoolsGateActualLearning)
{
	startGame();
	auto * hero = findHeroByOwner(PlayerColor(0));
	ASSERT_NE(hero, nullptr);
	giveArtifact(hero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);

	const SpellID earthquake(SpellID::EARTHQUAKE);
	const SpellID implosion(SpellID::IMPLOSION);
	const auto nature = SpellSchool::fromSerializationKey("new-horizons:nature");
	const auto sorcery = SpellSchool::fromSerializationKey("new-horizons:sorcery");
	const auto havocSkillId = SecondarySkill::decode("new-horizons:havocMagic");
	const auto natureSkillId = SecondarySkill::decode("new-horizons:natureMagic");
	const auto sorcerySkillId = SecondarySkill::decode("new-horizons:sorceryMagic");
	ASSERT_GE(havocSkillId, 0);
	ASSERT_GE(natureSkillId, 0);
	ASSERT_GE(sorcerySkillId, 0);

	EXPECT_EQ(hero->getSpellLevel(earthquake.toSpell()), 3);
	EXPECT_EQ(hero->getSpellLevel(implosion.toSpell()), 4);
	EXPECT_EQ(hero->getSpellSchools(earthquake.toSpell()), std::vector<SpellSchool>{nature});
	EXPECT_EQ(hero->getSpellSchools(implosion.toSpell()), std::vector<SpellSchool>{sorcery});
	hero->setSecSkillLevel(SecondarySkill(havocSkillId), MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	EXPECT_EQ(hero->getSpellSchoolLevel(earthquake.toSpell()), 0);
	EXPECT_EQ(hero->getSpellSchoolLevel(implosion.toSpell()), 0);
	EXPECT_FALSE(hero->canLearnSpell(earthquake.toSpell()));
	EXPECT_FALSE(hero->canLearnSpell(implosion.toSpell()));

	hero->setSecSkillLevel(SecondarySkill(natureSkillId), MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	EXPECT_EQ(hero->getSpellSchoolLevel(earthquake.toSpell()), 1);
	EXPECT_TRUE(hero->canLearnSpell(earthquake.toSpell()));
	hero->setSecSkillLevel(SecondarySkill(sorcerySkillId), MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	EXPECT_EQ(hero->getSpellSchoolLevel(implosion.toSpell()), 1);
	EXPECT_FALSE(hero->canLearnSpell(implosion.toSpell()));
	hero->setSecSkillLevel(SecondarySkill(sorcerySkillId), MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
	EXPECT_EQ(hero->getSpellSchoolLevel(implosion.toSpell()), 2);
	EXPECT_TRUE(hero->canLearnSpell(implosion.toSpell()));
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

TEST_F(NewHorizonsMagicStateTest, CapturedHavocSchoolAssignmentsSurviveWorldAndBattleRoundTrips)
{
	magicVersion = newHorizonsMagic::CURRENT_RULESET_VERSION;
	captureOldHavocSpellSchools = true;
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

TEST_F(NewHorizonsMagicStateTest, SummonBoatRejectsMissingBoatBeforeManaOrDailyStateInSavedNewHorizonsRules)
{
	ASSERT_NO_FATAL_FAILURE(prepareSummonBoatCast(true));
	const auto summonBoat = SpellID(SpellID::SUMMON_BOAT);
	ASSERT_TRUE(newHorizonsMagic::isAdventureSpell(attackerSideHero->getMagicRules(), summonBoat));
	ASSERT_GE(attackerSideHero->bestLocation().x, 0);
	ASSERT_TRUE(gameState()->getMap().getObjects<CGBoat>().empty());

	const auto manaBefore = attackerSideHero->getManaAvailable();
	EXPECT_FALSE(castSummonBoat());
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore);
	EXPECT_FALSE(attackerSideHero->hasNewHorizonsAdventureSpellCastToday());
	EXPECT_TRUE(gameState()->getMap().getObjects<CGBoat>().empty());
}

TEST_F(NewHorizonsMagicStateTest, SummonBoatMovesAnExistingBoatUnderSavedNewHorizonsRules)
{
	ASSERT_NO_FATAL_FAILURE(prepareSummonBoatCast(true));
	setMapVisibility(PlayerColor(0), true);
	const auto summonPosition = attackerSideHero->bestLocation();
	ASSERT_GE(summonPosition.x, 0);
	const auto * effect = summonBoatEffect();
	ASSERT_NE(effect, nullptr);
	ASSERT_TRUE(effect->requiresTargetSelection(attackerSideHero));

	std::vector<int3> offsets;
	attackerSideHero->getOutOffsets(offsets);
	const auto selectedPosition = std::find_if(offsets.begin(), offsets.end(), [&](const int3 & offset)
	{
		const auto candidate = attackerSideHero->visitablePos() + offset;
		return candidate != summonPosition && effect->isTargetInRange(&gameHandler->gameInfo(), attackerSideHero, candidate);
	});
	ASSERT_NE(selectedPosition, offsets.end());
	const auto selectedDestination = attackerSideHero->visitablePos() + *selectedPosition;
	ASSERT_NE(selectedDestination, summonPosition);
	EXPECT_TRUE(canCastSummonBoatAt(selectedDestination));

	const int3 remoteWater(20, 20, 0);
	gameState()->getMap().getTile(remoteWater).terrainType = ETerrainId::WATER;
	gameHandler->createBoat(remoteWater, BoatId::NECROPOLIS, PlayerColor(0));
	ASSERT_EQ(gameState()->getMap().getObjects<CGBoat>().size(), 1u);

	const auto manaBefore = attackerSideHero->getManaAvailable();
	ASSERT_TRUE(castSummonBoat(selectedDestination));

	const auto boats = gameState()->getMap().getObjects<CGBoat>();
	ASSERT_EQ(boats.size(), 1u);
	EXPECT_EQ(boats.front()->visitablePos(), selectedDestination);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore - attackerSideHero->getSpellCost(SpellID(SpellID::SUMMON_BOAT).toSpell()));
	EXPECT_TRUE(attackerSideHero->hasNewHorizonsAdventureSpellCastToday());
}

TEST_F(NewHorizonsMagicStateTest, SummonBoatUsesFirstLegalDestinationForExactNoTargetSentinel)
{
	ASSERT_NO_FATAL_FAILURE(prepareSummonBoatCast(true));
	setMapVisibility(PlayerColor(0), true);
	const int3 remoteWater(20, 20, 0);
	gameState()->getMap().getTile(remoteWater).terrainType = ETerrainId::WATER;
	gameHandler->createBoat(remoteWater, BoatId::NECROPOLIS, PlayerColor(0));

	const auto expectedDestination = attackerSideHero->bestLocation();
	ASSERT_GE(expectedDestination.x, 0);
	ASSERT_TRUE(summonBoatEffect()->requiresTargetSelection(attackerSideHero));
	ASSERT_TRUE(summonBoatEffect()->isTargetInRange(&gameHandler->gameInfo(), attackerSideHero, expectedDestination));
	ASSERT_TRUE(canCastSummonBoatAt(expectedDestination));

	ASSERT_TRUE(castSummonBoat(int3(-1, -1, -1)));
	const auto boats = gameState()->getMap().getObjects<CGBoat>();
	ASSERT_EQ(boats.size(), 1u);
	EXPECT_EQ(boats.front()->visitablePos(), expectedDestination);
	EXPECT_TRUE(attackerSideHero->hasNewHorizonsAdventureSpellCastToday());
}

TEST_F(NewHorizonsMagicStateTest, SummonBoatRejectsIllegalSelectedDestinationsWithoutConsumingCastState)
{
	ASSERT_NO_FATAL_FAILURE(prepareSummonBoatCast(true));
	setMapVisibility(PlayerColor(0), true);
	const auto * effect = summonBoatEffect();
	ASSERT_NE(effect, nullptr);
	ASSERT_TRUE(effect->requiresTargetSelection(attackerSideHero));

	const auto center = attackerSideHero->visitablePos();
	const int3 nonWater = center + int3(0, -1, 0);
	const int3 nonAdjacent = center + int3(0, 2, 0);
	const int3 occupiedWater = center + int3(0, 1, 0);
	const int3 hiddenAdjacent = center + int3(-1, 0, 0);
	const int3 differentLevel = center + int3(0, 1, 1);
	const int3 explicitZero;
	gameState()->getMap().getTile(nonWater).terrainType = ETerrainId::GRASS;
	gameState()->getMap().getTile(nonAdjacent).terrainType = ETerrainId::WATER;
	gameState()->getMap().getTile(explicitZero).terrainType = ETerrainId::GRASS;
	gameState()->getMap().getTile(hiddenAdjacent).terrainType = ETerrainId::WATER;
	const auto teamId = gameState()->players.at(PlayerColor(0)).team;
	gameState()->teams.at(teamId).fogOfWarMap[hiddenAdjacent] = 0;
	ASSERT_TRUE(gameState()->isVisibleFor(occupiedWater, PlayerColor(0)));
	ASSERT_FALSE(gameState()->isVisibleFor(hiddenAdjacent, PlayerColor(0)));
	gameHandler->createBoat(occupiedWater, BoatId::CASTLE, PlayerColor(0));
	ASSERT_TRUE(gameState()->getMap().isInTheMap(nonAdjacent));
	ASSERT_FALSE(gameState()->getMap().isInTheMap(differentLevel));

	const int3 remoteWater(20, 20, 0);
	gameState()->getMap().getTile(remoteWater).terrainType = ETerrainId::WATER;
	gameHandler->createBoat(remoteWater, BoatId::NECROPOLIS, PlayerColor(0));
	const auto initialBoats = gameState()->getMap().getObjects<CGBoat>();
	ASSERT_EQ(initialBoats.size(), 2u);
	std::vector<int3> originalBoatPositions;
	for(const auto * boat : initialBoats)
		originalBoatPositions.push_back(boat->visitablePos());

	const std::array<int3, 6> illegalTargets = {
		explicitZero, nonWater, nonAdjacent, differentLevel, occupiedWater, hiddenAdjacent
	};
	for(const auto & target : illegalTargets)
	{
		SCOPED_TRACE(target.toString());
		EXPECT_FALSE(effect->isTargetInRange(&gameHandler->gameInfo(), attackerSideHero, target));
		EXPECT_FALSE(canCastSummonBoatAt(target));

		const auto manaBefore = attackerSideHero->getManaAvailable();
		EXPECT_FALSE(castSummonBoat(target));
		EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore);
		EXPECT_FALSE(attackerSideHero->hasNewHorizonsAdventureSpellCastToday());

		const auto boatsAfter = gameState()->getMap().getObjects<CGBoat>();
		ASSERT_EQ(boatsAfter.size(), originalBoatPositions.size());
		for(size_t index = 0; index < boatsAfter.size(); ++index)
			EXPECT_EQ(boatsAfter[index]->visitablePos(), originalBoatPositions[index]);
	}
}

TEST_F(NewHorizonsMagicStateTest, LegacyExpertSummonBoatStillCreatesConfiguredBoat)
{
	ASSERT_NO_FATAL_FAILURE(prepareSummonBoatCast(false));
	ASSERT_FALSE(newHorizonsMagic::isAdventureSpell(attackerSideHero->getMagicRules(), SpellID(SpellID::SUMMON_BOAT)));
	const auto * effect = summonBoatEffect();
	ASSERT_NE(effect, nullptr);
	EXPECT_FALSE(effect->requiresTargetSelection(attackerSideHero));
	const auto summonPosition = attackerSideHero->bestLocation();
	ASSERT_GE(summonPosition.x, 0);
	ASSERT_TRUE(gameState()->getMap().getObjects<CGBoat>().empty());

	ASSERT_TRUE(castSummonBoat());

	const auto boats = gameState()->getMap().getObjects<CGBoat>();
	ASSERT_EQ(boats.size(), 1u);
	EXPECT_EQ(boats.front()->visitablePos(), summonPosition);
	EXPECT_FALSE(attackerSideHero->hasNewHorizonsAdventureSpellCastToday());
}

TEST_F(NewHorizonsMagicStateTest, WizardCanChooseArcaneMemoryFromTheBasicWisdomOffer)
{
	activateArcaneMemoryForTest();
	startGameWithWizard();
	for(int index = 0; index < LIBRARY->skillh->size(); ++index)
		attackerSideHero->setSecSkillLevel(SecondarySkill(index), MasteryLevel::NONE, ChangeValueMode::ABSOLUTE);
	attackerSideHero->setSecSkillLevel(wisdomSkill(), MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);

	const auto rankLookup = [this](const std::string & skillId)
	{
		return attackerSideHero->getPerkSkillRank(skillId);
	};
	bool selectedThroughOffer = false;
	for(uint64_t offerSeed = 0; offerSeed < 4096 && !selectedThroughOffer; ++offerSeed)
	{
		const auto offer = attackerSideHero->getPerkState().prepareOffer(rankLookup, offerSeed);
		const auto arcaneMemory = std::find_if(offer.begin(), offer.end(), [](const auto & candidate)
		{
			return candidate.selection.perkId == ARCANE_MEMORY;
		});
		if(arcaneMemory == offer.end())
			continue;

		const auto choice = static_cast<size_t>(std::distance(offer.begin(), arcaneMemory));
		gameHandler->levelUpHero(attackerSideHero, offer, choice, offerSeed, false);
		selectedThroughOffer = true;
	}
	ASSERT_TRUE(selectedThroughOffer) << "Arcane Memory should be a legal Basic Wisdom perk offer for a Wizard";
	EXPECT_TRUE(attackerSideHero->hasActivePerk(WISDOM_SKILL, ARCANE_MEMORY));
}

TEST_F(NewHorizonsMagicStateTest, PlannedArcaneMemoryIsNotOfferedOrActivatable)
{
	arcaneMemoryFixtureMode = ArcaneMemoryFixtureMode::ProductionStatus;
	startGameWithWizard();
	attackerSideHero->setSecSkillLevel(wisdomSkill(), MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);

	const auto & savedPerks = attackerSideHero->getPerkState().rules["skills"][WISDOM_SKILL]["perks"].Vector();
	const auto arcaneMemory = std::find_if(savedPerks.begin(), savedPerks.end(), [](const auto & perk)
	{
		return perk["id"].String() == ARCANE_MEMORY;
	});
	ASSERT_NE(arcaneMemory, savedPerks.end());
	EXPECT_EQ((*arcaneMemory)["effect"]["status"].String(), "planned");

	const auto rankLookup = [this](const std::string & skillId)
	{
		return attackerSideHero->getPerkSkillRank(skillId);
	};
	for(uint64_t offerSeed = 0; offerSeed < 128; ++offerSeed)
	{
		const auto offer = attackerSideHero->getPerkState().prepareOffer(rankLookup, offerSeed);
		EXPECT_TRUE(std::none_of(offer.begin(), offer.end(), [](const auto & candidate)
		{
			return candidate.selection.perkId == ARCANE_MEMORY;
		}));
	}
	EXPECT_THROW(attackerSideHero->applyPerkSelection({WISDOM_SKILL, ARCANE_MEMORY}), std::runtime_error);
	EXPECT_FALSE(attackerSideHero->hasActivePerk(WISDOM_SKILL, ARCANE_MEMORY));
}

TEST_F(NewHorizonsMagicStateTest, AcceptedCombatScrollCastLearnsAndPersistsWithoutConsuming)
{
	prepareWizardWithArcaneMemory();
	const SpellID magicArrow(SpellID::MAGIC_ARROW);
	ASSERT_TRUE(gameHandler->giveHeroNewScroll(attackerSideHero, magicArrow, ArtifactPosition::MISC1));
	ASSERT_TRUE(attackerSideHero->hasScroll(magicArrow, false));
	ASSERT_FALSE(attackerSideHero->spellbookContainsSpell(magicArrow));
	ASSERT_TRUE(attackerSideHero->canLearnSpell(magicArrow.toSpell()));

	startBattle();
	auto * target = addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), BattleHex(rightHex), 10);
	ASSERT_NE(target, nullptr);
	beginCombat();
	const auto action = heroSpellAction(magicArrow, target);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));

	EXPECT_TRUE(attackerSideHero->hasScroll(magicArrow, false));
	EXPECT_TRUE(attackerSideHero->spellbookContainsSpell(magicArrow));
	const auto saved = gameState()->saveToMemory();
	auto restored = std::make_shared<CGameState>();
	restored->preInit(LIBRARY);
	restored->loadFromMemory(saved);
	const auto * restoredHero = restored->getHero(attackerSideHero->id);
	ASSERT_NE(restoredHero, nullptr);
	EXPECT_TRUE(restoredHero->spellbookContainsSpell(magicArrow));
	EXPECT_TRUE(restoredHero->hasScroll(magicArrow, false));
}

TEST_F(NewHorizonsMagicStateTest, SpellbookSourceTakesPriorityOverMatchingScroll)
{
	prepareWizardWithArcaneMemory();
	const SpellID magicArrow(SpellID::MAGIC_ARROW);
	attackerSideHero->addSpellToSpellbook(magicArrow);
	ASSERT_TRUE(gameHandler->giveHeroNewScroll(attackerSideHero, magicArrow, ArtifactPosition::MISC1));
	const auto * spellbook = attackerSideHero->getArt(ArtifactPosition::SPELLBOOK);
	const auto * scroll = attackerSideHero->getArt(ArtifactPosition::MISC1);
	ASSERT_NE(spellbook, nullptr);
	ASSERT_NE(scroll, nullptr);
	const auto scrollId = scroll->getId();
	const auto spellSources = attackerSideHero->getSourcesForSpell(magicArrow);
	EXPECT_NE(std::find(spellSources.begin(), spellSources.end(), BonusSourceID(spellbook->getId())), spellSources.end());
	EXPECT_NE(std::find(spellSources.begin(), spellSources.end(), BonusSourceID(scrollId)), spellSources.end());

	startBattle();
	auto * enemy = addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), BattleHex(rightHex), 10);
	ASSERT_NE(enemy, nullptr);
	beginCombat();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		heroSpellAction(magicArrow, enemy)));
	EXPECT_TRUE(attackerSideHero->hasScroll(magicArrow, false));
	EXPECT_TRUE(attackerSideHero->spellbookContainsSpell(magicArrow));
	ASSERT_NE(attackerSideHero->getArt(ArtifactPosition::MISC1), nullptr);
	EXPECT_EQ(attackerSideHero->getArt(ArtifactPosition::MISC1)->getId(), scrollId);
	EXPECT_FALSE(attackerSideHero->canLearnSpell(SpellID(SpellID::MAGIC_ARROW).toSpell()))
		<< "A spell already in the book is not a new Arcane Memory acquisition";
}


TEST_F(NewHorizonsMagicStateTest, EquippedTomeSourceTakesPriorityOverMatchingScroll)
{
	prepareWizardWithArcaneMemory();
	const SpellID haste(SpellID::HASTE);
	ASSERT_TRUE(gameHandler->giveHeroNewArtifact(attackerSideHero,
		ArtifactID(ArtifactID::decode("core:tomeOfAirMagic")), ArtifactPosition::MISC2));
	ASSERT_TRUE(gameHandler->giveHeroNewScroll(attackerSideHero, haste, ArtifactPosition::MISC3));
	const auto * tomeSlot = attackerSideHero->getArt(ArtifactPosition::MISC2);
	ASSERT_NE(tomeSlot, nullptr);
	auto * tome = gameState()->getMap().getArtifactInstance(tomeSlot->getId());
	ASSERT_NE(tome, nullptr);
	const auto sorcery = SpellSchool::fromSerializationKey("new-horizons:sorcery");
	// Synthetic non-charge-source coverage only: legacy Tomes are intentionally excluded until
	// six-school replacement artifacts are authored; this does not claim shipped Tome support.
	tome->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::SPELLS_OF_SCHOOL,
		BonusSource::ARTIFACT, 0, BonusSourceID(tome->getId()), BonusSubtypeID(sorcery)));
	const auto * scrollSlot = attackerSideHero->getArt(ArtifactPosition::MISC3);
	ASSERT_NE(scrollSlot, nullptr);

	startBattle();
	auto * friendly = addStack(BattleSide::ATTACKER, creatureByName("core:archer"), BattleHex(leftHex), 10);
	ASSERT_NE(friendly, nullptr);
	beginCombat();
	ASSERT_TRUE(attackerSideHero->canCastThisSpell(haste.toSpell()));
	const auto hasteSources = attackerSideHero->getSourcesForSpell(haste);
	EXPECT_NE(std::find(hasteSources.begin(), hasteSources.end(), BonusSourceID(tome->getId())), hasteSources.end());
	EXPECT_NE(std::find(hasteSources.begin(), hasteSources.end(), BonusSourceID(scrollSlot->getId())), hasteSources.end());
	const auto * activeStack = battle()->battleActiveUnit();
	ASSERT_NE(activeStack, nullptr);
	EXPECT_EQ(battle()->battleGetOwner(activeStack), PlayerColor(0));
	EXPECT_FALSE(battle()->hasCompletedHeroSpellCast(BattleSide::ATTACKER));
	EXPECT_EQ(battle()->battleCastSpells(BattleSide::ATTACKER), 0);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		heroSpellAction(haste, friendly)));
	EXPECT_TRUE(attackerSideHero->hasScroll(haste, false));
	EXPECT_FALSE(attackerSideHero->spellbookContainsSpell(haste));
	ASSERT_NE(attackerSideHero->getArt(ArtifactPosition::MISC3), nullptr);
	EXPECT_EQ(attackerSideHero->getArt(ArtifactPosition::MISC3)->getId(), scrollSlot->getId());
}

TEST_F(NewHorizonsMagicStateTest, CombatScrollDoesNotBypassSchoolRequirement)
{
	prepareWizardWithArcaneMemory();
	const SpellID hypnotize(SpellID::HYPNOTIZE);
	const auto chaosSkillId = SecondarySkill::decode("new-horizons:chaosMagic");
	ASSERT_GE(chaosSkillId, 0);
	EXPECT_EQ(attackerSideHero->getSecSkillLevel(SecondarySkill(chaosSkillId)), MasteryLevel::NONE);
	ASSERT_FALSE(attackerSideHero->canLearnSpell(hypnotize.toSpell()));
	ASSERT_TRUE(gameHandler->giveHeroNewScroll(attackerSideHero, hypnotize, ArtifactPosition::MISC1));

	startBattle();
	auto * target = addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), BattleHex(rightHex), 1);
	ASSERT_NE(target, nullptr);
	beginCombat();
	EXPECT_FALSE(attackerSideHero->canCastThisSpell(hypnotize.toSpell()));
	const auto manaBefore = attackerSideHero->getManaAvailable();
	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		heroSpellAction(hypnotize, target)));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore);
	EXPECT_TRUE(attackerSideHero->hasScroll(hypnotize, false));
	EXPECT_FALSE(attackerSideHero->spellbookContainsSpell(hypnotize));

	// With the required School rank supplied, this covers the map-roster canLearnSpell gate.
	// Direct injection of a retired spell source is deferred to the broader Phase 2 matrix.
	attackerSideHero->setSecSkillLevel(SecondarySkill(chaosSkillId), MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	EXPECT_TRUE(attackerSideHero->canLearnSpell(hypnotize.toSpell()));
	ASSERT_TRUE(gameState()->getMap().allowedSpells.count(hypnotize));
	gameState()->getMap().allowedSpells.erase(hypnotize);
	EXPECT_FALSE(attackerSideHero->canLearnSpell(hypnotize.toSpell()));
}

TEST_F(NewHorizonsMagicStateTest, AcceptedAdventureScrollCastSettlesFromCompletedEffect)
{
	prepareWizardWithArcaneMemory();
	const SpellID fly(SpellID::FLY);
	ASSERT_TRUE(gameHandler->giveHeroNewScroll(attackerSideHero, fly, ArtifactPosition::MISC1));
	ASSERT_TRUE(attackerSideHero->hasScroll(fly, false));
	const auto cost = attackerSideHero->getSpellCost(fly.toSpell());
	const auto manaBefore = attackerSideHero->getManaAvailable();
	auto * environment = installCountingSpellEnvironment();

	AdventureSpellCastParameters parameters;
	parameters.caster = attackerSideHero;
	parameters.pos = int3();
	// The return value is not the completion signal: the assertions below use
	// the authoritative mana, daily-state, and scroll state after effects settle.
	const bool adventureCastReturned = fly.toSpell()->adventureCast(environment, parameters);
	(void)adventureCastReturned;

	EXPECT_TRUE(attackerSideHero->hasNewHorizonsAdventureSpellCastToday());
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore - cost);
	EXPECT_TRUE(attackerSideHero->hasScroll(fly, false));
	EXPECT_EQ(environment->prepared, 1);
	EXPECT_EQ(environment->completed, 1);
}

TEST_F(NewHorizonsMagicStateTest, DimensionDoorRejectedWithoutMovementLeavesScrollUntouched)
{
	prepareWizardWithArcaneMemory();
	const SpellID dimensionDoor(SpellID::DIMENSION_DOOR);
	ASSERT_TRUE(gameHandler->giveHeroNewScroll(attackerSideHero, dimensionDoor, ArtifactPosition::MISC1));
	ASSERT_TRUE(attackerSideHero->canCastThisSpell(dimensionDoor.toSpell()));
	attackerSideHero->setMovementPoints(0);
	const auto manaBefore = attackerSideHero->getManaAvailable();
	auto * environment = installCountingSpellEnvironment();

	AdventureSpellCastParameters parameters;
	parameters.caster = attackerSideHero;
	parameters.pos = int3();
	EXPECT_FALSE(dimensionDoor.toSpell()->adventureCast(environment, parameters));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore);
	EXPECT_EQ(attackerSideHero->movementPointsRemaining(), 0u);
	EXPECT_FALSE(attackerSideHero->hasNewHorizonsAdventureSpellCastToday());
	EXPECT_TRUE(attackerSideHero->hasScroll(dimensionDoor, false));
	EXPECT_FALSE(attackerSideHero->spellbookContainsSpell(dimensionDoor));
	EXPECT_EQ(environment->prepared, 0);
	EXPECT_EQ(environment->completed, 0);
}

TEST_F(NewHorizonsMagicStateTest, NewHorizonsDimensionDoorUsesVisibleEightTileTargetAndExhaustsMovement)
{
	prepareDimensionDoorCast(true, 1200);
	revealMap(PlayerColor(0));
	const SpellID dimensionDoor(SpellID::DIMENSION_DOOR);
	const auto * effect = dimensionDoorEffect();
	ASSERT_NE(effect, nullptr);
	ASSERT_TRUE(newHorizonsMagic::isAdventureSpell(attackerSideHero->getMagicRules(), dimensionDoor));
	EXPECT_TRUE(effect->requiresTargetSelection(attackerSideHero));

	const int3 source = attackerSideHero->getSightCenter();
	const int3 target = source + int3(8, 0, 0);
	ASSERT_EQ(source.dist(target, int3::DIST_2D), 8u);
	const int movementBefore = attackerSideHero->movementPointsRemaining();
	ASSERT_GT(movementBefore, 300);
	EXPECT_EQ(effect->getMovementPointsTaken(attackerSideHero, movementBefore), movementBefore);
	EXPECT_TRUE(effect->isTargetInRange(&gameHandler->gameInfo(), attackerSideHero, target));
	EXPECT_TRUE(effect->isValidTargetFrom(&gameHandler->gameInfo(), attackerSideHero, source, target));
	EXPECT_TRUE(canCastDimensionDoorAt(target));

	const auto manaBefore = attackerSideHero->getManaAvailable();
	ASSERT_TRUE(castDimensionDoorAt(target));

	EXPECT_EQ(attackerSideHero->visitablePos(), target);
	EXPECT_EQ(attackerSideHero->movementPointsRemaining(), 0);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore - attackerSideHero->getSpellCost(dimensionDoor.toSpell()));
	EXPECT_TRUE(attackerSideHero->hasNewHorizonsAdventureSpellCastToday());
}

TEST_F(NewHorizonsMagicStateTest, NewHorizonsDimensionDoorRejectsHiddenOutOfRangeAndBlockedTargetsWithoutSpending)
{
	prepareDimensionDoorCast(true, 1200);
	revealMap(PlayerColor(0));
	const auto * effect = dimensionDoorEffect();
	ASSERT_NE(effect, nullptr);
	const int3 source = attackerSideHero->getSightCenter();
	const int3 hiddenTarget = source + int3(3, 0, 0);
	const int3 outOfRangeTarget = source + int3(9, 0, 0);
	const int3 blockedTarget = source + int3(2, 2, 0);
	gameState()->getMap().getTile(blockedTarget).blockingObjects.push_back(defenderSideHero->id);
	ASSERT_TRUE(gameState()->getMap().getTile(blockedTarget).blocked());
	setTileVisibility(PlayerColor(0), hiddenTarget, false);
	ASSERT_FALSE(gameState()->isVisibleFor(hiddenTarget, PlayerColor(0)));

	const std::array<int3, 3> illegalTargets = {hiddenTarget, outOfRangeTarget, blockedTarget};
	const int movementBefore = attackerSideHero->movementPointsRemaining();
	const auto manaBefore = attackerSideHero->getManaAvailable();
	const int3 positionBefore = attackerSideHero->visitablePos();
	ASSERT_FALSE(attackerSideHero->hasNewHorizonsAdventureSpellCastToday());

	for(const auto & target : illegalTargets)
	{
		SCOPED_TRACE(target.toString());
		EXPECT_FALSE(effect->isTargetInRange(&gameHandler->gameInfo(), attackerSideHero, target));
		EXPECT_FALSE(effect->isValidTargetFrom(&gameHandler->gameInfo(), attackerSideHero, source, target));
		EXPECT_FALSE(canCastDimensionDoorAt(target));
		EXPECT_FALSE(castDimensionDoorAt(target));
		EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore);
		EXPECT_EQ(attackerSideHero->movementPointsRemaining(), movementBefore);
		EXPECT_EQ(attackerSideHero->visitablePos(), positionBefore);
		EXPECT_FALSE(attackerSideHero->hasNewHorizonsAdventureSpellCastToday());
	}
}

TEST_F(NewHorizonsMagicStateTest, LegacyDimensionDoorKeepsItsRectangularRangeAndConfiguredMovementCost)
{
	prepareDimensionDoorCast(false, 1200);
	setMapVisibility(PlayerColor(0), false);
	const SpellID dimensionDoor(SpellID::DIMENSION_DOOR);
	const auto * effect = dimensionDoorEffect();
	ASSERT_NE(effect, nullptr);
	ASSERT_FALSE(newHorizonsMagic::isAdventureSpell(attackerSideHero->getMagicRules(), dimensionDoor));
	EXPECT_TRUE(effect->requiresTargetSelection(attackerSideHero));

	const int3 source = attackerSideHero->getSightCenter();
	const int3 legacyRangeEdge = source + int3(9, 0, 0);
	const int movementBefore = attackerSideHero->movementPointsRemaining();
	ASSERT_GT(movementBefore, 300);
	EXPECT_EQ(effect->getMovementPointsTaken(attackerSideHero, movementBefore), 300);
	EXPECT_TRUE(effect->isTargetInRange(&gameHandler->gameInfo(), attackerSideHero, legacyRangeEdge));
	EXPECT_TRUE(effect->isValidTargetFrom(&gameHandler->gameInfo(), attackerSideHero, source, legacyRangeEdge));
	EXPECT_TRUE(canCastDimensionDoorAt(legacyRangeEdge));

	const auto manaBefore = attackerSideHero->getManaAvailable();
	ASSERT_TRUE(castDimensionDoorAt(legacyRangeEdge));
	EXPECT_EQ(attackerSideHero->visitablePos(), legacyRangeEdge);
	EXPECT_EQ(attackerSideHero->movementPointsRemaining(), movementBefore - 300);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore - attackerSideHero->getSpellCost(dimensionDoor.toSpell()));
	EXPECT_FALSE(attackerSideHero->hasNewHorizonsAdventureSpellCastToday());
}

TEST_F(NewHorizonsMagicStateTest, TownPortalQueryCancelThenGuildVisitRetainsTheOriginalScroll)
{
	prepareWizardWithArcaneMemory(true);
	auto * destination = findFirst<CGTownInstance>();
	ASSERT_NE(destination, nullptr);
	for(const auto building : {BuildingID::MAGES_GUILD_1, BuildingID::MAGES_GUILD_2, BuildingID::MAGES_GUILD_3})
		destination->addBuilding(building);

	const SpellID townPortal(SpellID::TOWN_PORTAL);
	const auto unlockCost = newHorizonsMagic::adventureSpellUnlockCost(gameState()->getMagicRules(), townPortal);
	const std::array<GameResID, 7> resources = {
		GameResID(EGameResID::WOOD), GameResID(EGameResID::MERCURY), GameResID(EGameResID::ORE),
		GameResID(EGameResID::SULFUR), GameResID(EGameResID::CRYSTAL), GameResID(EGameResID::GEMS),
		GameResID(EGameResID::GOLD)
	};
	const auto currentResources = gameState()->getPlayerState(PlayerColor(0))->resources;
	for(const auto resource : resources)
	{
		const auto deficit = unlockCost[resource] - currentResources[resource];
		if(deficit > 0)
			grantResources(PlayerColor(0), resource, static_cast<int>(deficit));
	}
	ASSERT_TRUE(gameHandler->unlockNewHorizonsAdventureSpell(destination->id, 3));
	ASSERT_TRUE(destination->hasNewHorizonsAdventureSpellUnlocked(3));

	// Legacy generic completion compatibility setup: Advanced mastery allows this
	// destination-query path. This is not canonical NH nearest-town acceptance.
	attackerSideHero->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::MAGIC_SCHOOL_SKILL, BonusSource::OTHER, MasteryLevel::ADVANCED,
		BonusSourceID(), BonusSubtypeID(SpellSchool::ANY)));
	attackerSideHero->setMovementPoints(1500);
	ASSERT_TRUE(gameHandler->giveHeroNewScroll(attackerSideHero, townPortal, ArtifactPosition::MISC1));
	ASSERT_TRUE(attackerSideHero->canLearnSpell(townPortal.toSpell()));
	auto * environment = installCountingSpellEnvironment();

	auto castForTownChoice = [&]()
	{
		AdventureSpellCastParameters parameters;
		parameters.caster = attackerSideHero;
		parameters.pos = int3(-1);
		const bool returned = townPortal.toSpell()->adventureCast(environment, parameters);
		(void)returned;
	};
	const auto manaBefore = attackerSideHero->getManaAvailable();
	const auto movementBefore = attackerSideHero->movementPointsRemaining();
	const auto dailyStateBefore = attackerSideHero->hasNewHorizonsAdventureSpellCastToday();

	// An in-map non-town tile passes target validation but fails during effect resolution.
	// The completion hook is prepared before effects, but must not settle this failed cast.
	AdventureSpellCastParameters failedParameters;
	failedParameters.caster = attackerSideHero;
	failedParameters.pos = int3(0, 0, 0);
	const bool failedCastReturned = townPortal.toSpell()->adventureCast(environment, failedParameters);
	(void)failedCastReturned;
	EXPECT_EQ(environment->prepared, 1);
	EXPECT_EQ(environment->completed, 0);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore);
	EXPECT_EQ(attackerSideHero->movementPointsRemaining(), movementBefore);
	EXPECT_EQ(attackerSideHero->hasNewHorizonsAdventureSpellCastToday(), dailyStateBefore);
	EXPECT_TRUE(attackerSideHero->hasScroll(townPortal, false));

	castForTownChoice();
	auto pending = gameHandler->queries->topQuery(PlayerColor(0));
	ASSERT_NE(pending, nullptr);
	ASSERT_EQ(pending->getType(), QueryType::Generic);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore);
	EXPECT_EQ(attackerSideHero->movementPointsRemaining(), movementBefore);
	EXPECT_FALSE(attackerSideHero->hasNewHorizonsAdventureSpellCastToday());
	EXPECT_TRUE(attackerSideHero->hasScroll(townPortal, false));
	EXPECT_EQ(environment->prepared, 1);
	EXPECT_EQ(environment->completed, 0);
	ASSERT_TRUE(gameHandler->queryReply(pending->queryID, std::nullopt, PlayerColor(0)));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore);
	EXPECT_EQ(attackerSideHero->movementPointsRemaining(), movementBefore);
	EXPECT_FALSE(attackerSideHero->hasNewHorizonsAdventureSpellCastToday());
	EXPECT_TRUE(attackerSideHero->hasScroll(townPortal, false));
	EXPECT_EQ(environment->prepared, 1);
	EXPECT_EQ(environment->completed, 0);

	castForTownChoice();
	pending = gameHandler->queries->topQuery(PlayerColor(0));
	ASSERT_NE(pending, nullptr);
	ASSERT_EQ(pending->getType(), QueryType::Generic);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore);
	EXPECT_TRUE(attackerSideHero->hasScroll(townPortal, false));
	EXPECT_EQ(environment->prepared, 1);
	EXPECT_EQ(environment->completed, 0);
	ASSERT_TRUE(gameHandler->queryReply(pending->queryID, destination->id.getNum(), PlayerColor(0)));

	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore - attackerSideHero->getSpellCost(townPortal.toSpell()));
	EXPECT_LT(attackerSideHero->movementPointsRemaining(), movementBefore);
	EXPECT_TRUE(attackerSideHero->hasNewHorizonsAdventureSpellCastToday());
	EXPECT_TRUE(attackerSideHero->hasScroll(townPortal, false));
	EXPECT_TRUE(attackerSideHero->spellbookContainsSpell(townPortal));
	EXPECT_FALSE(attackerSideHero->canLearnSpell(townPortal.toSpell()));
	EXPECT_EQ(environment->prepared, 2);
	EXPECT_EQ(environment->completed, 1);
}
