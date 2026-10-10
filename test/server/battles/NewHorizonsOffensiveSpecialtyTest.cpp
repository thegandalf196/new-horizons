/*
 * NewHorizonsOffensiveSpecialtyTest.cpp, part of VCMI engine
 * License: GNU General Public License v2 or later; see license.txt
 */
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/CCreatureHandler.h"
#include "../../../lib/entities/hero/CHero.h"
#include "../../../lib/entities/hero/NewHorizonsHeroRules.h"
#include "../../../lib/bonuses/BonusParameters.h"
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
SpellID originalSpell(bool loynis)
{
	return SpellID(loynis ? SpellID::PRAYER : SpellID::PRECISION);
}

SpellID offensiveSpell(bool loynis)
{
	return SpellID(SpellID::decode(loynis ? "new-horizons:crusade" : "new-horizons:focusMagic"));
}

#ifdef ENABLE_BATTLE_AI
class OffensivePredictionEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit OffensivePredictionEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};
#endif

class NewHorizonsOffensiveSpecialtyTest : public HeroCommandFixture
{
protected:
	bool legacyMagic = false;
	bool presetBook = false;
	bool optIn = true;
	bool explicitFalse = false;
	bool loynis = true;
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
		if(!optIn)
		{
			rules["nonDamageSpellSpecialties"].Struct().erase("offensiveStartReplacements");
			std::erase_if(rules["nonDamageSpellSpecialties"]["spells"].Vector(), [](const JsonNode & spell)
			{
				return spell.String() == "new-horizons:crusade" || spell.String() == "new-horizons:focusMagic";
			});
		}
		if(explicitFalse)
			rules["nonDamageSpellSpecialties"]["offensiveStartReplacements"].Bool() = false;
		rules.setOverrideFlag(true);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, rules);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
	}
	void prepare(bool useLoynis = true)
	{
		loynis = useLoynis;
		const CreatureID archer(CreatureID::decode("core:archer"));
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).playerActive(PlayerColor(0)).playerActive(PlayerColor(1))
			.hero({5, 5, 0}, HeroTypeID(HeroTypeID::decode(loynis ? "core:loynis" : "core:zubin")), PlayerColor(0))
			.heroGarrison({{archer, 100}});
		if(presetBook)
			builder.heroSpells({originalSpell(loynis)});
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
		for(const auto key : {"new-horizons:natureMagic", "new-horizons:lightMagic", "new-horizons:sorceryMagic", "new-horizons:spellcraft"})
			attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode(key)), 0, ChangeValueMode::ABSOLUTE);
		startBattle();
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
		target = addStack(BattleSide::ATTACKER, CreatureID(CreatureID::decode("core:archer")), BattleHex(3, 5), 100);
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
		action.spell = offensiveSpell(loynis);
		if(loynis)
			action.aimToHex(BattleHex::INVALID);
		else
			action.aimToUnit(target);
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action);
	}
	void expectOriginalProducer()
	{
		const auto & producers = attackerSideHero->getHeroType()->nonDamageSpellSpecialtyProducers;
		ASSERT_EQ(producers.size(), 1u);
		const auto & original = producers.front().bonus;
		EXPECT_EQ(original->parameters, nullptr);
		EXPECT_TRUE(std::ranges::any_of(attackerSideHero->getExportedBonusList(), [&original](const auto & bonus)
		{
			return bonus->type == original->type && bonus->subtype == original->subtype
				&& bonus->source == original->source && bonus->sid == original->sid
				&& bonus->val == original->val && bonus->parameters == original->parameters
				&& bonus->stacking == original->stacking;
		}));
	}

	void expectConvertedProducer() const
	{
		const auto & producers = attackerSideHero->getHeroType()->nonDamageSpellSpecialtyProducers;
		ASSERT_EQ(producers.size(), 1u);
		const auto & original = producers.front().bonus;
		EXPECT_EQ(original->parameters, nullptr);
		const auto converted = attackerSideHero->getAllBonuses(Selector::typeSubtype(
			BonusType::SPECIAL_PECULIAR_ENCHANT, BonusSubtypeID(originalSpell(loynis))));
		ASSERT_EQ(converted->size(), 1u);
		const auto & bonus = converted->front();
		EXPECT_NE(bonus.get(), original.get());
		EXPECT_EQ(bonus->val, 0);
		EXPECT_EQ(bonus->stacking, "new-horizons:non-damage-spell-specialty:"
			+ std::to_string(attackerSideHero->getHeroType()->getId().getNum()) + ":"
			+ std::to_string(offensiveSpell(loynis).getNum()));
		ASSERT_NE(bonus->parameters, nullptr);
		EXPECT_NE(bonus->parameters.get(), original->parameters.get());
		ASSERT_TRUE(bonus->parameters->isVector());
		EXPECT_EQ(bonus->parameters->toVector(), std::vector<int32_t>{0});
	}

	void expectValues(const battle::Unit * unit, int power) const
	{
		const auto bonuses = unit->getAllBonuses(Selector::source(BonusSource::SPELL_EFFECT,
			BonusSourceID(offensiveSpell(loynis))));
		ASSERT_EQ(bonuses->size(), loynis ? 5u : 1u);
		for(const auto & bonus : *bonuses)
		{
			EXPECT_EQ(bonus->turnsRemain, 3);
			if(!loynis)
			{
				EXPECT_EQ(bonus->type, BonusType::COMBAT_EVENT_TRIGGER);
				EXPECT_EQ(bonus->val, std::min(2000, 1000 + 5 * power * 120 / 100));
			}
			else if(bonus->type == BonusType::PRIMARY_SKILL)
				EXPECT_EQ(bonus->val, std::min(6, 3 + power * 120 / (75 * 100)));
			else if(bonus->type == BonusType::STACKS_INITIATIVE_FLAT)
				EXPECT_EQ(bonus->val, std::min(3, 1 + power * 120 / (100 * 100)));
			else if(bonus->type == BonusType::SPELL_DAMAGE_REDUCTION_BASIS_POINTS)
				EXPECT_EQ(bonus->val, std::min(2500, 1200 + 13 * power * 120 / (2 * 100)));
			else
				EXPECT_EQ(bonus->type, BonusType::MINIMUM_MORALE);
		}
	}

	void expectDefaultPaid(bool useLoynis)
	{
		ASSERT_NO_FATAL_FAILURE(prepare(useLoynis));
		EXPECT_TRUE(attackerSideHero->spellbookContainsSpell(offensiveSpell(loynis)));
		EXPECT_FALSE(attackerSideHero->spellbookContainsSpell(originalSpell(loynis)));
		EXPECT_TRUE(attackerSideHero->getHeroType()->spells.contains(originalSpell(loynis)));
		EXPECT_FALSE(attackerSideHero->getHeroType()->spells.contains(offensiveSpell(loynis)));
		EXPECT_EQ(attackerSideHero->getNonDamageSpellSpecialtyBonusPercent(offensiveSpell(loynis)), 20);
		expectConvertedProducer();
		ASSERT_NO_FATAL_FAILURE(combat());
		const auto hp = target->getAvailableHealth();
		const auto movement = target->getMovementRange();
		const auto mana = attackerSideHero->getManaAvailable();
		ASSERT_TRUE(paidCast());
		EXPECT_EQ(mana - attackerSideHero->getManaAvailable(), loynis ? 24 : 11);
		EXPECT_EQ(target->getAvailableHealth(), hp);
		EXPECT_EQ(target->getMovementRange(), movement);
		expectValues(target, 100);
		EXPECT_FALSE(paidCast()) << "ordinary completed hero action cannot repeat";
	}

	void expectMapBook(bool useLoynis)
	{
		presetBook = true;
		ASSERT_NO_FATAL_FAILURE(prepare(useLoynis));
		EXPECT_TRUE(attackerSideHero->spellbookContainsSpell(originalSpell(loynis)));
		EXPECT_FALSE(attackerSideHero->spellbookContainsSpell(offensiveSpell(loynis)));
		EXPECT_EQ(attackerSideHero->getNonDamageSpellSpecialtyBonusPercent(offensiveSpell(loynis)), 20);
		expectConvertedProducer();
	}

	void expectLegacy(bool useLoynis)
	{
		legacyMagic = true;
		ASSERT_NO_FATAL_FAILURE(prepare(useLoynis));
		EXPECT_TRUE(attackerSideHero->spellbookContainsSpell(originalSpell(loynis)));
		EXPECT_FALSE(attackerSideHero->spellbookContainsSpell(offensiveSpell(loynis)));
		EXPECT_EQ(attackerSideHero->getNonDamageSpellSpecialtyBonusPercent(offensiveSpell(loynis)), 0);
		expectOriginalProducer();
	}
};

