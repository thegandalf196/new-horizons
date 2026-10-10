/*
 * NewHorizonsRemainingSpellSpecialtyTest.cpp, part of VCMI engine
 * License: GNU General Public License v2 or later; see license.txt
 */
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/entities/hero/CHero.h"
#include "../../../lib/battle/CUnitState.h"
#include "../../../lib/entities/hero/NewHorizonsHeroRules.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/networkPacks/PacksForLobby.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/ISpellMechanics.h"
#include "../../../lib/spells/NewHorizonsMagic.h"
#include "../../../lib/spells/NewHorizonsDirectDamage.h"
#include "../../../lib/spells/NewHorizonsSorcery.h"
#include "../../../lib/StartInfo.h"
#ifdef ENABLE_BATTLE_AI
#include "../../../AI/BattleAI/StackWithBonuses.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#endif

namespace
{
struct Successor
{
	const char * hero;
	const char * from;
	const char * to;
	bool damage;
};
constexpr std::array<Successor, 6> successors{{
	{"core:ash", "core:bloodlust", "core:fireball", true},
	{"core:darkstorn", "core:stoneSkin", "new-horizons:hexOfPain", true},
	{"core:astral", "core:hypnotize", "new-horizons:phantomArmy", false},
	{"core:septienna", "core:deathRipple", "new-horizons:plague", true},
	{"core:melodia", "core:fortune", "core:bless", false},
	{"core:daremyth", "core:fortune", "core:haste", false}
}};

SpellID spell(const char * key) { return SpellID(SpellID::decode(key)); }

#ifdef ENABLE_BATTLE_AI
class SuccessorEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit SuccessorEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};
#endif

class NewHorizonsRemainingSpellSpecialtyTest : public HeroCommandFixture
{
protected:
	int selected = 0;
	bool preset = false;
	bool enabled = true;
	bool legacy = false;
	bool explicitFalse = false;
	CStack * friendly = nullptr;
	CStack * enemy = nullptr;

