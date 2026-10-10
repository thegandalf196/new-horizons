/*
 * NewHorizonsCoroniusHolyWrathTest.cpp, part of VCMI engine
 * License: GNU General Public License v2 or later; see license.txt
 */
#include "StdInc.h"
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
#include "../../../lib/spells/BattleSpellMechanics.h"
#include "../../../lib/spells/ISpellMechanics.h"
#include "../../../lib/spells/NewHorizonsMagic.h"
#include "../../../lib/callback/IGameRandomizer.h"
#include "FullGameSnapshotTypes.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#ifdef ENABLE_BATTLE_AI
#include "../../../AI/BattleAI/StackWithBonuses.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#endif

namespace
{
SpellID holyWrath() { return SpellID(SpellID::decode("new-horizons:holyWrath")); }
#ifdef ENABLE_BATTLE_AI
class CoroniusEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit CoroniusEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};
#endif
}

class NewHorizonsCoroniusHolyWrathTest : public HeroCommandFixture
{
protected:
	bool absent = false;
	bool disabled = false;
	bool legacy = false;
	bool presetBook = false;
	CStack * target = nullptr;
	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		ASSERT_TRUE(vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE));
	}
	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		JsonNode rules(JsonPath::builtin("config/newHorizonsHeroes"));
		if(absent)
			rules["damageSpellSpecialties"].Struct().erase("coroniusHolyWrathReplacement");
		if(disabled)
			rules["damageSpellSpecialties"]["coroniusHolyWrathReplacement"].Bool() = false;
		rules.setOverrideFlag(true);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, rules);
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS,
			legacy ? JsonNode() : JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
	}
	void prepare()
	{
		const CreatureID pikeman(CreatureID::decode("core:pikeman"));
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36).playerActive(PlayerColor(0)).playerActive(PlayerColor(1))
			.hero({5, 5, 0}, HeroTypeID(HeroTypeID::decode("core:coronius")), PlayerColor(0))
			.heroExperience(0).heroGarrison({{pikeman, 1}});
		if(presetBook)
			builder.heroSpells({SpellID::SLAYER});
		builder.hero({7, 7, 0}, HeroTypeID(HeroTypeID::decode("core:aislinn")), PlayerColor(1))
			.heroExperience(0).heroGarrison({{pikeman, 1}});
		startWithMap(std::move(builder));
		server.gameState = gameState();
		gameHandler = std::make_shared<CGameHandler>(server, gameState());
		attackerSideHero = findHeroAt({5, 5, 0});
		defenderSideHero = findHeroAt({7, 7, 0});
		ASSERT_NE(attackerSideHero, nullptr);
		ASSERT_EQ(attackerSideHero->getHeroType()->getJsonKey(), "core:coronius");
	}
	void combat(const std::string & creature = "core:pikeman")
	{
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 100, ChangeValueMode::ABSOLUTE);
		attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
		attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:lightMagic")),
			MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		setTestSpellPointTotal(attackerSideHero, 1000);
		startBattle();
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
		addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(3, 5), 1);
		target = addStack(BattleSide::DEFENDER, creatureByName(creature), BattleHex(12, 5), 1000);
		beginCombat();
		ASSERT_NE(target, nullptr);
		for(int attempt = 0; attempt < 64; ++attempt)
		{
			const auto * active = battle()->battleActiveUnit();
			ASSERT_NE(active, nullptr);
			if(battle()->battleGetActionController(active) == PlayerColor(0))
				break;
			ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0),
				battle()->battleGetActionController(active), BattleAction::makeDefend(active)));
		}
		ASSERT_NE(battle()->battleActiveUnit(), nullptr);
		ASSERT_EQ(battle()->battleGetActionController(battle()->battleActiveUnit()), PlayerColor(0));
	}
	bool paid()
	{
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = BattleSide::ATTACKER;
		action.spell = holyWrath();
		action.aimToUnit(target);
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action);
	}
	void expectLegacyProducer() const
	{
		const auto & producers = attackerSideHero->getHeroType()->damageSpellSpecialtyProducers;
		const auto found = std::ranges::find_if(producers, [](const auto & producer) { return producer.spell == SpellID::SLAYER; });
		ASSERT_NE(found, producers.end());
		ASSERT_TRUE(found->bonus->parameters);
		EXPECT_EQ(found->bonus->parameters->toVector(), (std::vector<int32_t>{4, 3, 2, 1, 0, 0, 0}));
		EXPECT_EQ(found->bonus->subtype, BonusSubtypeID(SpellID(SpellID::SLAYER)));
	}
};

