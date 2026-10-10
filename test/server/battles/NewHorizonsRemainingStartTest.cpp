/*
 * NewHorizonsRemainingStartTest.cpp, part of VCMI engine
 * License: GNU General Public License v2 or later; see license.txt
 */
#include "StdInc.h"
#include "../../NewHorizonsHistoricalAdventurePolicyTestUtils.h"
#include "HeroCommandFixture.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/entities/hero/CHero.h"
#include "../../../lib/entities/hero/NewHorizonsHeroRules.h"
#include "../../../lib/bonuses/BonusParameters.h"
#include "../../../lib/gameState/QuestInfo.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/networkPacks/PacksForLobby.h"
#include "../../../lib/spells/CSpellHandler.h"
#include "FullGameSnapshotTypes.h"
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
#ifdef ENABLE_BATTLE_AI
class RemainingPredictionEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit RemainingPredictionEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};
#endif

class NewHorizonsRemainingStartTest : public HeroCommandFixture, public ::testing::WithParamInterface<bool>
{
protected:
	bool presetBook = false;
	bool legacyMagic = false;
	bool absent = false;
	bool explicitFalse = false;
	CStack * target = nullptr;
	bool halon() const { return GetParam(); }
	SpellID original() const { return SpellID(halon() ? SpellID::STONE_SKIN : SpellID::BLOODLUST); }
	SpellID replacement() const
	{
		return SpellID(SpellID::decode(halon() ? "new-horizons:guardianSpirit" : "new-horizons:crusade"));
	}
	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		ASSERT_TRUE(vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE));
	}
	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS,
			legacyMagic ? JsonNode() : JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
		JsonNode rules(JsonPath::builtin("config/newHorizonsHeroes"));
		if(absent)
		{
			rules["nonDamageSpellSpecialties"].Struct().erase("remainingStartReplacements");
			// This control captures the preceding profile, not Coronius's later
			// optional specialty whose key cannot be written to that format.
			rules["damageSpellSpecialties"].Struct().erase("coroniusHolyWrathReplacement");
			rules["startingSkills"].Struct().erase("startingBookReplacements");
			rules.Struct().erase("remainingSpellSpecialtyReplacements");
			rules["skillSpecialties"].Struct().erase("navigationStartReplacements");
			rules.Struct().erase("defaultCreatureLineReplacements");
			std::erase_if(rules["nonDamageSpellSpecialties"]["spells"].Vector(),
				[](const JsonNode & value) { return value.String() == "new-horizons:phantomArmy"; });
			rules.Struct().erase("lighthouseDeparture");
		}
		if(explicitFalse)
			rules["nonDamageSpellSpecialties"]["remainingStartReplacements"].Bool() = false;
		rules.setOverrideFlag(true);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, rules);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
		if(absent)
			isolateHistoricalAdventurePolicies(*loaded);
	}
	void prepare()
	{
		const CreatureID pikeman(CreatureID::decode("core:pikeman"));
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36).playerActive(PlayerColor(0)).playerActive(PlayerColor(1))
			.hero({5, 5, 0}, HeroTypeID(HeroTypeID::decode(halon() ? "core:halon" : "core:inteus")), PlayerColor(0))
			.heroExperience(0).heroGarrison({{pikeman, 1}});
		if(presetBook)
			builder.heroSpells({original()});
		builder.hero({7, 7, 0}, HeroTypeID(HeroTypeID::decode("core:aislinn")), PlayerColor(1))
			.heroExperience(0).heroGarrison({{pikeman, 1}});
		startWithMap(std::move(builder));
		server.gameState = gameState();
		gameHandler = std::make_shared<CGameHandler>(server, gameState());
		gameHandler->randomizer->setSeed(seed);
		attackerSideHero = findHeroAt({5, 5, 0});
		defenderSideHero = findHeroAt({7, 7, 0});
		ASSERT_NE(attackerSideHero, nullptr);
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 100, ChangeValueMode::ABSOLUTE);
		setTestSpellPointTotal(attackerSideHero, 1000);
	}
	void combat()
	{
		// Normalize only numerical forecast controls, not production starts or
		// Halon's faction specialty/capacity. No competing action is disabled.
		for(const auto key : {"new-horizons:lightMagic", "new-horizons:natureMagic", "new-horizons:spellcraft"})
			attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode(key)), 0, ChangeValueMode::ABSOLUTE);
		startBattle();
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
		target = addStack(BattleSide::ATTACKER, CreatureID(CreatureID::decode("core:pikeman")), BattleHex(3, 5), 100);
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
		action.spell = replacement();
		if(halon()) action.aimToUnit(target);
		else action.aimToHex(BattleHex::INVALID);
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action);
	}
	void expectProducer(bool converted)
	{
		const auto * type = attackerSideHero->getHeroType();
		if(halon())
		{
			EXPECT_TRUE(type->nonDamageSpellSpecialtyProducers.empty());
			EXPECT_EQ(attackerSideHero->getNonDamageSpellSpecialtyBonusPercent(replacement()), 0);
			EXPECT_EQ(newHorizonsMagic::metamagicRank(attackerSideHero), legacyMagic ? 2 : 1);
			EXPECT_EQ(newHorizonsMagic::metamagicCapacity(attackerSideHero), 2);
			return;
		}
		ASSERT_EQ(type->nonDamageSpellSpecialtyProducers.size(), 1);
		const auto & producer = type->nonDamageSpellSpecialtyProducers.front();
		EXPECT_EQ(producer.spell, original());
		EXPECT_EQ(producer.bonus->parameters, nullptr);
		const auto clones = attackerSideHero->getAllBonuses(Selector::typeSubtype(
			BonusType::SPECIAL_PECULIAR_ENCHANT, BonusSubtypeID(original())));
		ASSERT_EQ(clones->size(), 1);
		const auto & clone = clones->front();
		if(converted)
		{
			EXPECT_NE(clone.get(), producer.bonus.get());
			EXPECT_EQ(clone->val, 0);
			ASSERT_NE(clone->parameters, nullptr);
			EXPECT_EQ(clone->parameters->toVector(), std::vector<int32_t>{0});
			EXPECT_EQ(attackerSideHero->getNonDamageSpellSpecialtyBonusPercent(replacement()), 20);
		}
		else
		{
			EXPECT_EQ(clone->val, producer.bonus->val);
			EXPECT_EQ(clone->parameters, producer.bonus->parameters);
			EXPECT_EQ(clone->stacking, producer.bonus->stacking);
			EXPECT_EQ(attackerSideHero->getNonDamageSpellSpecialtyBonusPercent(replacement()), 0);
		}
	}
	void expectValues(const battle::Unit * unit)
	{
		if(halon())
		{
			EXPECT_EQ(unit->getGuardianSpiritHitPoints(), 250); // ordinary50+2SP, not290
			EXPECT_EQ(unit->getGuardianSpiritRoundsRemaining(), 2);
			return;
		}
		const auto bonuses = unit->getAllBonuses(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(replacement())));
		ASSERT_EQ(bonuses->size(), 5);
		for(const auto & bonus : *bonuses)
		{
			EXPECT_EQ(bonus->turnsRemain, 3);
			if(bonus->type == BonusType::PRIMARY_SKILL) EXPECT_EQ(bonus->val, 4);
			else if(bonus->type == BonusType::STACKS_INITIATIVE_FLAT) EXPECT_EQ(bonus->val, 2);
			else if(bonus->type == BonusType::SPELL_DAMAGE_REDUCTION_BASIS_POINTS) EXPECT_EQ(bonus->val, 1980);
			else EXPECT_EQ(bonus->type, BonusType::MINIMUM_MORALE);
		}
	}
};