TEST_F(NewHorizonsOffensiveSpecialtyTest, LoynisFreshDefaultPaidArmyEnchantment) { expectDefaultPaid(true); }
TEST_F(NewHorizonsOffensiveSpecialtyTest, ZubinFreshDefaultPaidFirstShotEnchantment) { expectDefaultPaid(false); }
TEST_F(NewHorizonsOffensiveSpecialtyTest, LoynisMapBookUnchanged) { expectMapBook(true); }
TEST_F(NewHorizonsOffensiveSpecialtyTest, ZubinMapBookUnchanged) { expectMapBook(false); }
TEST_F(NewHorizonsOffensiveSpecialtyTest, LoynisLegacyBookAndProducerUnchanged) { expectLegacy(true); }
TEST_F(NewHorizonsOffensiveSpecialtyTest, ZubinLegacyBookAndProducerUnchanged) { expectLegacy(false); }

TEST_F(NewHorizonsOffensiveSpecialtyTest, AbsentOptInPreservesOldFormatWritability)
{
	optIn = false;
	ASSERT_NO_FATAL_FAILURE(prepare());
	EXPECT_FALSE(attackerSideHero->spellbookContainsSpell(offensiveSpell(true)));
	expectOriginalProducer();
	CMemorySerializer bytes;
	bytes.oser.version = ESerializationVersion::NEW_HORIZONS_DEFENSIVE_START_SPECIALTIES;
	EXPECT_NO_THROW(attackerSideHero->serialize(bytes.oser));
	EXPECT_FALSE(bytes.extractBuffer().empty());
}

