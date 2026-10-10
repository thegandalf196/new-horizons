/*
 * NewHorizonsDefensiveSpecialtyTest.cpp, part of VCMI engine
 * License: GNU General Public License v2 or later; see license.txt
 */
#include "StdInc.h"
#include "../../NewHorizonsHistoricalAdventurePolicyTestUtils.h"
#include "HeroCommandFixture.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/CCreatureHandler.h"
#include "../../../lib/entities/hero/CHero.h"
#include "../../../lib/entities/hero/NewHorizonsHeroRules.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/networkPacks/PacksForLobby.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/ISpellMechanics.h"
#include "../../../lib/spells/NewHorizonsMagic.h"
#include "../../../lib/StartInfo.h"
#ifdef ENABLE_BATTLE_AI
#include "../../../AI/BattleAI/StackWithBonuses.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#endif

namespace
{
SpellID defensiveSpell(bool merist)
{
	return SpellID(SpellID::decode(merist ? "new-horizons:hydrasVitality" : "new-horizons:guardianSpirit"));
}

#ifdef ENABLE_BATTLE_AI
class DefensivePredictionEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit DefensivePredictionEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};
#endif

class NewHorizonsDefensiveSpecialtyTest : public HeroCommandFixture
{
protected:
	bool legacyMagic = false;
	bool presetBook = false;
	bool optIn = true;
	bool explicitFalse = false;
	bool merist = true;
	CStack * target = nullptr;

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
			legacyMagic ? JsonNode() : JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
		JsonNode rules(JsonPath::builtin("config/newHorizonsHeroes"));
		rules["startingSkills"].Struct().erase("startingDevelopmentProfiles");
		rules["nonDamageSpellSpecialties"].Struct().erase("remainingStartReplacements");
		rules["damageSpellSpecialties"].Struct().erase("coroniusHolyWrathReplacement");
		rules["startingSkills"].Struct().erase("startingBookReplacements");
		rules.Struct().erase("remainingSpellSpecialtyReplacements");
		rules["skillSpecialties"].Struct().erase("navigationStartReplacements");
		rules.Struct().erase("defaultCreatureLineReplacements");
		std::erase_if(rules["nonDamageSpellSpecialties"]["spells"].Vector(),
			[](const JsonNode & value) { return value.String() == "new-horizons:phantomArmy"; });
		rules.Struct().erase("lighthouseDeparture");
		rules["nonDamageSpellSpecialties"].Struct().erase("offensiveStartReplacements");
		std::erase_if(rules["nonDamageSpellSpecialties"]["spells"].Vector(), [](const JsonNode & spell)
		{
			return spell.String() == "new-horizons:crusade" || spell.String() == "new-horizons:focusMagic";
		});
		if(!optIn)
		{
			rules["nonDamageSpellSpecialties"].Struct().erase("defensiveStartReplacements");
			std::erase_if(rules["nonDamageSpellSpecialties"]["spells"].Vector(), [](const JsonNode & spell)
			{
				return spell.String() == "new-horizons:hydrasVitality" || spell.String() == "new-horizons:guardianSpirit";
			});
		}
		if(explicitFalse)
			rules["nonDamageSpellSpecialties"]["defensiveStartReplacements"].Bool() = false;
		rules.setOverrideFlag(true);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, rules);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
		isolateHistoricalAdventurePolicies(*loaded);
	}
	void prepare(bool useMerist = true)
	{
		merist = useMerist;
		const CreatureID archer(CreatureID::decode("core:archer"));
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).playerActive(PlayerColor(0)).playerActive(PlayerColor(1))
			.hero({5, 5, 0}, HeroTypeID(HeroTypeID::decode(merist ? "core:merist" : "core:labetha")), PlayerColor(0))
			.heroGarrison({{archer, 100}});
		if(presetBook)
			builder.heroSpells({SpellID::STONE_SKIN});
		builder.hero({7, 7, 0}, HeroTypeID(HeroTypeID::decode("core:aislinn")), PlayerColor(1))
			.heroGarrison({{archer, 100}});
		startWithMap(std::move(builder));
		server.gameState = gameState();
		gameHandler = std::make_shared<CGameHandler>(server, gameState());
		gameHandler->randomizer->setSeed(seed);
		attackerSideHero = findHeroAt({5, 5, 0});
		defenderSideHero = findHeroAt({7, 7, 0});
		ASSERT_NE(attackerSideHero, nullptr);
		ASSERT_NE(defenderSideHero, nullptr);
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 100, ChangeValueMode::ABSOLUTE);
		setTestSpellPointTotal(attackerSideHero, 1000);
	}
	void combat()
	{
		// Numerical controls use known coefficient100%; no production starts change.
		for(const auto key : {"new-horizons:natureMagic", "new-horizons:lightMagic", "new-horizons:spellcraft"})
			attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode(key)), 0, ChangeValueMode::ABSOLUTE);
		startBattle();
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
		target = addStack(BattleSide::ATTACKER, CreatureID(CreatureID::decode(merist ? "core:pikeman" : "core:airElemental")), BattleHex(3, 5), 100);
		addStack(BattleSide::DEFENDER, CreatureID(CreatureID::decode("core:peasant")), BattleHex(12, 5), 100);
		beginCombat();
		BattleSetActiveStack active;
		active.battleID = BattleID(0);
		active.stack = target->unitId();
		active.reason = BattleUnitTurnReason::TURN_QUEUE;
		gameHandler->sendAndApply(active);
	}
	bool paidCast()
	{
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = BattleSide::ATTACKER;
		action.spell = defensiveSpell(merist);
		action.aimToUnit(target);
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action);
	}
	void expectOriginalProducer()
	{
		const auto & producers = attackerSideHero->getHeroType()->nonDamageSpellSpecialtyProducers;
		ASSERT_EQ(producers.size(), 1u);
		const auto & original = producers.front().bonus;
		EXPECT_TRUE(std::ranges::any_of(attackerSideHero->getExportedBonusList(), [&original](const auto & bonus)
		{
			return bonus->type == original->type && bonus->subtype == original->subtype
				&& bonus->source == original->source && bonus->sid == original->sid
				&& bonus->val == original->val && bonus->parameters == original->parameters
				&& bonus->stacking == original->stacking;
		}));
	}
	void expectDefaultPaid(bool useMerist)
	{
		ASSERT_NO_FATAL_FAILURE(prepare(useMerist));
		EXPECT_TRUE(attackerSideHero->spellbookContainsSpell(defensiveSpell(merist)));
		EXPECT_FALSE(attackerSideHero->spellbookContainsSpell(SpellID::STONE_SKIN));
		EXPECT_TRUE(attackerSideHero->getHeroType()->spells.contains(SpellID::STONE_SKIN));
		EXPECT_FALSE(attackerSideHero->getHeroType()->spells.contains(defensiveSpell(merist)));
		EXPECT_EQ(attackerSideHero->getNonDamageSpellSpecialtyBonusPercent(defensiveSpell(merist)), 20);
		ASSERT_NO_FATAL_FAILURE(combat());
		const auto hp = target->getAvailableHealth();
		const auto maximum = target->getMaxHealth();
		const auto mana = attackerSideHero->getManaAvailable();
		ASSERT_TRUE(paidCast());
		EXPECT_EQ(mana - attackerSideHero->getManaAvailable(), merist ? 16 : 8);
		EXPECT_EQ(target->getAvailableHealth(), hp);
		if(merist)
		{
			EXPECT_EQ(target->getMaxHealth(), maximum * 143 / 100);
			const auto marker = target->getAllBonuses(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(defensiveSpell(true))).And(Selector::type()(BonusType::STACK_HEALTH)));
			ASSERT_EQ(marker->size(), 1u);
			EXPECT_EQ(marker->front()->turnsRemain, 3);
		}
		else
		{
			EXPECT_EQ(target->guardianSpiritHitPoints, 290);
			EXPECT_EQ(target->guardianSpiritRoundsRemaining, 2);
			EXPECT_EQ(target->getMaxHealth(), maximum);
		}
	}
};

