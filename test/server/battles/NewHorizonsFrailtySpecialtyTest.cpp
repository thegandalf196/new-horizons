/*
 * NewHorizonsFrailtySpecialtyTest.cpp, part of VCMI engine
 * License: GNU General Public License v2 or later; see license.txt
 */
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/CCreatureHandler.h"
#include "../../../lib/bonuses/BonusParameters.h"
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
SpellID frailty()
{
	return SpellID(SpellID::decode(std::string(newHorizonsMagic::SHADOW_FRAILTY_SPELL)));
}

#ifdef ENABLE_BATTLE_AI
class FrailtyPredictionEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit FrailtyPredictionEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};
#endif

class NewHorizonsFrailtySpecialtyTest : public HeroCommandFixture
{
protected:
	bool optIn = true;
	bool legacyMagic = false;
	bool presetBook = false;
	CStack * friendly = nullptr;
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
		if(!optIn)
		{
			std::erase_if(rules["nonDamageSpellSpecialties"]["spells"].Vector(),
				[](const JsonNode & spell) { return spell.String() == newHorizonsMagic::SHADOW_FRAILTY_SPELL; });
			rules.setOverrideFlag(true);
		}
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, rules);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
	}
	void prepare(const std::string & actor = "core:cuthbert", int spellPower = 100)
	{
		const CreatureID archer(CreatureID::decode("core:archer"));
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).playerActive(PlayerColor(0)).playerActive(PlayerColor(1))
			.hero({5, 5, 0}, HeroTypeID(HeroTypeID::decode(actor)), PlayerColor(0))
			.heroGarrison({{archer, 100}});
		if(presetBook)
			builder.heroSpells({SpellID::WEAKNESS});
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
		setPower(spellPower);
		setTestSpellPointTotal(attackerSideHero, 1000);
	}
	void setPower(int spellPower)
	{
		// Frailty uses getEffectPower's raw Attribute, not generic damage's divisor.
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER,
			spellPower, ChangeValueMode::ABSOLUTE);
	}
	void combat()
	{
		startBattle();
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
		friendly = addStack(BattleSide::ATTACKER, CreatureID(CreatureID::decode("core:archer")), BattleHex(3, 5), 100);
		target = addStack(BattleSide::DEFENDER, CreatureID(CreatureID::decode("core:ancientBehemoth")), BattleHex(12, 5), 100);
		beginCombat();
		activate();
	}
	void activate()
	{
		BattleSetActiveStack active;
		active.battleID = BattleID(0);
		active.stack = friendly->unitId();
		active.reason = BattleUnitTurnReason::TURN_QUEUE;
		gameHandler->sendAndApply(active);
	}
	bool paidCast()
	{
		activate();
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = BattleSide::ATTACKER;
		action.spell = frailty();
		action.aimToUnit(target);
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action);
	}
	void expectMarker(const battle::Unit * unit, int32_t expected)
	{
		const auto bonuses = unit->getAllBonuses(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(frailty())));
		ASSERT_EQ(bonuses->size(), 1u);
		ASSERT_NE(bonuses->front()->parameters, nullptr);
		EXPECT_EQ(bonuses->front()->parameters->toNumber(), expected);
		EXPECT_EQ(bonuses->front()->val, -(unit->unitType()->getBaseDefense() * expected / 10000));
		EXPECT_EQ(bonuses->front()->duration, BonusDuration::ONE_BATTLE);
		EXPECT_EQ(bonuses->front()->turnsRemain, 0);
	}
	void expectNamedStart(const std::string & key, SpellID oldSpell)
	{
		ASSERT_NO_FATAL_FAILURE(prepare(key));
		EXPECT_TRUE(attackerSideHero->spellbookContainsSpell(frailty()));
		EXPECT_FALSE(attackerSideHero->spellbookContainsSpell(oldSpell));
		EXPECT_TRUE(attackerSideHero->getHeroType()->spells.contains(oldSpell));
		EXPECT_FALSE(attackerSideHero->getHeroType()->spells.contains(frailty()));
		EXPECT_EQ(attackerSideHero->getNonDamageSpellSpecialtyBonusPercent(frailty()), 20);
		EXPECT_EQ(attackerSideHero->getNonDamageSpellSpecialtyBonusPercent(oldSpell), 0);
		EXPECT_EQ(attackerSideHero->getPerkSkillRank(std::string(newHorizonsMagic::SHADOW_MAGIC_SKILL)), 0);
		EXPECT_TRUE(attackerSideHero->canCastThisSpell(frailty().toSpell()))
			<< "Known default spells may cast without adding a new starting school choice";
		ASSERT_NO_FATAL_FAILURE(combat());
		spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, frailty().toSpell());
		const auto mechanics = frailty().toSpell()->battleMechanics(&cast);
		EXPECT_EQ(mechanics->getEffectPower(), 100);
		EXPECT_EQ(mechanics->getSpellPowerCoefficientBasisPoints(), 10000);
		EXPECT_EQ(mechanics->getFrailtyDefenseLossBasisPoints(), 1600);
		const auto mana = attackerSideHero->getManaAvailable();
		const auto before = target->getDefense(false);
		ASSERT_TRUE(paidCast());
		EXPECT_EQ(mana - attackerSideHero->getManaAvailable(), 8);
		expectMarker(target, 1600);
		EXPECT_EQ(target->getDefense(false), before - target->unitType()->getBaseDefense() * 1600 / 10000);
	}
	void expectOriginalProducer()
	{
		const auto & producers = attackerSideHero->getHeroType()->nonDamageSpellSpecialtyProducers;
		ASSERT_EQ(producers.size(), 1u);
		const auto & prototype = producers.front().bonus;
		EXPECT_TRUE(std::ranges::any_of(attackerSideHero->getExportedBonusList(), [&prototype](const auto & bonus)
		{
			return bonus->type == prototype->type && bonus->subtype == prototype->subtype
				&& bonus->source == prototype->source && bonus->sid == prototype->sid
				&& bonus->val == prototype->val && bonus->stacking == prototype->stacking
				&& bonus->parameters == prototype->parameters;
		}));
	}
};