TEST_F(NewHorizonsOffensiveSpecialtyTest, CurrentWorldRestoresAndOlderEnclosingWritersRejectBeforePrefix)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	CGameState restored;
	restored.preInit(LIBRARY);
	restored.loadFromMemory(gameState()->saveToMemory());
	const auto * hero = restored.getHero(attackerSideHero->id);
	ASSERT_NE(hero, nullptr);
	EXPECT_TRUE(hero->spellbookContainsSpell(offensiveSpell(true)));
	EXPECT_EQ(hero->getNonDamageSpellSpecialtyBonusPercent(offensiveSpell(true)), 20);
	const auto reject = [](auto & value)
	{
		CMemorySerializer bytes;
		bytes.oser.version = ESerializationVersion::NEW_HORIZONS_DEFENSIVE_START_SPECIALTIES;
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
	incoming.iser.version = ESerializationVersion::NEW_HORIZONS_DEFENSIVE_START_SPECIALTIES;
	GameSettings decoded;
	EXPECT_THROW(decoded.serialize(incoming.iser), std::runtime_error);
}

TEST_F(NewHorizonsOffensiveSpecialtyTest, FalseFlagPresenceAndListAloneRejectOlderFormat)
{
	optIn = false;
	explicitFalse = true;
	ASSERT_NO_FATAL_FAILURE(prepare());
	expectOriginalProducer();
	CMemorySerializer bytes;
	bytes.oser.version = ESerializationVersion::NEW_HORIZONS_DEFENSIVE_START_SPECIALTIES;
	EXPECT_THROW(attackerSideHero->serialize(bytes.oser), std::runtime_error);
	EXPECT_TRUE(bytes.extractBuffer().empty());
	JsonNode rules = attackerSideHero->getPrimaryGrowthRules();
	rules["nonDamageSpellSpecialties"].Struct().erase("offensiveStartReplacements");
	JsonNode identity;
	identity.String() = "new-horizons:focusMagic";
	rules["nonDamageSpellSpecialties"]["spells"].Vector().push_back(identity);
	EXPECT_THROW(newHorizonsHeroes::validateOffensiveStartSpecialtySerialization(rules, false), std::runtime_error);
	rules["nonDamageSpellSpecialties"]["offensiveStartReplacements"] = JsonNode();
	EXPECT_THROW(newHorizonsHeroes::nonDamageSpellSpecialtyRules(rules), std::runtime_error);
}