TEST_F(NewHorizonsDefensiveSpecialtyTest, MeristDefaultAndPaidCapacityPreserveCurrentHP) { expectDefaultPaid(true); }
TEST_F(NewHorizonsDefensiveSpecialtyTest, LabethaDefaultAndPaidPoolProtectNonlivingElementals) { expectDefaultPaid(false); }

TEST_F(NewHorizonsDefensiveSpecialtyTest, MeristFixedBaseFractionFloorAndCapRemainUnchanged)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(combat());
	for(const auto power : {0, 7, 100, 2000})
	{
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, power, ChangeValueMode::ABSOLUTE);
		spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, defensiveSpell(true).toSpell());
		EXPECT_EQ(defensiveSpell(true).toSpell()->battleMechanics(&cast)->getEffectValue(),
			std::min<int64_t>(50'000'000, 25'000'000 + 180'000LL * power));
	}
}

TEST_F(NewHorizonsDefensiveSpecialtyTest, LabethaSpecialtyThenHealerThenFixedBase)
{
	ASSERT_NO_FATAL_FAILURE(prepare(false));
	ASSERT_NO_FATAL_FAILURE(combat());
	for(const auto power : {0, 7, 100})
	{
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, power, ChangeValueMode::ABSOLUTE);
		spells::BattleCast sample(battle(), attackerSideHero, spells::Mode::HERO, defensiveSpell(false).toSpell());
		EXPECT_EQ(defensiveSpell(false).toSpell()->battleMechanics(&sample)->getGuardianSpiritHitPoints(),
			50 + 2 * power * 120 / 100);
	}
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 100, ChangeValueMode::ABSOLUTE);
	attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:lightMagic")),
		MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	attackerSideHero->applyPerkSelection({"new-horizons:lightMagic", "new-horizons:lightMagic.healer"});
	ASSERT_TRUE(attackerSideHero->hasActivePerk("new-horizons:lightMagic", "new-horizons:lightMagic.healer"));
	ASSERT_FALSE(attackerSideHero->hasActivePerk("new-horizons:lightMagic", "new-horizons:lightMagic.guardian"));
	spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, defensiveSpell(false).toSpell());
	const auto mechanics = defensiveSpell(false).toSpell()->battleMechanics(&cast);
	EXPECT_EQ(mechanics->getSpellPowerCoefficientBasisPoints(), 14500);
	EXPECT_EQ(mechanics->getGuardianSpiritHitPoints(), 467); // 50+floor(348*1.2)
	const auto mana = attackerSideHero->getManaAvailable();
	ASSERT_TRUE(paidCast());
	EXPECT_EQ(mana - attackerSideHero->getManaAvailable(), 8);
	EXPECT_EQ(target->guardianSpiritHitPoints, 467);
}