TEST_F(NewHorizonsFrailtySpecialtyTest, CuthbertDefaultStartAndPaidDefenseLoss) { expectNamedStart("core:cuthbert", SpellID::WEAKNESS); }
TEST_F(NewHorizonsFrailtySpecialtyTest, OlemaDefaultStartAndPaidDefenseLoss) { expectNamedStart("core:olema", SpellID::WEAKNESS); }
TEST_F(NewHorizonsFrailtySpecialtyTest, MirlandaDefaultStartAndPaidDefenseLoss) { expectNamedStart("core:mirlanda", SpellID::WEAKNESS); }
TEST_F(NewHorizonsFrailtySpecialtyTest, XsiDefaultStartAndPaidDefenseLoss) { expectNamedStart("core:xsi", SpellID::STONE_SKIN); }

TEST_F(NewHorizonsFrailtySpecialtyTest, FixedTermRankRationalFloorAndCastCapAreUnchanged)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(combat());
	const auto shadow = SecondarySkill(SecondarySkill::decode(std::string(newHorizonsMagic::SHADOW_MAGIC_SKILL)));
	const auto spellcraft = SecondarySkill(SecondarySkill::decode(std::string(newHorizonsMagic::SPELLCRAFT_SKILL)));
	for(int rank = 0; rank <= MasteryLevel::EXPERT; ++rank)
	{
		attackerSideHero->setSecSkillLevel(shadow, rank, ChangeValueMode::ABSOLUTE);
		attackerSideHero->setSecSkillLevel(spellcraft, rank, ChangeValueMode::ABSOLUTE);
		setPower(7);
		spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, frailty().toSpell());
		const auto mechanics = frailty().toSpell()->battleMechanics(&cast);
		EXPECT_EQ(mechanics->getWarcastingBonusPercent(), 0);
		EXPECT_EQ(mechanics->getEmpowerSpellBonusPercent(), 0);
		EXPECT_EQ(mechanics->getFrailtyDefenseLossBasisPoints(),
			1000 + 5LL * 7 * mechanics->getSpellPowerCoefficientBasisPoints() * 120 / 1'000'000);
		setPower(0);
		spells::BattleCast zero(battle(), attackerSideHero, spells::Mode::HERO, frailty().toSpell());
		EXPECT_EQ(frailty().toSpell()->battleMechanics(&zero)->getFrailtyDefenseLossBasisPoints(), 1000);
	}
	setPower(2000);
	attackerSideHero->applyPerkSelection({std::string(newHorizonsMagic::SHADOW_MAGIC_SKILL),
		std::string(newHorizonsMagic::SHADOW_WITHERING_TOUCH_PERK)});
	spells::BattleCast capped(battle(), attackerSideHero, spells::Mode::HERO, frailty().toSpell());
	EXPECT_EQ(frailty().toSpell()->battleMechanics(&capped)->getFrailtyDefenseLossBasisPoints(), 2500);
}

TEST_F(NewHorizonsFrailtySpecialtyTest, PaidRepeatedCastsKeepTheCumulativeSixtyPercentCap)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(combat());
	for(int castNumber = 1; castNumber <= 4; ++castNumber)
	{
		if(castNumber > 1)
		{
			BattleNextRound next;
			next.battleID = BattleID(0);
			gameHandler->sendAndApply(next);
		}
		ASSERT_TRUE(paidCast());
		expectMarker(target, std::min(6000, castNumber * 1600));
	}
}