TEST_P(NewHorizonsRemainingStartTest, ActualDefaultPaidCastAndSpecialtyIsolation)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	EXPECT_TRUE(attackerSideHero->spellbookContainsSpell(replacement()));
	EXPECT_FALSE(attackerSideHero->spellbookContainsSpell(original()));
	EXPECT_TRUE(attackerSideHero->getHeroType()->spells.contains(original()));
	EXPECT_FALSE(attackerSideHero->getHeroType()->spells.contains(replacement()));
	expectProducer(true);
	ASSERT_NO_FATAL_FAILURE(combat());
	const auto mana = attackerSideHero->getManaAvailable();
	const auto hp = target->getAvailableHealth();
	ASSERT_TRUE(paidCast());
	EXPECT_EQ(mana - attackerSideHero->getManaAvailable(), halon() ? 8 : 24);
	EXPECT_EQ(target->getAvailableHealth(), hp);
	expectValues(target);
	EXPECT_FALSE(paidCast());
}

TEST_P(NewHorizonsRemainingStartTest, ExplicitMapBookRemainsOriginal)
{
	presetBook = true;
	ASSERT_NO_FATAL_FAILURE(prepare());
	EXPECT_TRUE(attackerSideHero->spellbookContainsSpell(original()));
	EXPECT_FALSE(attackerSideHero->spellbookContainsSpell(replacement()));
	expectProducer(true);
}

TEST_P(NewHorizonsRemainingStartTest, CapturedLegacyBookAndProducerRemainOriginal)
{
	legacyMagic = true;
	ASSERT_NO_FATAL_FAILURE(prepare());
	EXPECT_TRUE(attackerSideHero->spellbookContainsSpell(original()));
	EXPECT_FALSE(attackerSideHero->spellbookContainsSpell(replacement()));
	expectProducer(false);
}

TEST_P(NewHorizonsRemainingStartTest, AbsentOptInDoesNotEnableReplacement)
{
	absent = true;
	ASSERT_NO_FATAL_FAILURE(prepare());
	EXPECT_FALSE(attackerSideHero->spellbookContainsSpell(replacement()));
	expectProducer(false);
	CMemorySerializer bytes;
	bytes.oser.version = static_cast<ESerializationVersion>(
		static_cast<int>(ESerializationVersion::NEW_HORIZONS_REMAINING_START_REPLACEMENTS) - 1);
	EXPECT_NO_THROW(attackerSideHero->serialize(bytes.oser));
	EXPECT_FALSE(bytes.extractBuffer().empty());
}