TEST_F(NewHorizonsDefensiveSpecialtyTest, LabethaSpecialtyThenFixedBaseThenGuardian)
{
	ASSERT_NO_FATAL_FAILURE(prepare(false));
	ASSERT_NO_FATAL_FAILURE(combat());
	for(const auto power : {0, 7, 100})
	{
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, power, ChangeValueMode::ABSOLUTE);
		spells::BattleCast sample(battle(), attackerSideHero, spells::Mode::HERO, defensiveSpell(false).toSpell());
		EXPECT_EQ(defensiveSpell(false).toSpell()->battleMechanics(&sample)->getGuardianSpiritHitPoints(),
			50 + 2 * power * 120 / 100);
	}
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 100, ChangeValueMode::ABSOLUTE);
	attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:lightMagic")),
		MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	attackerSideHero->applyPerkSelection({"new-horizons:lightMagic", "new-horizons:lightMagic.guardian"});
	ASSERT_TRUE(attackerSideHero->hasActivePerk("new-horizons:lightMagic", "new-horizons:lightMagic.guardian"));
	ASSERT_FALSE(attackerSideHero->hasActivePerk("new-horizons:lightMagic", "new-horizons:lightMagic.healer"));
	spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, defensiveSpell(false).toSpell());
	const auto mechanics = defensiveSpell(false).toSpell()->battleMechanics(&cast);
	EXPECT_EQ(mechanics->getSpellPowerCoefficientBasisPoints(), 14500);
	EXPECT_EQ(mechanics->getGuardianSpiritHitPoints(), 497); // floor((50+348)*1.25)
	const auto mana = attackerSideHero->getManaAvailable();
	ASSERT_TRUE(paidCast());
	EXPECT_EQ(mana - attackerSideHero->getManaAvailable(), 8);
	EXPECT_EQ(target->guardianSpiritHitPoints, 497);
}

TEST_F(NewHorizonsDefensiveSpecialtyTest, MeristMapBookIsPreservedWithConvertedSpecialty)
{
	presetBook = true;
	ASSERT_NO_FATAL_FAILURE(prepare());
	EXPECT_TRUE(attackerSideHero->spellbookContainsSpell(SpellID::STONE_SKIN));
	EXPECT_FALSE(attackerSideHero->spellbookContainsSpell(defensiveSpell(true)));
	EXPECT_EQ(attackerSideHero->getNonDamageSpellSpecialtyBonusPercent(defensiveSpell(true)), 20);
}