TEST_F(NewHorizonsFrailtySpecialtyTest, LegacyRulesKeepXsiOriginalInscriptionAndProducer)
{
	legacyMagic = true;
	ASSERT_NO_FATAL_FAILURE(prepare("core:xsi"));
	EXPECT_TRUE(attackerSideHero->spellbookContainsSpell(SpellID::STONE_SKIN));
	EXPECT_FALSE(attackerSideHero->spellbookContainsSpell(frailty()));
	EXPECT_EQ(attackerSideHero->getNonDamageSpellSpecialtyBonusPercent(frailty()), 0);
	expectOriginalProducer();
}

TEST_F(NewHorizonsFrailtySpecialtyTest, MapPresetBookIsNeverRewritten)
{
	presetBook = true;
	ASSERT_NO_FATAL_FAILURE(prepare());
	EXPECT_TRUE(attackerSideHero->spellbookContainsSpell(SpellID::WEAKNESS));
	EXPECT_FALSE(attackerSideHero->spellbookContainsSpell(frailty()));
	EXPECT_EQ(attackerSideHero->getNonDamageSpellSpecialtyBonusPercent(frailty()), 20);
}

TEST_F(NewHorizonsFrailtySpecialtyTest, CurrentWorldRoundtripAndPreviousFormatOuterAdmission)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto id = attackerSideHero->id;
	CGameState restored;
	restored.preInit(LIBRARY);
	restored.loadFromMemory(gameState()->saveToMemory());
	const auto * loaded = restored.getHero(id);
	ASSERT_NE(loaded, nullptr);
	EXPECT_TRUE(loaded->spellbookContainsSpell(frailty()));
	EXPECT_EQ(loaded->getNonDamageSpellSpecialtyBonusPercent(frailty()), 20);
	EXPECT_NE(loaded->getSpecialtyDescriptionTranslated().find("Creature Defense loss component"), std::string::npos);
	const auto reject = [](auto & value)
	{
		CMemorySerializer bytes;
		bytes.oser.version = ESerializationVersion::NEW_HORIZONS_THANT_REANIMATE;
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
	raw["heroes"]["newHorizons"] = JsonNode(JsonPath::builtin("config/newHorizonsHeroes"));
	CMemorySerializer incoming;
	incoming.oser & raw;
	incoming.iser.version = ESerializationVersion::NEW_HORIZONS_THANT_REANIMATE;
	GameSettings decoded;
	EXPECT_THROW(decoded.serialize(incoming.iser), std::runtime_error);
}

TEST_F(NewHorizonsFrailtySpecialtyTest, CurrentOptOutPreservesProducerAndPreviousFormatCompatibility)
{
	optIn = false;
	ASSERT_NO_FATAL_FAILURE(prepare());
	EXPECT_FALSE(attackerSideHero->spellbookContainsSpell(SpellID::WEAKNESS));
	EXPECT_FALSE(attackerSideHero->spellbookContainsSpell(frailty()));
	EXPECT_EQ(attackerSideHero->getNonDamageSpellSpecialtyBonusPercent(frailty()), 0);
	expectOriginalProducer();
	CMemorySerializer bytes;
	bytes.oser.version = ESerializationVersion::NEW_HORIZONS_THANT_REANIMATE;
	EXPECT_NO_THROW(attackerSideHero->serialize(bytes.oser));
	EXPECT_FALSE(bytes.extractBuffer().empty());
	attackerSideHero->addSpellToSpellbook(frailty());
	ASSERT_NO_FATAL_FAILURE(combat());
	spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, frailty().toSpell());
	EXPECT_EQ(frailty().toSpell()->battleMechanics(&cast)->getFrailtyDefenseLossBasisPoints(), 1500);
	ASSERT_TRUE(paidCast());
	expectMarker(target, 1500);
}

#ifdef ENABLE_BATTLE_AI
TEST_F(NewHorizonsFrailtySpecialtyTest, DetachedAICastUsesSameSpecializedLossWithoutChangingLiveState)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(combat());
	FrailtyPredictionEnvironment environment(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor::SPECTATOR);
	HypotheticBattle projected(&environment, callback);
	const auto before = target->getDefense(false);
	const auto mana = attackerSideHero->getManaAvailable();
	spells::BattleCast cast(&projected, attackerSideHero, spells::Mode::HERO, frailty().toSpell());
	const auto mechanics = frailty().toSpell()->battleMechanics(&cast);
	const auto * detached = projected.battleGetUnitByID(target->unitId());
	spells::Target aim{spells::Destination(detached)};
	ASSERT_TRUE(mechanics->canBeCastAt(aim));
	EXPECT_EQ(mechanics->getFrailtyDefenseLossBasisPoints(), 1600);
	mechanics->castEval(projected.getServerCallback(), aim);
	expectMarker(projected.battleGetUnitByID(target->unitId()), 1600);
	EXPECT_EQ(target->getDefense(false), before);
	EXPECT_TRUE(target->getAllBonuses(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(frailty())))->empty());
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana);
}
#endif
}