	const Successor & row() const { return successors.at(selected); }
	SpellID targetSpell() const { return spell(row().to); }
	int percent(const CGHeroInstance & hero) const
	{
		return row().damage ? hero.getDamageSpellSpecialtyBonusPercent(targetSpell())
			: hero.getNonDamageSpellSpecialtyBonusPercent(targetSpell());
	}
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
			legacy ? JsonNode() : JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
		JsonNode rules(JsonPath::builtin("config/newHorizonsHeroes"));
		ASSERT_TRUE(newHorizonsHeroes::usesRemainingSpellSpecialties(rules))
			<< "Principal gate requires the shipped opt-in, not a fixture activation";
		rules["startingSkills"].Struct().erase("startingDevelopmentProfiles");
		// An original-magic context cannot admit the optional modern starting-book table.
		if(legacy)
			rules["startingSkills"].Struct().erase("startingBookReplacements");
		if(!enabled)
		{
			rules["skillSpecialties"].Struct().erase("navigationStartReplacements");
			rules.Struct().erase("defaultCreatureLineReplacements");
			rules.Struct().erase("remainingSpellSpecialtyReplacements");
			std::erase_if(rules["nonDamageSpellSpecialties"]["spells"].Vector(),
				[](const JsonNode & value) { return value.String() == "new-horizons:phantomArmy"; });
		}
		if(explicitFalse)
			rules["remainingSpellSpecialtyReplacements"].Bool() = false;
		rules.setOverrideFlag(true);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, rules);
	}
	void prepare(int index = 0, int power = 100)
	{
		selected = index;
		const CreatureID pikeman(CreatureID::decode("core:pikeman"));
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).playerActive(PlayerColor(0)).playerActive(PlayerColor(1))
			.hero({5, 5, 0}, HeroTypeID(HeroTypeID::decode(row().hero)), PlayerColor(0))
			.heroGarrison({{pikeman, 1000}});
		if(preset)
			builder.heroSpells({spell(row().from)});
		builder.hero({7, 7, 0}, HeroTypeID(HeroTypeID::decode("core:aislinn")), PlayerColor(1))
			.heroGarrison({{pikeman, 1000}});
		startWithMap(std::move(builder));
		server.gameState = gameState();
		gameHandler = std::make_shared<CGameHandler>(server, gameState());
		gameHandler->randomizer->setSeed(seed);
		attackerSideHero = findHeroAt({5, 5, 0});
		defenderSideHero = findHeroAt({7, 7, 0});
		ASSERT_NE(attackerSideHero, nullptr);
		ASSERT_NE(defenderSideHero, nullptr);
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, power, ChangeValueMode::ABSOLUTE);
		attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
		setTestSpellPointTotal(attackerSideHero, 1000);
	}
	void combat()
	{
		// Retain genuine School admission: clear unrelated riders, then install
		// exactly the minimum learned rank required by this current spell.
		for(const auto key : {"new-horizons:chaosMagic", "new-horizons:shadowMagic",
			"new-horizons:sorceryMagic", "new-horizons:lightMagic", "new-horizons:natureMagic",
			"new-horizons:havocMagic",
			"new-horizons:spellcraft"})
			attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode(key)), 0, ChangeValueMode::ABSOLUTE);
		const auto schools = newHorizonsMagic::spellSchoolSkills(attackerSideHero->getMagicRules(), targetSpell());
		const int required = newHorizonsMagic::requiredSchoolRank(attackerSideHero->getMagicRules(), targetSpell());
		if(required > 0)
		{
			ASSERT_FALSE(schools.empty());
			attackerSideHero->setSecSkillLevel(schools.front(), required, ChangeValueMode::ABSOLUTE);
		}
		startBattle();
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
		friendly = addStack(BattleSide::ATTACKER, CreatureID(CreatureID::decode("core:pikeman")), BattleHex(3, 5), 1000);
		enemy = addStack(BattleSide::DEFENDER, CreatureID(CreatureID::decode("core:pikeman")), selected == 1 ? BattleHex(4, 5) : BattleHex(12, 5), 1000);
		for(auto * unit : {friendly, enemy})
			unit->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
				BonusType::NO_MORALE, BonusSource::OTHER, 1, BonusSourceID()));
		beginCombat();
		BattleSetActiveStack active;
		active.battleID = BattleID(0);
		active.stack = friendly->unitId();
		active.reason = BattleUnitTurnReason::TURN_QUEUE;
		gameHandler->sendAndApply(active);
	}
	battle::Unit * recipient() const { return selected == 0 || selected == 1 || selected == 3 ? enemy : friendly; }
	bool paid()
	{
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = BattleSide::ATTACKER;
		action.spell = targetSpell();
		action.aimToUnit(recipient());
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action);
	}
	void originalAndMarker() const
	{
		const auto * type = attackerSideHero->getHeroType();
		ASSERT_EQ(type->spellSpecialtySuccessorProducers.size(), 1u);
		const auto & producer = type->spellSpecialtySuccessorProducers.front();
		EXPECT_EQ(producer.source, spell(row().from));
		EXPECT_EQ(producer.target, row().to);
		EXPECT_TRUE(type->spells.contains(spell(row().from)));
		EXPECT_FALSE(type->spells.contains(targetSpell()));
		const auto marker = std::string(row().damage ? "new-horizons:damage-spell-specialty:"
			: "new-horizons:non-damage-spell-specialty:")
			+ std::to_string(type->getId().getNum()) + ":" + std::to_string(targetSpell().getNum());
		size_t count = 0;
		for(const auto & bonus : attackerSideHero->getExportedBonusList())
			if(bonus->stacking == marker)
			{
				++count;
				EXPECT_NE(bonus.get(), producer.bonus.get());
				EXPECT_EQ(bonus->type, BonusType::NONE);
				EXPECT_EQ(bonus->val, 0);
			}
		EXPECT_EQ(count, 1u);
		EXPECT_NE(producer.bonus->type, BonusType::NONE);
	}
	void checkEffects(const CBattleInfoCallback & context, int power, int64_t sourceHealth, int duration) const
	{
		const auto * unit = context.battleGetUnitByID(recipient()->unitId());
		ASSERT_NE(unit, nullptr);
		const int coefficient = newHorizonsMagic::spellPowerCoefficientBasisPoints(
			context.getBattle()->getMagicRules(), attackerSideHero, targetSpell());
		const auto effects = unit->getAllBonuses(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(targetSpell())));
		if(selected == 0)
		{
			const auto formula = newHorizonsMagic::spellDirectDamage(context.getBattle()->getMagicRules(), row().to);
			ASSERT_TRUE(formula.has_value());
			const auto expected = formula->evaluateBasisPoints(power,
				attackerSideHero->getEffectPowerDivisor(targetSpell().toSpell()), coefficient, 0, 15);
			EXPECT_EQ(sourceHealth - unit->getAvailableHealth(), expected);
			EXPECT_TRUE(effects->empty());
		}
		else if(selected == 2)
		{
			const auto phantoms = context.battleGetUnitsIf([](const battle::Unit * stack)
				{ return stack->getPhantomInitialIntegrity() > 0; });
			ASSERT_EQ(phantoms.size(), 1u);
			EXPECT_EQ(phantoms.front()->getPhantomInitialIntegrity(),
				newHorizonsSorcery::phantomArmyIntegrityWithModifiers(sourceHealth, power, false, coefficient, 0, 0, 20));
			const auto * state = dynamic_cast<const battle::CUnitState *>(phantoms.front());
			ASSERT_NE(state, nullptr);
			EXPECT_EQ(state->getPhantomRoundsRemaining(), 2);
			EXPECT_EQ(unit->getAvailableHealth(), sourceHealth);
		}
		else
		{
			ASSERT_FALSE(effects->empty());
			for(const auto & effect : *effects)
			{
				if(selected == 1 || selected == 3)
				{
					EXPECT_EQ(effect->type, BonusType::COMBAT_EVENT_TRIGGER);
					EXPECT_EQ(effect->val, selected == 1
						? 15 + 7LL * power * coefficient * 115 / 10'000'000
						: 25 + 8LL * power * coefficient * 115 / 10'000'000);
					EXPECT_EQ(effect->turnsRemain, 3);
				}
				else
				{
					EXPECT_EQ(effect->turnsRemain, duration);
					if(selected == 5 && effect->type == BonusType::STACKS_SPEED)
					{
						// Retain core None/Basic Haste's actual +3 Speed. The
						// successor strengthens duration, never this payload.
						EXPECT_EQ(effect->val, 3);
					}
				}
			}
			EXPECT_EQ(unit->getAvailableHealth(), sourceHealth);
		}
	}
	void paidControl(int index, int power)
	{
		ASSERT_NO_FATAL_FAILURE(prepare(index, power));
		EXPECT_TRUE(attackerSideHero->spellbookContainsSpell(targetSpell()));
		EXPECT_FALSE(attackerSideHero->spellbookContainsSpell(spell(row().from)));
		EXPECT_EQ(percent(*attackerSideHero), row().damage ? 15 : 20);
		originalAndMarker();
		ASSERT_NO_FATAL_FAILURE(combat());
		const auto * definition = targetSpell().toSpell();
		spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, definition);
		const auto mechanics = definition->battleMechanics(&cast);
		spells::Target aim{spells::Destination(recipient())};
		ASSERT_TRUE(mechanics->canBeCastAt(aim));
		EXPECT_EQ(mechanics->getSpellPowerCoefficientBasisPoints(),
			newHorizonsMagic::schoolRankPowerCoefficientPercent(battle()->getMagicRules(),
				newHorizonsMagic::requiredSchoolRank(battle()->getMagicRules(), targetSpell())) * 100);
		const auto hp = recipient()->getAvailableHealth();
		const auto mana = attackerSideHero->getManaAvailable();
		const auto cost = battle()->battleGetSpellCost(definition, attackerSideHero);
		const int duration = mechanics->getEffectDuration();
		ASSERT_TRUE(paid());
		EXPECT_EQ(mana - attackerSideHero->getManaAvailable(), cost);
		checkEffects(*battle(), power, hp, duration);
		EXPECT_FALSE(paid());
	}
};