TEST_F(NewHorizonsCoroniusHolyWrathTest, FreshDefaultUsesExactNativeProducerAndDamageOnlyMarker)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(attackerSideHero->spellbookContainsSpell(holyWrath()));
	EXPECT_FALSE(attackerSideHero->spellbookContainsSpell(SpellID::SLAYER));
	EXPECT_EQ(attackerSideHero->getDamageSpellSpecialtyBonusPercent(holyWrath()), 15);
	EXPECT_EQ(attackerSideHero->getNonDamageSpellSpecialtyBonusPercent(holyWrath()), 0);
	expectLegacyProducer();
	const auto & local = attackerSideHero->getExportedBonusList();
	EXPECT_FALSE(std::ranges::any_of(local, [](const auto & bonus)
	{
		return bonus->type == BonusType::SPECIAL_PECULIAR_ENCHANT && bonus->subtype == BonusSubtypeID(SpellID(SpellID::SLAYER));
	}));
	const auto converted = std::ranges::find_if(local, [](const auto & bonus)
	{
		return bonus->type == BonusType::SPECIFIC_SPELL_DAMAGE && bonus->subtype == BonusSubtypeID(holyWrath());
	});
	ASSERT_NE(converted, local.end());
	EXPECT_EQ((*converted)->val, 0);
	EXPECT_FALSE((*converted)->parameters);
	EXPECT_NE(attackerSideHero->getSpecialtyDescriptionTranslated().find("15%"), std::string::npos);
}

TEST_F(NewHorizonsCoroniusHolyWrathTest, ActualPaidOrdinaryDamagePreservesFixedBaseAndCost)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(combat());
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 0, ChangeValueMode::ABSOLUTE);
	spells::BattleCast baseCast(battle(), attackerSideHero, spells::Mode::HERO, holyWrath().toSpell());
	EXPECT_EQ(holyWrath().toSpell()->battleMechanics(&baseCast)->getEffectValue(), 40);
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 100, ChangeValueMode::ABSOLUTE);
	spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, holyWrath().toSpell());
	const auto mechanics = holyWrath().toSpell()->battleMechanics(&cast);
	EXPECT_EQ(mechanics->getEffectValue(), 66); // 40 + floor(20 * 1.15 School * 1.15 specialty)
	const auto hp = target->getAvailableHealth();
	const auto mana = attackerSideHero->getManaAvailable();
	ASSERT_TRUE(paid());
	EXPECT_EQ(hp - target->getAvailableHealth(), 66);
	EXPECT_EQ(mana - attackerSideHero->getManaAvailable(), 11);
	ASSERT_EQ(server.castsOf(holyWrath()).size(), 1u);
	EXPECT_EQ(server.castsOf(holyWrath()).front().damage, 66);
}

TEST_F(NewHorizonsCoroniusHolyWrathTest, ActualPaidUndeadKeepsSingleClassificationMultiplier)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(combat("core:skeleton"));
	const auto hp = target->getAvailableHealth();
	ASSERT_TRUE(paid());
	EXPECT_EQ(hp - target->getAvailableHealth(), 99);
}

TEST_F(NewHorizonsCoroniusHolyWrathTest, ActualPaidInfernoKeepsSingleClassificationMultiplier)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(combat("core:imp"));
	const auto hp = target->getAvailableHealth();
	ASSERT_TRUE(paid());
	EXPECT_EQ(hp - target->getAvailableHealth(), 99);
}

TEST_F(NewHorizonsCoroniusHolyWrathTest, ExplicitMapBookRetainsSlayerWhileCapturedSpecialtyIsIndependent)
{
	presetBook = true;
	ASSERT_NO_FATAL_FAILURE(prepare());
	EXPECT_TRUE(attackerSideHero->spellbookContainsSpell(SpellID::SLAYER));
	EXPECT_FALSE(attackerSideHero->spellbookContainsSpell(holyWrath()));
	EXPECT_EQ(attackerSideHero->getDamageSpellSpecialtyBonusPercent(holyWrath()), 15);
	expectLegacyProducer();
}

TEST_F(NewHorizonsCoroniusHolyWrathTest, AbsentFlagRetainsOriginalProducerWithoutNewBook)
{
	absent = true;
	ASSERT_NO_FATAL_FAILURE(prepare());
	EXPECT_FALSE(attackerSideHero->spellbookContainsSpell(holyWrath()));
	EXPECT_EQ(attackerSideHero->getDamageSpellSpecialtyBonusPercent(holyWrath()), 0);
	expectLegacyProducer();
	CMemorySerializer older;
	older.oser.version = static_cast<ESerializationVersion>(static_cast<int>(ESerializationVersion::NEW_HORIZONS_CORONIUS_HOLY_WRATH) - 1);
	ASSERT_NO_THROW(older.oser & *attackerSideHero);
	EXPECT_FALSE(older.extractBuffer().empty());
}

TEST_F(NewHorizonsCoroniusHolyWrathTest, FalseFlagRetainsOriginalProducerWithoutNewBook)
{
	disabled = true;
	ASSERT_NO_FATAL_FAILURE(prepare());
	EXPECT_FALSE(attackerSideHero->spellbookContainsSpell(holyWrath()));
	EXPECT_EQ(attackerSideHero->getDamageSpellSpecialtyBonusPercent(holyWrath()), 0);
	expectLegacyProducer();
}