TEST_F(NewHorizonsOffensiveSpecialtyTest, EnabledFlagRequiresBothIdentities)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	JsonNode rules = attackerSideHero->getPrimaryGrowthRules();
	std::erase_if(rules["nonDamageSpellSpecialties"]["spells"].Vector(), [](const JsonNode & spell)
	{
		return spell.String() == "new-horizons:focusMagic";
	});
	EXPECT_THROW(newHorizonsHeroes::nonDamageSpellSpecialtyRules(rules), std::runtime_error);
}

#ifdef ENABLE_BATTLE_AI
TEST_F(NewHorizonsOffensiveSpecialtyTest, LoynisDetachedFloorsCapsAndPaidMatchWithoutLiveMutation)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(combat());
	OffensivePredictionEnvironment environment(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor::SPECTATOR);
	for(const auto power : {0, 63, 84, 100, 2000})
	{
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, power, ChangeValueMode::ABSOLUTE);
		HypotheticBattle projected(&environment, callback);
		const auto mana = attackerSideHero->getManaAvailable();
		spells::BattleCast cast(&projected, attackerSideHero, spells::Mode::HERO, offensiveSpell(true).toSpell());
		const auto mechanics = offensiveSpell(true).toSpell()->battleMechanics(&cast);
		spells::Target aim{spells::Destination(BattleHex::INVALID)};
		ASSERT_TRUE(mechanics->canBeCastAt(aim));
		mechanics->castEval(projected.getServerCallback(), aim);
		expectValues(projected.battleGetUnitByID(target->unitId()), power);
		EXPECT_TRUE(target->getAllBonuses(Selector::source(BonusSource::SPELL_EFFECT,
			BonusSourceID(offensiveSpell(true))))->empty());
		EXPECT_EQ(attackerSideHero->getManaAvailable(), mana);
	}
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 100, ChangeValueMode::ABSOLUTE);
	ASSERT_TRUE(paidCast());
	expectValues(target, 100);
}

TEST_F(NewHorizonsOffensiveSpecialtyTest, ZubinDetachedFixedBaseFractionAndCapMatchPaidWithoutLiveMutation)
{
	ASSERT_NO_FATAL_FAILURE(prepare(false));
	ASSERT_NO_FATAL_FAILURE(combat());
	OffensivePredictionEnvironment environment(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor::SPECTATOR);
	for(const auto power : {0, 7, 100, 2000})
	{
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, power, ChangeValueMode::ABSOLUTE);
		HypotheticBattle projected(&environment, callback);
		const auto mana = attackerSideHero->getManaAvailable();
		spells::BattleCast cast(&projected, attackerSideHero, spells::Mode::HERO, offensiveSpell(false).toSpell());
		const auto mechanics = offensiveSpell(false).toSpell()->battleMechanics(&cast);
		spells::Target aim{spells::Destination(projected.battleGetUnitByID(target->unitId()))};
		ASSERT_TRUE(mechanics->canBeCastAt(aim));
		mechanics->castEval(projected.getServerCallback(), aim);
		expectValues(projected.battleGetUnitByID(target->unitId()), power);
		EXPECT_TRUE(target->getAllBonuses(Selector::source(BonusSource::SPELL_EFFECT,
			BonusSourceID(offensiveSpell(false))))->empty());
		EXPECT_EQ(attackerSideHero->getManaAvailable(), mana);
	}
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 100, ChangeValueMode::ABSOLUTE);
	ASSERT_TRUE(paidCast());
	expectValues(target, 100);
}
#endif
}