class ApprovedSpellSuccessors : public NewHorizonsRemainingSpellSpecialtyTest, public testing::WithParamInterface<int> {};
TEST_P(ApprovedSpellSuccessors, ActualPaidDefaultWithPower) { paidControl(GetParam(), 100); }
TEST_P(ApprovedSpellSuccessors, ActualPaidZeroPowerKeepsFixedTerms) { paidControl(GetParam(), 0); }
TEST_P(ApprovedSpellSuccessors, ExplicitMapBookPreservedAndSpecialtyIndependent)
{
	preset = true;
	ASSERT_NO_FATAL_FAILURE(prepare(GetParam()));
	EXPECT_TRUE(attackerSideHero->spellbookContainsSpell(spell(row().from)));
	EXPECT_FALSE(attackerSideHero->spellbookContainsSpell(targetSpell()));
	EXPECT_EQ(percent(*attackerSideHero), row().damage ? 15 : 20);
	originalAndMarker();
}
TEST_P(ApprovedSpellSuccessors, LegacySnapshotDoesNotConvertBookOrPrototype)
{
	legacy = true;
	ASSERT_NO_FATAL_FAILURE(prepare(GetParam()));
	EXPECT_TRUE(attackerSideHero->spellbookContainsSpell(spell(row().from)));
	EXPECT_FALSE(attackerSideHero->spellbookContainsSpell(targetSpell()));
	EXPECT_EQ(percent(*attackerSideHero), 0);
}
#ifdef ENABLE_BATTLE_AI
TEST_P(ApprovedSpellSuccessors, DetachedActualEffectMatchesPaidWithoutLiveMutation)
{
	ASSERT_NO_FATAL_FAILURE(prepare(GetParam()));
	ASSERT_NO_FATAL_FAILURE(combat());
	SuccessorEnvironment environment(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor::SPECTATOR);
	HypotheticBattle projected(&environment, callback);
	const auto hp = recipient()->getAvailableHealth();
	const auto mana = attackerSideHero->getManaAvailable();
	spells::BattleCast cast(&projected, attackerSideHero, spells::Mode::HERO, targetSpell().toSpell());
	const auto mechanics = targetSpell().toSpell()->battleMechanics(&cast);
	spells::Target aim{spells::Destination(projected.battleGetUnitByID(recipient()->unitId()))};
	ASSERT_TRUE(mechanics->canBeCastAt(aim));
	mechanics->castEval(projected.getServerCallback(), aim);
	checkEffects(projected, 100, hp, mechanics->getEffectDuration());
	EXPECT_EQ(recipient()->getAvailableHealth(), hp);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana);
	ASSERT_TRUE(paid());
	checkEffects(*battle(), 100, hp, mechanics->getEffectDuration());
}
#endif
INSTANTIATE_TEST_SUITE_P(Six, ApprovedSpellSuccessors, testing::Range(0, 6));