TEST_F(NewHorizonsCoroniusHolyWrathTest, CapturedLegacyMagicKeepsOriginalBookAndProducer)
{
	legacy = true;
	ASSERT_NO_FATAL_FAILURE(prepare());
	EXPECT_TRUE(attackerSideHero->spellbookContainsSpell(SpellID::SLAYER));
	EXPECT_FALSE(attackerSideHero->spellbookContainsSpell(holyWrath()));
	EXPECT_EQ(attackerSideHero->getDamageSpellSpecialtyBonusPercent(holyWrath()), 0);
	expectLegacyProducer();
}

TEST_F(NewHorizonsCoroniusHolyWrathTest, NullFlagRejectsRatherThanSilentlyAdmittingLegacy)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto rules = attackerSideHero->getPrimaryGrowthRules();
	rules["damageSpellSpecialties"]["coroniusHolyWrathReplacement"] = JsonNode();
	EXPECT_THROW(newHorizonsHeroes::damageSpellSpecialtyRules(rules), std::runtime_error);
}

TEST_F(NewHorizonsCoroniusHolyWrathTest, WorldRoundtripAndSavedInitializationKeepCapturedBookAndDamage)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	CGameState restored;
	restored.preInit(LIBRARY);
	restored.loadFromMemory(gameState()->saveToMemory());
	auto * loaded = restored.getMap().getHero(attackerSideHero->getHeroTypeID());
	ASSERT_NE(loaded, nullptr);
	ASSERT_EQ(loaded->id, attackerSideHero->id);
	ASSERT_TRUE(loaded->spellbookContainsSpell(holyWrath()));
	EXPECT_EQ(loaded->getDamageSpellSpecialtyBonusPercent(holyWrath()), 15);
	GameRandomizer randomizer(restored);
	ASSERT_NO_THROW(loaded->initHero(randomizer));
	EXPECT_TRUE(loaded->spellbookContainsSpell(holyWrath()));
	EXPECT_FALSE(loaded->spellbookContainsSpell(SpellID::SLAYER));
	EXPECT_EQ(loaded->getDamageSpellSpecialtyBonusPercent(holyWrath()), 15);
}

TEST_F(NewHorizonsCoroniusHolyWrathTest, FalseKeyAndRawReaderRejectOldAdmissionBeforeEnclosingPrefixes)
{
	disabled = true;
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto previous = static_cast<ESerializationVersion>(static_cast<int>(ESerializationVersion::NEW_HORIZONS_CORONIUS_HOLY_WRATH) - 1);
	const auto reject = [previous](auto & value)
	{
		CMemorySerializer serializer;
		serializer.oser.version = previous;
		EXPECT_THROW(serializer.oser & value, std::runtime_error);
		EXPECT_TRUE(serializer.extractBuffer().empty());
	};
	reject(*attackerSideHero);
	reject(gameState()->getMap());
	reject(*gameState());
	LobbyStartGame lobby;
	lobby.initializedGameState = gameState();
	reject(lobby);
	JsonNode raw;
	raw["heroes"]["newHorizons"] = attackerSideHero->getPrimaryGrowthRules();
	CMemorySerializer serializer;
	serializer.oser & raw;
	serializer.iser.version = previous;
	GameSettings settings;
	EXPECT_THROW(serializer.iser & settings, std::runtime_error);
}

#ifdef ENABLE_BATTLE_AI
TEST_F(NewHorizonsCoroniusHolyWrathTest, DetachedActualCastMatchesPaidAndDoesNotMutateParentOrSibling)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(combat());
	CoroniusEnvironment environment(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor::SPECTATOR);
	HypotheticBattle projected(&environment, callback);
	HypotheticBattle sibling(&environment, callback);
	const auto hp = target->getAvailableHealth();
	const auto mana = attackerSideHero->getManaAvailable();
	spells::BattleCast cast(&projected, attackerSideHero, spells::Mode::HERO, holyWrath().toSpell());
	const auto mechanics = holyWrath().toSpell()->battleMechanics(&cast);
	spells::Target aim{spells::Destination(projected.battleGetUnitByID(target->unitId()))};
	ASSERT_TRUE(mechanics->canBeCastAt(aim));
	mechanics->castEval(projected.getServerCallback(), aim);
	EXPECT_EQ(hp - projected.battleGetUnitByID(target->unitId())->getAvailableHealth(), 66);
	EXPECT_EQ(target->getAvailableHealth(), hp);
	EXPECT_EQ(sibling.battleGetUnitByID(target->unitId())->getAvailableHealth(), hp);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana);
	ASSERT_TRUE(paid());
	EXPECT_EQ(hp - target->getAvailableHealth(), 66);
}
#endif
