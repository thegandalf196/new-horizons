/*
 * NewHorizonsPlaguebearerTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"

#include "HeroCommandFixture.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/spells/CSpellHandler.h"
#include "../../../lib/spells/BattleSpellMechanics.h"
#include "../../../lib/bonuses/BonusParameters.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/battle/BattleLayout.h"
#include "../../../lib/networkPacks/SetStackEffect.h"
#include "../../../lib/networkPacks/PacksForLobby.h"
#include "../../../AI/BattleAI/StackWithBonuses.h"
#include "../../../AI/BattleAI/SpellTargetsEvaluator.h"

#include "../../../lib/GameSettings.h"
#include "../../../lib/battle/NewHorizonsPlague.h"
#include "../../../lib/spells/MagicalDamageReduction.h"
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/bonuses/BonusParameters.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../lib/gameState/QuestInfo.h"
#include "FullGameSnapshotTypes.h"
#include "../../../lib/spells/NewHorizonsMagic.h"
#include <limits>

namespace
{
constexpr std::string_view PLAGUE_KEY = "new-horizons:plague";

SpellID plagueSpell()
{
	return SpellID(SpellID::decode(std::string(PLAGUE_KEY)));
}

JsonNode certainMorale()
{
	JsonNode result;
	for(int index = 0; index < 10; ++index)
		result.Vector().emplace_back(100);
	return result;
}

class NewHorizonsPlaguebearerTest : public HeroCommandFixture
{
protected:
	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";
	}

	void mapLoaded(CMap * map) override
	{
		HeroCommandFixture::mapLoaded(map);
		const JsonNode perks(JsonPath::builtin("config/newHorizonsPerks"));
		bool found = false;
		for(const auto & entry : perks["skills"][newHorizonsPlague::SKILL]["perks"].Vector())
			if(entry["id"].String() == newHorizonsPlague::PLAGUEBEARER)
			{
				EXPECT_EQ(entry["effect"]["status"].String(), "active");
				found = true;
			}
		EXPECT_TRUE(found);
		map->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
		map->overrideGameSetting(EGameSettings::COMBAT_GOOD_MORALE_CHANCE, certainMorale());
		map->overrideGameSetting(EGameSettings::COMBAT_MORALE_DICE_SIZE, JsonNode(100));
	}

	void configureCaster(CGHeroInstance * hero)
	{
		ASSERT_NE(hero, nullptr);
		const auto spell = plagueSpell();
		ASSERT_NE(spell, SpellID::NONE);
		const auto shadowMagic = SecondarySkill::decode("new-horizons:shadowMagic");
		ASSERT_GE(shadowMagic, 0);
		hero->setSecSkillLevel(SecondarySkill(shadowMagic), MasteryLevel::BASIC,
			ChangeValueMode::ABSOLUTE);
		// Identical legal Basic prerequisite in selected and unselected Plague controls.
		// Blood Drinker affects Life Drain only, not Plague's damage or propagation.
		const std::string prerequisite = "new-horizons:shadowMagic.bloodDrinker";
		const auto ranks = [hero](const std::string & id) { return hero->getPerkSkillRank(id); };
		for(uint64_t seed = 0; seed < 10000 && !hero->hasActivePerk(newHorizonsPlague::SKILL, prerequisite); ++seed)
		{
			const auto offers = hero->getPerkState().prepareOffer(ranks, seed);
			for(size_t index = 0; index < offers.size(); ++index)
				if(offers[index].selection.perkId == prerequisite)
				{
					gameHandler->levelUpHero(hero, offers, index, seed, false);
					break;
				}
		}
		ASSERT_TRUE(hero->hasActivePerk(newHorizonsPlague::SKILL, prerequisite));
		hero->setPrimarySkill(PrimarySkill::SPELL_POWER, 100, ChangeValueMode::ABSOLUTE);
		hero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
		giveArtifact(hero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		hero->addSpellToSpellbook(spell);
		hero->addSpellToSpellbook(SpellID::DISPEL);
		setTestSpellPointTotal(hero, 1000);
	}

	void removeDeployedUnits()
	{
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
	}

	void prepare(bool adjacentStacks = false, bool targetHasMorale = false)
	{
		startGame();
		configureCaster(attackerSideHero);
		configureCaster(defenderSideHero);
		startBattle();
		removeDeployedUnits();

		if(adjacentStacks)
		{
			afflicted = addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), BattleHex(8, 5), 1000);
			ASSERT_NE(afflicted, nullptr);
		auto surrounding = afflicted->getSurroundingHexes().toVector();
			std::sort(surrounding.begin(), surrounding.end(), [](const BattleHex & lhs, const BattleHex & rhs)
			{
				return lhs.toInt() < rhs.toInt();
			});
			for(const auto & hex : surrounding)
			{
				if(!hex.isValid() || battle()->battleGetUnitByPos(hex, false))
					continue;
				if(!friendlyVictim)
					friendlyVictim = addStack(BattleSide::ATTACKER, creatureByName("core:peasant"), hex, 1000);
				else
				{
					enemyVictim = addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), hex, 1000);
					break;
				}
			}
			ASSERT_NE(friendlyVictim, nullptr);
			ASSERT_NE(enemyVictim, nullptr);
		}
		else
		{
			attacker = addStack(BattleSide::ATTACKER, creatureByName("core:peasant"), BattleHex(3, 5), 1000);
			afflicted = addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), BattleHex(12, 5), 1000);
			ASSERT_NE(attacker, nullptr);
			ASSERT_NE(afflicted, nullptr);
		}
		if(targetHasMorale)
			afflicted->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
				BonusType::MORALE, BonusSource::OTHER, 1, BonusSourceID()));
		beginCombat();
	}

	std::shared_ptr<const Bonus> plagueStatus(const CStack * stack) const
	{
		if(!stack)
			return {};
		const auto bonuses = stack->getBonuses(Selector::source(BonusSource::SPELL_EFFECT,
			BonusSourceID(plagueSpell())).And(Selector::type()(BonusType::COMBAT_EVENT_TRIGGER)));
		return bonuses->empty() ? std::shared_ptr<const Bonus>{} : bonuses->front();
	}

	bool issue(const battle::Unit * stack, const BattleAction & action)
	{
		return stack && gameHandler->battles->makePlayerBattleAction(BattleID(0),
			battle()->sideToPlayer(stack->unitSide()), action);
	}

	bool activateTarget(CStack * stack)
	{
		const auto maximumActions = battle()->stacks.size() * 8 + 8;
		for(size_t actionIndex = 0; actionIndex < maximumActions; ++actionIndex)
		{
			const auto * active = battle()->battleActiveUnit();
			if(!active)
				return false;
			if(active->unitId() == stack->unitId())
				return true;
			if(!issue(active, BattleAction::makeDefend(active)))
				return false;
		}
		return false;
	}

	bool finishTargetTurn(CStack * stack, bool waitFirst = false, int32_t * completedRound = nullptr)
	{
		const auto before = plagueStatus(stack);
		if(!before)
			return false;
		const auto roundsBefore = before->turnsRemain;
		const auto maximumActions = battle()->stacks.size() * 12 + 12;
		bool hasWaited = false;
		for(size_t actionIndex = 0; actionIndex < maximumActions; ++actionIndex)
		{
			const auto * active = battle()->battleActiveUnit();
			if(!active)
				return false;
			if(active->unitId() == stack->unitId() && waitFirst && !hasWaited)
			{
				hasWaited = true;
				if(!issue(active, BattleAction::makeWait(active)))
					return false;
				const auto afterWait = plagueStatus(stack);
				if(!afterWait || afterWait->turnsRemain != roundsBefore)
					return false;
				continue;
			}
			const auto actionRound = battle()->battleGetRound();
			if(!issue(active, BattleAction::makeDefend(active)))
				return false;
			const auto afterAction = plagueStatus(stack);
			if(!afterAction || afterAction->turnsRemain < roundsBefore)
			{
				if(completedRound)
					*completedRound = actionRound;
				return true;
			}
		}
		return false;
	}

	int64_t damageTo(uint32_t unitId, size_t firstInjury = 0) const
	{
		int64_t total = 0;
		for(size_t injuryIndex = firstInjury; injuryIndex < server.injuries.size(); ++injuryIndex)
			for(const auto & hit : server.injuries[injuryIndex].stacks)
				if(hit.stackAttacked == unitId)
					total += hit.damageAmount;
		return total;
	}

	int64_t expectedLegalCastDamage() const
	{
		const auto coefficient = newHorizonsMagic::spellPowerCoefficientBasisPoints(
			battle()->getMagicRules(), attackerSideHero, plagueSpell());
		return newHorizonsPlague::rawTickDamage(100, coefficient);
	}

	void advancedShadow(bool selected)
	{
		attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode(newHorizonsPlague::SKILL)),
			MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
		if(!selected) return;
		const auto ranks = [this](const std::string & id) { return attackerSideHero->getPerkSkillRank(id); };
		for(uint64_t seed = 0; seed < 10000; ++seed)
		{
			const auto offers = attackerSideHero->getPerkState().prepareOffer(ranks, seed);
			for(size_t index = 0; index < offers.size(); ++index)
				if(offers[index].selection.perkId == newHorizonsPlague::PLAGUEBEARER)
				{
					gameHandler->levelUpHero(attackerSideHero, offers, index, seed, false);
					ASSERT_TRUE(attackerSideHero->hasActivePerk(newHorizonsPlague::SKILL, newHorizonsPlague::PLAGUEBEARER));
					return;
				}
		}
		FAIL() << "No legal public Plaguebearer offer";
	}
	CStack * attacker = nullptr;
	CStack * afflicted = nullptr;
	CStack * friendlyVictim = nullptr;
	CStack * enemyVictim = nullptr;
};
}


namespace
{
struct PlaguePrefixProbe
{
	using Version = ESerializationVersion;
	bool saving = true;
	bool loadingGamestate = false;
	int fields = 0;
	bool hasFeature(Version value) const { return value != Version::NEW_HORIZONS_PLAGUEBEARER; }
	template <typename T> PlaguePrefixProbe & operator&(T &)
	{
		++fields;
		throw std::runtime_error("Reached ordinary payload");
	}
};
template <typename T> void rejectsBeforePrefix(T & object)
{
	PlaguePrefixProbe probe;
	EXPECT_THROW(object.serialize(probe), std::runtime_error);
	EXPECT_EQ(probe.fields, 0);
}
class PlagueEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit PlagueEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};
}

TEST_F(NewHorizonsPlaguebearerTest, SelectedActualTickInfectsBothDistinctAdjacentRecipients)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true));
	ASSERT_NO_FATAL_FAILURE(advancedShadow(true));
	ASSERT_TRUE(castOn(attackerSideHero, plagueSpell(), afflicted));
	const auto initial = plagueStatus(afflicted);
	ASSERT_NE(initial, nullptr);
	EXPECT_EQ(newHorizonsPlague::capturedPropagationLimit(*initial), 2);
	const auto raw = initial->val;
	ASSERT_TRUE(finishTargetTurn(afflicted));
	EXPECT_TRUE(newHorizonsPlague::hasPlague(friendlyVictim));
	EXPECT_TRUE(newHorizonsPlague::hasPlague(enemyVictim));
	EXPECT_EQ(plagueStatus(friendlyVictim)->val, raw);
	EXPECT_EQ(plagueStatus(enemyVictim)->val, raw);
	EXPECT_EQ(newHorizonsPlague::capturedPropagationLimit(*plagueStatus(friendlyVictim)), 2);
	EXPECT_EQ(newHorizonsPlague::capturedPropagationLimit(*plagueStatus(enemyVictim)), 2);
}
TEST_F(NewHorizonsPlaguebearerTest, UnselectedSameRankActualTickRetainsOneRecipient)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true));
	ASSERT_NO_FATAL_FAILURE(advancedShadow(false));
	ASSERT_TRUE(castOn(attackerSideHero, plagueSpell(), afflicted));
	EXPECT_EQ(newHorizonsPlague::capturedPropagationLimit(*plagueStatus(afflicted)), 1);
	ASSERT_TRUE(finishTargetTurn(afflicted));
	EXPECT_TRUE(newHorizonsPlague::hasPlague(friendlyVictim));
	EXPECT_FALSE(newHorizonsPlague::hasPlague(enemyVictim));
}
TEST_F(NewHorizonsPlaguebearerTest, NoAdjacentRecipientDoesNotInventInfectionOrExtraTick)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(advancedShadow(true));
	ASSERT_TRUE(castOn(attackerSideHero, plagueSpell(), afflicted));
	const auto count = battle()->stacks.size();
	const auto firstInjury = server.injuries.size();
	ASSERT_TRUE(finishTargetTurn(afflicted));
	EXPECT_EQ(battle()->stacks.size(), count);
	EXPECT_EQ(damageTo(afflicted->unitId(), firstInjury), expectedLegalCastDamage());
	EXPECT_EQ(plagueStatus(afflicted)->turnsRemain, 2);
}
TEST_F(NewHorizonsPlaguebearerTest, SelectedWaitAndThreeRoundDamageDurationRemainUnchanged)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(advancedShadow(true));
	ASSERT_TRUE(castOn(attackerSideHero, plagueSpell(), afflicted));
	ASSERT_EQ(plagueStatus(afflicted)->turnsRemain, 3);
	for(int tick = 0; tick < 3; ++tick)
	{
		const auto before = server.injuries.size();
		ASSERT_TRUE(finishTargetTurn(afflicted, tick == 0));
		EXPECT_EQ(damageTo(afflicted->unitId(), before), expectedLegalCastDamage());
	}
	EXPECT_FALSE(newHorizonsPlague::hasPlague(afflicted));
}
TEST_F(NewHorizonsPlaguebearerTest, LaterPerkSelectionDoesNotRetroactivelyChangeMarkerOrChildren)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true));
	ASSERT_NO_FATAL_FAILURE(advancedShadow(false));
	ASSERT_TRUE(castOn(attackerSideHero, plagueSpell(), afflicted));
	ASSERT_NO_FATAL_FAILURE(advancedShadow(true));
	ASSERT_TRUE(finishTargetTurn(afflicted));
	EXPECT_EQ(newHorizonsPlague::capturedPropagationLimit(*plagueStatus(afflicted)), 1);
	EXPECT_EQ(newHorizonsPlague::capturedPropagationLimit(*plagueStatus(friendlyVictim)), 1);
	EXPECT_FALSE(newHorizonsPlague::hasPlague(enemyVictim));
}
TEST_F(NewHorizonsPlaguebearerTest, ChildKeepsCapturedLimitAndOrdinaryFreshThreeRoundDuration)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true));
	ASSERT_NO_FATAL_FAILURE(advancedShadow(true));
	ASSERT_TRUE(castOn(attackerSideHero, plagueSpell(), afflicted));
	ASSERT_TRUE(activateTarget(afflicted));
	const auto infectionRound = battle()->battleGetRound();
	ASSERT_TRUE(issue(afflicted, BattleAction::makeDefend(afflicted)));
	const auto child = plagueStatus(friendlyVictim);
	ASSERT_NE(child, nullptr);
	EXPECT_EQ(child->turnsRemain, 3);
	EXPECT_EQ(newHorizonsPlague::capturedPropagationLimit(*child), 2);
	EXPECT_EQ(child->parameters->toCustom<JsonNode>()["spreadAttempts"].Integer(), 0);
	EXPECT_EQ(child->parameters->toCustom<JsonNode>()["lastProcessedRound"].Integer(), infectionRound - 1);
}
TEST_F(NewHorizonsPlaguebearerTest, DetachedForecastIncludesSecondEnemyRecipientWithoutMutatingLive)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true));
	ASSERT_NO_FATAL_FAILURE(advancedShadow(false));
	spells::BattleCast ordinary(battle(), attackerSideHero, spells::Mode::HERO, plagueSpell().toSpell());
	auto ordinaryMechanics = plagueSpell().toSpell()->battleMechanics(&ordinary);
	const spells::Target target{spells::Destination(afflicted)};
	const auto one = SpellTargetEvaluator::plagueDelayedDamageValue(ordinaryMechanics.get(), target);
	ASSERT_NO_FATAL_FAILURE(advancedShadow(true));
	spells::BattleCast enhanced(battle(), attackerSideHero, spells::Mode::HERO, plagueSpell().toSpell());
	auto enhancedMechanics = plagueSpell().toSpell()->battleMechanics(&enhanced);
	EXPECT_EQ(enhancedMechanics->getPlaguePropagationLimit(), 2);
	EXPECT_GT(SpellTargetEvaluator::plagueDelayedDamageValue(enhancedMechanics.get(), target), one);
	EXPECT_FALSE(newHorizonsPlague::hasPlague(afflicted));
	EXPECT_FALSE(newHorizonsPlague::hasPlague(friendlyVictim));
	EXPECT_FALSE(newHorizonsPlague::hasPlague(enemyVictim));
	ASSERT_TRUE(castOn(attackerSideHero, plagueSpell(), afflicted));
	PlagueEnvironment environment(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	auto parent = std::make_shared<HypotheticBattle>(&environment, callback);
	auto child = std::make_shared<HypotheticBattle>(&environment, parent);
	const auto projected = child->getForUpdate(afflicted->unitId());
	const auto bonuses = projected->getBonuses(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(plagueSpell()))
		.And(Selector::type()(BonusType::COMBAT_EVENT_TRIGGER)));
	ASSERT_FALSE(bonuses->empty());
	EXPECT_EQ(newHorizonsPlague::capturedPropagationLimit(*bonuses->front()), 2);
	EXPECT_EQ(newHorizonsPlague::capturedPropagationLimit(*plagueStatus(afflicted)), 2);
}
TEST_F(NewHorizonsPlaguebearerTest, CurrentMarkerRoundTripAndOwningOldPacketPrefixes)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(advancedShadow(true));
	ASSERT_TRUE(castOn(attackerSideHero, plagueSpell(), afflicted));
	Bonus saved(*plagueStatus(afflicted));
	CMemorySerializer memory;
	saved.serialize(memory.oser);
	Bonus restored;
	restored.serialize(memory.iser);
	EXPECT_EQ(newHorizonsPlague::capturedPropagationLimit(restored), 2);
	Bonus ordinary(saved);
	auto ordinaryParameters = ordinary.parameters->toCustom<JsonNode>();
	ordinaryParameters["propagationLimit"].Integer() = 1;
	ordinary.parameters = std::make_shared<BonusParameters>(ordinaryParameters);
	PlaguePrefixProbe legacyProbe;
	EXPECT_NO_THROW(ordinary.validatePlagueSerialization(legacyProbe));
	rejectsBeforePrefix(saved);
	rejectsBeforePrefix(*afflicted);
	rejectsBeforePrefix(*battle());
	SetStackEffect effects;
	effects.toAdd = {{afflicted->unitId(), {saved}}};
	rejectsBeforePrefix(effects);
	UnitChanges change(afflicted->unitId(), UnitChanges::EOperation::UPDATE);
	change.data["bonuses"].Vector().push_back(saved.toJsonNode());
	rejectsBeforePrefix(change);
	BattleUnitsChanged units;
	units.changedStacks.push_back(change);
	rejectsBeforePrefix(units);
	BattleStackAttacked hit;
	hit.newState = change;
	rejectsBeforePrefix(hit);
	BattleAttack attack;
	attack.bsa.push_back(hit);
	rejectsBeforePrefix(attack);
	StacksInjured injury;
	injury.stacks.push_back(hit);
	rejectsBeforePrefix(injury);
	BattleStart start;
	start.info = BattleInfo::setupBattle(gameState().get(), {4,4,0}, gameState()->getTile({4,4,0})->getTerrainID(),
		BattleField(*LIBRARY->identifiers()->getIdentifier(ModScope::scopeGame(), "battlefield", "core:sand_shore")),
		{attackerSideHero, defenderSideHero}, {attackerSideHero, defenderSideHero},
		BattleLayout::createDefaultLayout(*gameState(), attackerSideHero, defenderSideHero), nullptr);
	auto * stack = start.info->getStack(start.info->stacks.front()->unitId(), false);
	stack->addNewBonus(std::make_shared<Bonus>(saved));
	rejectsBeforePrefix(start);
}
TEST_F(NewHorizonsPlaguebearerTest, MalformedCurrentUnitMarkerReadRejectsAndLegacyMissingLimitDefaultsOne)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(castOn(attackerSideHero, plagueSpell(), afflicted));
	Bonus legacy(*plagueStatus(afflicted));
	auto parameters = legacy.parameters->toCustom<JsonNode>();
	parameters.Struct().erase("propagationLimit");
	legacy.parameters = std::make_shared<BonusParameters>(parameters);
	EXPECT_EQ(newHorizonsPlague::capturedPropagationLimit(legacy), 1);
	UnitChanges malformed(afflicted->unitId(), UnitChanges::EOperation::UPDATE);
	auto node = legacy.toJsonNode();
	node["addInfo"]["propagationLimit"].Bool() = true;
	SCOPED_TRACE(node.toCompactString());
	EXPECT_EQ(node["type"].String(), "COMBAT_EVENT_TRIGGER");
	EXPECT_EQ(node["sourceType"].String(), "SPELL_EFFECT");
	EXPECT_EQ(node["sourceID"].String(), PLAGUE_KEY);
	EXPECT_THROW(newHorizonsPlague::containsExtendedPropagation(node), std::runtime_error);
	malformed.data["bonuses"].Vector().push_back(node);
	CMemorySerializer memory;
	memory.oser & malformed.id;
	memory.oser & malformed.healthDelta;
	memory.oser & malformed.data;
	memory.oser & malformed.operation;
	UnitChanges restored;
	EXPECT_THROW(restored.serialize(memory.iser), std::runtime_error);
	EXPECT_EQ(newHorizonsPlague::capturedPropagationLimit(*plagueStatus(afflicted)), 1);
}
TEST_F(NewHorizonsPlaguebearerTest, ConfigurableNormalLimitValidatesWithoutInventingChainLifetime)
{
	JsonNode rules(JsonPath::builtin("config/newHorizonsMagic"));
	rules["spells"][std::string(PLAGUE_KEY)]["propagationLimit"].Integer() = 2;
	EXPECT_NO_THROW(newHorizonsMagic::validateRules(rules));
	EXPECT_EQ(newHorizonsPlague::normalPropagationLimit(rules), 2);
	for(const auto invalid : {0, -1})
	{
		rules["spells"][std::string(PLAGUE_KEY)]["propagationLimit"].Integer() = invalid;
		EXPECT_THROW(newHorizonsMagic::validateRules(rules), std::runtime_error);
		EXPECT_THROW(newHorizonsPlague::normalPropagationLimit(rules), std::runtime_error);
	}
}

TEST_F(NewHorizonsPlaguebearerTest, CapturedLuaIntegralNumbersAreValidButRawRulesRemainStrict)
{
	JsonNode rules(JsonPath::builtin("config/newHorizonsMagic"));
	rules["spells"][std::string(PLAGUE_KEY)]["propagationLimit"].Float() = 2;
	EXPECT_THROW(newHorizonsMagic::validateRules(rules), std::runtime_error);
	EXPECT_THROW(newHorizonsPlague::normalPropagationLimit(rules), std::runtime_error);
	Bonus marker;
	marker.type = BonusType::COMBAT_EVENT_TRIGGER;
	marker.source = BonusSource::SPELL_EFFECT;
	marker.sid = BonusSourceID(plagueSpell());
	JsonNode captured;
	for(const auto valid : {1.0, 2.0, static_cast<double>(std::numeric_limits<int32_t>::max())})
	{
		captured["propagationLimit"].Float() = valid;
		marker.parameters = std::make_shared<BonusParameters>(captured);
		EXPECT_EQ(newHorizonsPlague::capturedPropagationLimit(marker), static_cast<int32_t>(valid));
		EXPECT_EQ(newHorizonsPlague::containsExtendedPropagation(marker.toJsonNode()), valid > 1);
	}
	for(const auto invalid : {0.0, -1.0, 1.5, static_cast<double>(std::numeric_limits<int32_t>::max()) + 1,
		std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()})
	{
		captured["propagationLimit"].Float() = invalid;
		marker.parameters = std::make_shared<BonusParameters>(captured);
		EXPECT_THROW(newHorizonsPlague::capturedPropagationLimit(marker), std::runtime_error);
		EXPECT_THROW(newHorizonsPlague::containsExtendedPropagation(marker.toJsonNode()), std::runtime_error);
	}
	captured["propagationLimit"].Bool() = true;
	marker.parameters = std::make_shared<BonusParameters>(captured);
	EXPECT_THROW(newHorizonsPlague::capturedPropagationLimit(marker), std::runtime_error);
	EXPECT_THROW(newHorizonsPlague::containsExtendedPropagation(marker.toJsonNode()), std::runtime_error);
}

TEST_F(NewHorizonsPlaguebearerTest, RawRulePresenceGuardsSettingsMapWorldAndLobbyBeforePrefixes)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto rules = gameState()->getMagicRules();
	rules["spells"][std::string(PLAGUE_KEY)]["propagationLimit"].Integer() = 1;
	GameSettings settings;
	settings.addOverride(EGameSettings::MAGIC_NEW_HORIZONS, rules);
	rejectsBeforePrefix(settings);
	gameState()->getMap().overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, rules);
	rejectsBeforePrefix(gameState()->getMap());
	// The world delegates the raw map override even when its own capture lacks it.
	rejectsBeforePrefix(*gameState());
	LobbyStartGame lobby;
	lobby.initializedGameState = gameState();
	lobby.initializedStartInfo = std::make_shared<StartInfo>(*gameState()->getStartInfo());
	rejectsBeforePrefix(lobby);
	rules["spells"][std::string(PLAGUE_KEY)].Struct().erase("propagationLimit");
	EXPECT_NO_THROW(newHorizonsPlague::validateRuleSerialization(rules, false));
}
TEST_F(NewHorizonsPlaguebearerTest, RawRuleReaderRejectsOldPresenceAndMalformedCurrentBeforeInstallation)
{
	JsonNode raw;
	raw["magic"]["newHorizons"] = JsonNode(JsonPath::builtin("config/newHorizonsMagic"));
	// Keep the old reader control attributable to Plague, not a later capture.
	raw["magic"]["newHorizons"].Struct().erase("protectedAdventureBarriers");
	raw["magic"]["newHorizons"]["spells"][std::string(PLAGUE_KEY)]["propagationLimit"].Integer() = 1;
	CMemorySerializer old;
	old.oser & raw;
	old.iser.version = static_cast<ESerializationVersion>(
		static_cast<int>(ESerializationVersion::NEW_HORIZONS_PLAGUEBEARER) - 1);
	GameSettings decoded;
	EXPECT_THROW(decoded.serialize(old.iser), std::runtime_error);
	EXPECT_FALSE(decoded.getMagicOverride().has_value());
	raw["magic"]["newHorizons"]["spells"][std::string(PLAGUE_KEY)]["propagationLimit"] = JsonNode();
	CMemorySerializer malformed;
	malformed.oser & raw;
	EXPECT_THROW(decoded.serialize(malformed.iser), std::runtime_error);
	EXPECT_FALSE(decoded.getMagicOverride().has_value());
	raw["magic"]["newHorizons"]["spells"][std::string(PLAGUE_KEY)].Struct().erase("propagationLimit");
	CMemorySerializer absent;
	absent.oser & raw;
	ASSERT_NO_THROW(decoded.serialize(absent.iser));
	ASSERT_TRUE(decoded.getMagicOverride().has_value());
	EXPECT_FALSE((*decoded.getMagicOverride())["spells"][std::string(PLAGUE_KEY)].Struct().contains("propagationLimit"));
}