TEST_F(NewHorizonsRemainingSpellSpecialtyTest, CurrentWorldAndReinitializationRetainLocalCapture)
{
	ASSERT_NO_FATAL_FAILURE(prepare(3));
	const auto rules = attackerSideHero->getPrimaryGrowthRules();
	CGameState restored;
	restored.preInit(LIBRARY);
	restored.loadFromMemory(gameState()->saveToMemory());
	auto * hero = const_cast<CGHeroInstance *>(restored.getHero(attackerSideHero->id));
	ASSERT_NE(hero, nullptr);
	EXPECT_EQ(hero->getPrimaryGrowthRules(), rules);
	EXPECT_EQ(percent(*hero), 15);
	hero->initHero(*gameHandler->randomizer);
	EXPECT_EQ(percent(*hero), 15);
	size_t markers = 0;
	for(const auto & bonus : hero->getExportedBonusList())
		if(bonus->stacking.starts_with("new-horizons:damage-spell-specialty:"))
			++markers;
	EXPECT_EQ(markers, 1u);
}

TEST_F(NewHorizonsRemainingSpellSpecialtyTest, AllEnclosingOldWritersRejectBeforePrefix)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto reject = [](auto & value)
	{
		CMemorySerializer bytes;
		bytes.oser.version = static_cast<ESerializationVersion>(
			static_cast<int>(ESerializationVersion::NEW_HORIZONS_REMAINING_SPELL_SPECIALTIES) - 1);
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
}

TEST_F(NewHorizonsRemainingSpellSpecialtyTest, FalsePresenceListOnlyAndRawReadAdmission)
{
	enabled = false;
	explicitFalse = true;
	ASSERT_NO_FATAL_FAILURE(prepare());
	EXPECT_EQ(percent(*attackerSideHero), 0);
	JsonNode rules = attackerSideHero->getPrimaryGrowthRules();
	EXPECT_THROW(newHorizonsHeroes::validateRemainingSpellSpecialtySerialization(rules, false), std::runtime_error);
	rules.Struct().erase("remainingSpellSpecialtyReplacements");
	EXPECT_NO_THROW(newHorizonsHeroes::validateRemainingSpellSpecialtySerialization(rules, false));
	JsonNode identity;
	identity.String() = "new-horizons:phantomArmy";
	rules["nonDamageSpellSpecialties"]["spells"].Vector().push_back(identity);
	EXPECT_THROW(newHorizonsHeroes::validateRemainingSpellSpecialtySerialization(rules, false), std::runtime_error);
	rules["remainingSpellSpecialtyReplacements"] = JsonNode();
	EXPECT_THROW(newHorizonsHeroes::validateResolvedHeroRules(rules), std::runtime_error);
	JsonNode raw;
	raw["heroes"]["newHorizons"] = rules;
	CMemorySerializer bytes;
	bytes.oser & raw;
	GameSettings decoded;
	EXPECT_THROW(decoded.serialize(bytes.iser), std::runtime_error);
}