TEST_P(NewHorizonsRemainingStartTest, WorldRoundtripAndSavedReinitPreserveBookAndSpecialtyCoefficient)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	CGameState restored;
	restored.preInit(LIBRARY);
	restored.loadFromMemory(gameState()->saveToMemory());
	auto * loaded = restored.getMap().getHero(attackerSideHero->getHeroTypeID());
	ASSERT_NE(loaded, nullptr);
	ASSERT_EQ(loaded->getHeroTypeID(), attackerSideHero->getHeroTypeID());
	ASSERT_EQ(loaded->id, attackerSideHero->id);
	const auto spells = loaded->getInscribedSpellsForCasting();
	GameRandomizer randomizer(restored);
	ASSERT_NO_THROW(loaded->initHero(randomizer));
	EXPECT_EQ(loaded->getInscribedSpellsForCasting(), spells);
	EXPECT_TRUE(loaded->spellbookContainsSpell(replacement()));
	EXPECT_EQ(loaded->getNonDamageSpellSpecialtyBonusPercent(replacement()), halon() ? 0 : 20);
	if(halon()) EXPECT_EQ(newHorizonsMagic::metamagicCapacity(loaded), 2);
}

TEST_P(NewHorizonsRemainingStartTest, FalseKeyRejectsOlderHeroMapWorldLobbyAndRawSettingsBeforeAdmission)
{
	explicitFalse = true;
	ASSERT_NO_FATAL_FAILURE(prepare());
	expectProducer(false);
	const auto previous = static_cast<ESerializationVersion>(
		static_cast<int>(ESerializationVersion::NEW_HORIZONS_REMAINING_START_REPLACEMENTS) - 1);
	const auto reject = [previous](auto & value)
	{
		CMemorySerializer bytes;
		bytes.oser.version = previous;
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
	GameSettings settings;
	settings.addOverride(EGameSettings::HEROES_NEW_HORIZONS, attackerSideHero->getPrimaryGrowthRules());
	reject(settings);
	JsonNode raw;
	raw["heroes"]["newHorizons"] = attackerSideHero->getPrimaryGrowthRules();
	CMemorySerializer incoming;
	incoming.oser & raw;
	incoming.iser.version = previous;
	GameSettings decoded;
	EXPECT_THROW(decoded.serialize(incoming.iser), std::runtime_error);
}

TEST_P(NewHorizonsRemainingStartTest, NullKeyAndMissingInteusIdentityRejectWhileAbsenceIsCompatible)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto rules = attackerSideHero->getPrimaryGrowthRules();
	rules["nonDamageSpellSpecialties"].Struct().erase("remainingStartReplacements");
	EXPECT_NO_THROW(newHorizonsHeroes::validateRemainingStartSerialization(rules, false));
	rules["nonDamageSpellSpecialties"]["remainingStartReplacements"] = JsonNode();
	EXPECT_THROW(newHorizonsHeroes::nonDamageSpellSpecialtyRules(rules), std::runtime_error);
	EXPECT_THROW(newHorizonsHeroes::validateRemainingStartSerialization(rules, false), std::runtime_error);
	rules = attackerSideHero->getPrimaryGrowthRules();
	std::erase_if(rules["nonDamageSpellSpecialties"]["spells"].Vector(), [](const auto & spell)
	{
		return spell.String() == "new-horizons:crusade";
	});
	EXPECT_THROW(newHorizonsHeroes::nonDamageSpellSpecialtyRules(rules), std::runtime_error);
}

#ifdef ENABLE_BATTLE_AI
TEST_P(NewHorizonsRemainingStartTest, DetachedPredictionMatchesPaidCastWithoutMutatingLiveOrCapacity)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(combat());
	RemainingPredictionEnvironment environment(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor::SPECTATOR);
	HypotheticBattle projected(&environment, callback);
	const auto mana = attackerSideHero->getManaAvailable();
	const auto capacity = newHorizonsMagic::metamagicCapacity(attackerSideHero);
	spells::BattleCast cast(&projected, attackerSideHero, spells::Mode::HERO, replacement().toSpell());
	const auto mechanics = replacement().toSpell()->battleMechanics(&cast);
	spells::Target aim{halon() ? spells::Destination(projected.battleGetUnitByID(target->unitId()))
		: spells::Destination(BattleHex::INVALID)};
	ASSERT_TRUE(mechanics->canBeCastAt(aim));
	mechanics->castEval(projected.getServerCallback(), aim);
	expectValues(projected.battleGetUnitByID(target->unitId()));
	EXPECT_EQ(target->getGuardianSpiritHitPoints(), 0);
	EXPECT_TRUE(target->getAllBonuses(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(replacement())))->empty());
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana);
	EXPECT_EQ(newHorizonsMagic::metamagicCapacity(attackerSideHero), capacity);
	ASSERT_TRUE(paidCast());
	expectValues(target);
}
#endif

INSTANTIATE_TEST_SUITE_P(InteusAndHalon, NewHorizonsRemainingStartTest, ::testing::Values(false, true),
	[](const auto & info) { return info.param ? "Halon" : "Inteus"; });
}