TEST_F(NewHorizonsDefensiveSpecialtyTest, LabethaLegacyOriginalBookAndProducerRemain)
{
	legacyMagic = true;
	ASSERT_NO_FATAL_FAILURE(prepare(false));
	EXPECT_TRUE(attackerSideHero->spellbookContainsSpell(SpellID::STONE_SKIN));
	EXPECT_FALSE(attackerSideHero->spellbookContainsSpell(defensiveSpell(false)));
	EXPECT_EQ(attackerSideHero->getNonDamageSpellSpecialtyBonusPercent(defensiveSpell(false)), 0);
	expectOriginalProducer();
}

TEST_F(NewHorizonsDefensiveSpecialtyTest, LabethaMapBookIsPreservedWithConvertedSpecialty)
{
	presetBook = true;
	ASSERT_NO_FATAL_FAILURE(prepare(false));
	EXPECT_TRUE(attackerSideHero->spellbookContainsSpell(SpellID::STONE_SKIN));
	EXPECT_FALSE(attackerSideHero->spellbookContainsSpell(defensiveSpell(false)));
	EXPECT_EQ(attackerSideHero->getNonDamageSpellSpecialtyBonusPercent(defensiveSpell(false)), 20);
}

TEST_F(NewHorizonsDefensiveSpecialtyTest, MeristLegacyOriginalBookAndProducerRemain)
{
	legacyMagic = true;
	ASSERT_NO_FATAL_FAILURE(prepare());
	EXPECT_TRUE(attackerSideHero->spellbookContainsSpell(SpellID::STONE_SKIN));
	EXPECT_FALSE(attackerSideHero->spellbookContainsSpell(defensiveSpell(true)));
	EXPECT_EQ(attackerSideHero->getNonDamageSpellSpecialtyBonusPercent(defensiveSpell(true)), 0);
	expectOriginalProducer();
}

TEST_F(NewHorizonsDefensiveSpecialtyTest, AbsentOptInKeepsProducerAndPreviousFormatCompatibility)
{
	optIn = false;
	ASSERT_NO_FATAL_FAILURE(prepare());
	EXPECT_FALSE(attackerSideHero->spellbookContainsSpell(defensiveSpell(true)));
	expectOriginalProducer();
	CMemorySerializer bytes;
	bytes.oser.version = ESerializationVersion::NEW_HORIZONS_AENAIN_FRAILTY_SPECIALTY;
	EXPECT_NO_THROW(attackerSideHero->serialize(bytes.oser));
	EXPECT_FALSE(bytes.extractBuffer().empty());
}

TEST_F(NewHorizonsDefensiveSpecialtyTest, WorldRoundtripAndOlderEnclosingAdmission)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	CGameState restored;
	restored.preInit(LIBRARY);
	restored.loadFromMemory(gameState()->saveToMemory());
	const auto * hero = restored.getHero(attackerSideHero->id);
	ASSERT_NE(hero, nullptr);
	EXPECT_TRUE(hero->spellbookContainsSpell(defensiveSpell(true)));
	EXPECT_EQ(hero->getNonDamageSpellSpecialtyBonusPercent(defensiveSpell(true)), 20);
	const auto reject = [](auto & value)
	{
		CMemorySerializer bytes;
		bytes.oser.version = ESerializationVersion::NEW_HORIZONS_AENAIN_FRAILTY_SPECIALTY;
		EXPECT_THROW(value.serialize(bytes.oser), std::runtime_error);
		EXPECT_TRUE(bytes.extractBuffer().empty());
	};
	reject(*attackerSideHero);
	reject(gameState()->getMap());
	reject(*gameState());
	LobbyStartGame lobby;
	lobby.initializedStartInfo = std::make_shared<StartInfo>(*gameState()->getStartInfo());
	lobby.initializedGameState = gameState();
	reject(lobby);
	JsonNode raw;
	raw["heroes"]["newHorizons"] = attackerSideHero->getPrimaryGrowthRules();
	CMemorySerializer incoming;
	incoming.oser & raw;
	incoming.iser.version = ESerializationVersion::NEW_HORIZONS_AENAIN_FRAILTY_SPECIALTY;
	GameSettings decoded;
	EXPECT_THROW(decoded.serialize(incoming.iser), std::runtime_error);
}