TEST_F(NewHorizonsRemainingSpellSpecialtyTest, HexTriggerRetainsInitialSnapshotAndUnboostedTenPercentShare)
{
	ASSERT_NO_FATAL_FAILURE(prepare(1, 100));
	ASSERT_NO_FATAL_FAILURE(combat());
	blockRetaliation(enemy);
	forceMaximumDamage(enemy);
	ASSERT_TRUE(paid());
	const auto effects = enemy->getAllBonuses(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(targetSpell())));
	ASSERT_EQ(effects->size(), 1u);
	const int64_t captured = effects->front()->val;
	ASSERT_EQ(captured, 95);
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 1000, ChangeValueMode::ABSOLUTE);
	server.attacks.clear();
	server.injuries.clear();
	ASSERT_TRUE(attack(enemy, friendly->getPosition()));
	int64_t physical = 0;
	for(const auto & event : server.attacks)
		if(event.stackAttacking == enemy->unitId() && !event.counter())
			for(const auto & hit : event.bsa)
				if(hit.stackAttacked == friendly->unitId())
					physical += hit.damageAmount;
	ASSERT_GT(physical, 0);
	int64_t echo = 0;
	for(const auto & event : server.injuries)
		for(const auto & hit : event.stacks)
			if(hit.stackAttacked == enemy->unitId() && hit.attackerID == friendly->unitId())
				echo += hit.damageAmount;
	EXPECT_EQ(echo, captured + physical / 10);
}

TEST_F(NewHorizonsRemainingSpellSpecialtyTest, PlagueTickAndChildReuseBoostedSnapshotWithoutRecalculation)
{
	ASSERT_NO_FATAL_FAILURE(prepare(3, 100));
	ASSERT_NO_FATAL_FAILURE(combat());
	auto * child = addStack(BattleSide::DEFENDER, CreatureID(CreatureID::decode("core:pikeman")), BattleHex(13, 5), 1000);
	ASSERT_NE(child, nullptr);
	ASSERT_TRUE(paid());
	const auto hp = enemy->getAvailableHealth();
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 1000, ChangeValueMode::ABSOLUTE);
	endRound();
	const auto parentEffects = enemy->getAllBonuses(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(targetSpell())));
	const auto childEffects = child->getAllBonuses(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(targetSpell())));
	ASSERT_EQ(parentEffects->size(), 1u);
	ASSERT_EQ(childEffects->size(), 1u);
	EXPECT_EQ(parentEffects->front()->val, 130);
	EXPECT_EQ(childEffects->front()->val, 130);
	EXPECT_EQ(parentEffects->front()->turnsRemain, 2);
	EXPECT_LE(childEffects->front()->turnsRemain, 3);
	EXPECT_GE(childEffects->front()->turnsRemain, 2);
	EXPECT_EQ(hp - enemy->getAvailableHealth(), 130);
}

TEST(NewHorizonsRemainingSpecialtyFormulaTest, PhantomFractionBeforeCapAndFinalFloor)
{
	EXPECT_EQ(newHorizonsSorcery::phantomArmyIntegrityWithModifiers(10000, 0, false, 10000, 0, 0, 20), 2000);
	EXPECT_EQ(newHorizonsSorcery::phantomArmyIntegrityWithModifiers(10000, 100, false, 10000, 0, 0, 20), 3800);
	EXPECT_EQ(newHorizonsSorcery::phantomArmyIntegrityWithModifiers(10000, 100, true, 10000, 0, 0, 20), 4750);
	EXPECT_EQ(newHorizonsSorcery::phantomArmyIntegrityWithModifiers(10000, 2000, true, 13000, 50, 30, 20), 5000);
	EXPECT_EQ(newHorizonsSorcery::phantomArmyIntegrityWithModifiers(923, 1, false, 10000, 0, 0, 20), 186);
	EXPECT_EQ(newHorizonsSorcery::phantomArmyIntegrityWithModifiers(10000, 100, false, 10000, 0, 0, 0),
		newHorizonsSorcery::phantomArmyIntegrity(10000, 100, false));
}
}