TEST_F(NewHorizonsDefensiveSpecialtyTest, FalseKeyAndSpellListAloneAreNotOlderFormatCompatible)
{
	optIn = false;
	explicitFalse = true;
	ASSERT_NO_FATAL_FAILURE(prepare());
	expectOriginalProducer();
	CMemorySerializer bytes;
	bytes.oser.version = ESerializationVersion::NEW_HORIZONS_AENAIN_FRAILTY_SPECIALTY;
	EXPECT_THROW(attackerSideHero->serialize(bytes.oser), std::runtime_error);
	EXPECT_TRUE(bytes.extractBuffer().empty());
	JsonNode rules = attackerSideHero->getPrimaryGrowthRules();
	rules["nonDamageSpellSpecialties"].Struct().erase("defensiveStartReplacements");
	JsonNode identity;
	identity.String() = "new-horizons:guardianSpirit";
	rules["nonDamageSpellSpecialties"]["spells"].Vector().push_back(identity);
	EXPECT_THROW(newHorizonsHeroes::validateDefensiveStartSpecialtySerialization(rules, false), std::runtime_error);
	rules["nonDamageSpellSpecialties"]["defensiveStartReplacements"] = JsonNode();
	EXPECT_THROW(newHorizonsHeroes::nonDamageSpellSpecialtyRules(rules), std::runtime_error);
}

#ifdef ENABLE_BATTLE_AI
TEST_F(NewHorizonsDefensiveSpecialtyTest, MeristDetachedForecastMatchesPaidCapacityWithoutLiveMutation)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(combat());
	DefensivePredictionEnvironment environment(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor::SPECTATOR);
	HypotheticBattle projected(&environment, callback);
	const auto mana = attackerSideHero->getManaAvailable();
	const auto maximum = target->getMaxHealth();
	const auto hp = target->getAvailableHealth();
	spells::BattleCast cast(&projected, attackerSideHero, spells::Mode::HERO, defensiveSpell(true).toSpell());
	const auto mechanics = defensiveSpell(true).toSpell()->battleMechanics(&cast);
	spells::Target aim{spells::Destination(projected.battleGetUnitByID(target->unitId()))};
	ASSERT_TRUE(mechanics->canBeCastAt(aim));
	mechanics->castEval(projected.getServerCallback(), aim);
	const auto * detached = projected.battleGetUnitByID(target->unitId());
	EXPECT_EQ(detached->getMaxHealth(), maximum * 143 / 100);
	EXPECT_EQ(detached->getAvailableHealth(), hp);
	EXPECT_EQ(target->getMaxHealth(), maximum);
	EXPECT_EQ(target->getAvailableHealth(), hp);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana);
	ASSERT_TRUE(paidCast());
	EXPECT_EQ(target->getMaxHealth(), detached->getMaxHealth());
	EXPECT_EQ(target->getAvailableHealth(), detached->getAvailableHealth());
}

TEST_F(NewHorizonsDefensiveSpecialtyTest, LabethaDetachedForecastMatchesPaidPoolWithoutLiveMutation)
{
	ASSERT_NO_FATAL_FAILURE(prepare(false));
	ASSERT_NO_FATAL_FAILURE(combat());
	DefensivePredictionEnvironment environment(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor::SPECTATOR);
	HypotheticBattle projected(&environment, callback);
	const auto mana = attackerSideHero->getManaAvailable();
	spells::BattleCast cast(&projected, attackerSideHero, spells::Mode::HERO, defensiveSpell(false).toSpell());
	const auto mechanics = defensiveSpell(false).toSpell()->battleMechanics(&cast);
	spells::Target aim{spells::Destination(projected.battleGetUnitByID(target->unitId()))};
	ASSERT_TRUE(mechanics->canBeCastAt(aim));
	mechanics->castEval(projected.getServerCallback(), aim);
	const auto * detached = dynamic_cast<const battle::CUnitState *>(projected.battleGetUnitByID(target->unitId()));
	ASSERT_NE(detached, nullptr);
	EXPECT_EQ(detached->guardianSpiritHitPoints, 290);
	EXPECT_EQ(target->guardianSpiritHitPoints, 0);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana);
	ASSERT_TRUE(paidCast());
	EXPECT_EQ(target->guardianSpiritHitPoints, detached->guardianSpiritHitPoints);
}
#endif
}
