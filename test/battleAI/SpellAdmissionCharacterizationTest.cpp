/*
 * SpellAdmissionCharacterizationTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "../server/battles/HeroCommandFixture.h"
#include "../../AI/BattleAI/AttackPossibility.h"
#include "../../AI/BattleAI/BattleEvaluator.h"
#include "../../AI/BattleAI/HypotheticBattle.h"
#include "../../lib/CRandomGenerator.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/battle/CUnitState.h"
#include "../../lib/callback/CBattleCallback.h"
#include "../../lib/callback/GameRandomizer.h"
#include "../../lib/modding/CModHandler.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include "../../lib/spells/BattleSpellMechanics.h"
#include "../../lib/spells/CSpell.h"
#include "../../lib/spells/NewHorizonsMagic.h"
#include "../../lib/spells/NewHorizonsSpellAvailability.h"

#include <algorithm>
#include <array>
#include <limits>
#include <sstream>

namespace
{
class AdmissionEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit AdmissionEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class AdmissionCallback final : public CBattleCallback
{
public:
	std::vector<BattleAction> requests, unitRequests;
	AdmissionCallback() : CBattleCallback(PlayerColor(0), nullptr) {}
	void battleMakeSpellAction(const BattleID &, const BattleAction & action) override { requests.push_back(action); }
	void battleMakeUnitAction(const BattleID &, const BattleAction & action) override { unitRequests.push_back(action); }
};

std::string hexBytes(const std::vector<std::byte> & bytes)
{
	constexpr char digits[] = "0123456789abcdef";
	std::string result;
	for(const auto byte : bytes)
	{
		const auto value = std::to_integer<unsigned int>(byte);
		result.push_back(digits[value >> 4]);
		result.push_back(digits[value & 15]);
	}
	return result;
}

template<typename T> std::string wireHex(const T & value)
{
	CMemorySerializer memory;
	memory.oser & value;
	return hexBytes(memory.extractBuffer());
}

// Explicit unit-state/allowance/order observations, not a universal bonus graph
// serializer or proof of every hidden runtime field.
std::string admissionView(const CBattleInfoCallback & view)
{
	std::ostringstream out;
	auto units = view.battleGetAllUnits(false);
	std::sort(units.begin(), units.end(), [](const auto * a, const auto * b) { return a->unitId() < b->unitId(); });
	for(const auto * unit : units)
		out << "unit=" << unit->unitId() << ':' << unit->acquireState()->save().toString();
	const auto * state = view.getBattle();
	out << ";round=" << state->getRound() << ";active=" << state->getActiveStackID();
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
		out << ";side=" << static_cast<int>(side)
			<< ";ledger=" << wireHex(state->getHeroActionAllowances(side))
			<< ";orders=" << wireHex(state->getHeroOrderStates(side))
			<< ";used=" << state->getHeroCommandUsed(side)
			<< ";casts=" << state->getCastSpells(side);
	return out.str();
}

struct AdmissionObservation
{
	std::array<std::string, 3> views;
	std::array<int64_t, 6> spellPoints;
	std::string authorityRng, fixtureRng, traffic;
	bool operator==(const AdmissionObservation &) const = default;
};

struct AdmissionSelection
{
	BattleAction ordinary;
	bool canCast = false, submitted = false;
	AdmissionObservation before, after;
	std::string childAfter;
};
}

class SpellAdmissionCharacterizationTest : public HeroCommandFixture
{
protected:
	CStack * active = nullptr;
	CStack * enemy = nullptr;
	std::shared_ptr<AdmissionEnvironment> environment;
	std::shared_ptr<AdmissionCallback> callback;
	std::shared_ptr<CPlayerBattleCallback> live;
	std::shared_ptr<HypotheticBattle> parent, child, sibling;

	void mapLoaded(CMap * map) override
	{
		HeroCommandFixture::mapLoaded(map); // Keep all canonical Orders enabled.
		map->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, JsonNode(JsonPath::builtin("config/newHorizonsHeroes")));
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_CAPABILITIES,
			JsonNode(JsonPath::builtin("config/newHorizonsCapabilities")));
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
	}

	void prepare()
	{
		ASSERT_TRUE(vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE));
		dynamic_cast<CRandomGenerator &>(*getRNG()).setSeed(seed);
		ASSERT_NO_FATAL_FAILURE(startGame());
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		attackerSideHero->removeAllSpells();
		attackerSideHero->addSpellToSpellbook(SpellID::IMPLOSION);
		const auto sorcery = SecondarySkill(SecondarySkill::decode("new-horizons:sorceryMagic"));
		ASSERT_TRUE(sorcery.hasValue());
		attackerSideHero->setSecSkillLevel(sorcery, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
		const auto * spell = SpellID(SpellID::IMPLOSION).toSpell();
		const auto divisor = attackerSideHero->getEffectPowerDivisor(spell);
		ASSERT_GT(divisor, 0);
		const auto rating = int64_t{99} * divisor;
		ASSERT_LE(rating, std::numeric_limits<int32_t>::max());
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, static_cast<int32_t>(rating), ChangeValueMode::ABSOLUTE);
		ASSERT_EQ(attackerSideHero->getEffectPower(spell), rating); // Reject a silent setup clamp.
		ASSERT_EQ(attackerSideHero->getEffectPowerDivisor(spell), divisor);
		setTestSpellPointTotal(attackerSideHero, 1000);
		ASSERT_NO_FATAL_FAILURE(startBattle());
		ASSERT_NO_FATAL_FAILURE(beginCombat());
		active = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(70), 10);
		enemy = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(71), 100);
		ASSERT_EQ(battle()->battleGetAllUnits(false).size(), 4u);
		BattleSetActiveStack activate;
		activate.battleID = BattleID(0);
		activate.stack = active->unitId();
		activate.reason = BattleUnitTurnReason::TURN_QUEUE;
		gameHandler->sendAndApply(activate);
		callback = std::make_shared<AdmissionCallback>();
		callback->onBattleStarted(battle());
		live = callback->getBattle(BattleID(0));
		environment = std::make_shared<AdmissionEnvironment>(gameState());
		ASSERT_EQ(live->battleGetMyHero(), attackerSideHero);
		ASSERT_TRUE(newHorizonsMagic::rulesActive(battle()->getMagicRules()));
		ASSERT_TRUE(newHorizonsMagic::spellAllowedBySavedRoster(battle()->getMagicRules(), SpellID::IMPLOSION));
		ASSERT_EQ(attackerSideHero->getSpellsInSpellbook(), std::set<SpellID>{SpellID::IMPLOSION});
		ASSERT_TRUE(spell->canBeCast(live.get(), spells::Mode::HERO, attackerSideHero));
		spells::BattleCast cast(live.get(), attackerSideHero, spells::Mode::HERO, spell);
		const auto damage = spell->battleMechanics(&cast)->adjustEffectValue(enemy);
		const auto melee = battle()->calculateDmgRange(BattleAttackInfo(active, enemy, 0, false)).damage.max;
		ASSERT_GT(damage, melee / 2);
		ASSERT_LT(damage, enemy->getAvailableHealth());
		ASSERT_TRUE(live->battleCanUseHeroCommand(BattleSide::ATTACKER, HeroCommand::CHARGE));
		const auto grant = live->battleGetSpellActionAllowance(BattleSide::ATTACKER, SpellID::IMPLOSION);
		ASSERT_TRUE(grant.has_value());
		ASSERT_EQ(grant->source, HeroActionAllowanceState::GrantSource::ROUND);
		ASSERT_EQ(grant->allowance, HeroActionAllowanceState::AllowanceKind::HERO);
		ASSERT_FALSE(live->battleHasPendingDoubleCommand(BattleSide::ATTACKER));
		ASSERT_FALSE(live->battleHasPendingPreCombatOrder(BattleSide::ATTACKER));
		ASSERT_FALSE(battle()->getCrisisCommandState().choice());
		RecordProperty("savedMagicRulesVersion", std::to_string(battle()->getMagicRules()["rulesetVersion"].Integer()));
		RecordProperty("savedOrderRulesVersion", std::to_string(battle()->getHeroCommandRules()["rulesetVersion"].Integer()));
		RecordProperty("effectivePowerRating", std::to_string(rating));
		RecordProperty("forecastDamage", std::to_string(damage));
		// Retaliation queries latch a real within-round allowance, including after Dispel.
		// Preserve the cold observation, then materialize before any retained branch.
		RecordProperty("preMaterializationLive", admissionView(*live));
		for(const auto * unit : live->battleGetAllUnits(false))
			(void)unit->ableToRetaliate();
		RecordProperty("materializedLive", admissionView(*live));
	}

	void retainBranches()
	{
		parent = std::make_shared<HypotheticBattle>(environment.get(), live);
		child = std::make_shared<HypotheticBattle>(environment.get(), parent);
		sibling = std::make_shared<HypotheticBattle>(environment.get(), parent);
	}

	AdmissionObservation observe()
	{
		AdmissionObservation result;
		result.views = {admissionView(*live), admissionView(*parent), admissionView(*sibling)};
		result.spellPoints = {attackerSideHero->getManaAvailable(), attackerSideHero->getNormalSpellPoints(),
			attackerSideHero->getBufferSpellPoints(), defenderSideHero->getManaAvailable(),
			defenderSideHero->getNormalSpellPoints(), defenderSideHero->getBufferSpellPoints()};
		result.authorityRng = wireHex(*gameHandler->randomizer);
		result.fixtureRng = wireHex(dynamic_cast<CRandomGenerator &>(*getRNG()));
		result.traffic = wireHex(std::vector<size_t>{server.startedActions.size(), server.stackActivations.size(),
			server.orderStateUpdates.size(), server.attacks.size(), server.injuries.size(), server.battleLogLines.size(),
			server.casts.size(), server.battleAnimations.size(), server.unitAdditionRounds.size(), server.progressionPackets.size()});
		return result;
	}

	AdmissionSelection select(bool allowSpells)
	{
		AdmissionSelection result;
		result.before = observe();
		DamageCache cache;
		cache.buildDamageCache(child, BattleSide::ATTACKER);
		BattleEvaluator evaluator(environment, callback, child, cache, active, PlayerColor(0), BattleID(0),
			BattleSide::ATTACKER, 1.0f, 2);
		result.ordinary = evaluator.selectStackAction(active);
		result.canCast = evaluator.canCastSpell();
		result.submitted = evaluator.attemptCastingSpell(active, allowSpells); // Even when canCastSpell is false.
		result.after = observe();
		result.childAfter = admissionView(*child);
		return result;
	}

	void expectReadOnlySelection(const AdmissionSelection & selection)
	{
		EXPECT_EQ(selection.after, selection.before);
		EXPECT_TRUE(callback->unitRequests.empty());
		for(size_t index = 0; index < selection.before.views.size(); ++index)
		{
			RecordProperty("selectorBeforeView" + std::to_string(index), selection.before.views[index]);
			RecordProperty("selectorAfterView" + std::to_string(index), selection.after.views[index]);
		}
		RecordProperty("selectorChildAfter", selection.childAfter);
		RecordProperty("selectorBeforePoints", wireHex(selection.before.spellPoints));
		RecordProperty("selectorAfterPoints", wireHex(selection.after.spellPoints));
		RecordProperty("selectorBeforeAuthorityRng", selection.before.authorityRng);
		RecordProperty("selectorAfterAuthorityRng", selection.after.authorityRng);
		RecordProperty("selectorBeforeFixtureRng", selection.before.fixtureRng);
		RecordProperty("selectorAfterFixtureRng", selection.after.fixtureRng);
		RecordProperty("selectorBeforeTraffic", selection.before.traffic);
		RecordProperty("selectorAfterTraffic", selection.after.traffic);
		RecordProperty("ordinaryWire", wireHex(selection.ordinary));
		RecordProperty("heroRequestCount", std::to_string(callback->requests.size()));
		for(size_t index = 0; index < callback->requests.size(); ++index)
			RecordProperty("heroRequestWire" + std::to_string(index), wireHex(callback->requests[index]));
	}

	void recordAccepted(const std::string & prefix)
	{
		const auto state = observe();
		RecordProperty(prefix + "Live", state.views[0]);
		RecordProperty(prefix + "Points", wireHex(state.spellPoints));
		RecordProperty(prefix + "AuthorityRng", state.authorityRng);
		RecordProperty(prefix + "FixtureRng", state.fixtureRng);
		RecordProperty(prefix + "Traffic", state.traffic);
	}
};

TEST_F(SpellAdmissionCharacterizationTest, PositiveSpellBeatsOrdersAndAuthoritativeCastPaysOnlyAfterRequest)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	retainBranches();
	const auto result = select(true);
	expectReadOnlySelection(result);
	ASSERT_TRUE(result.canCast);
	ASSERT_TRUE(result.submitted);
	ASSERT_EQ(callback->requests.size(), 1u);
	const auto action = callback->requests.front();
	BattleAction expected;
	expected.actionType = EActionType::HERO_SPELL;
	expected.side = BattleSide::ATTACKER;
	expected.stackNumber = static_cast<uint32_t>(-1);
	expected.spell = SpellID::IMPLOSION;
	expected.aimToUnit(enemy);
	EXPECT_EQ(wireHex(action), wireHex(expected)); // Includes every optional/retired wire field.
	const auto health = enemy->getAvailableHealth();
	const auto mana = attackerSideHero->getManaAvailable();
	const auto cost = live->battleGetSpellCost(SpellID(SpellID::IMPLOSION).toSpell(), attackerSideHero);
	const auto ledger = battle()->getHeroActionAllowances(BattleSide::ATTACKER);
	const auto traffic = result.after.traffic;
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana - cost);
	EXPECT_LT(enemy->getAvailableHealth(), health);
	EXPECT_GT(enemy->getAvailableHealth(), 0);
	EXPECT_NE(battle()->getHeroActionAllowances(BattleSide::ATTACKER), ledger);
	EXPECT_FALSE(live->battleGetSpellActionAllowance(BattleSide::ATTACKER, SpellID::IMPLOSION));
	EXPECT_EQ(battle()->battleCastSpells(BattleSide::ATTACKER), 1);
	EXPECT_NE(observe().traffic, traffic);
	RecordProperty("acceptedSpellNormal", attackerSideHero->getNormalSpellPoints());
	RecordProperty("acceptedSpellBuffer", attackerSideHero->getBufferSpellPoints());
	recordAccepted("acceptedSpell");
}

TEST_F(SpellAdmissionCharacterizationTest, AllowSpellsFalseSuppressesSpellButKeepsLegalOrderCompetition)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	retainBranches();
	const auto result = select(false);
	expectReadOnlySelection(result);
	ASSERT_TRUE(result.canCast);
	ASSERT_TRUE(result.submitted);
	ASSERT_EQ(callback->requests.size(), 1u);
	const auto action = callback->requests.front();
	ASSERT_EQ(action.actionType, EActionType::HERO_COMMAND);
	ASSERT_NE(action.command, HeroCommand::NONE);
	EXPECT_TRUE(live->battleCanBeginHeroCommand(BattleSide::ATTACKER, action.command));
	ASSERT_LE(action.target.size(), 2u);
	BattleAction expected = BattleAction::makeHeroCommand(BattleSide::ATTACKER, action.command);
	if(action.target.size() == 1)
		expected = BattleAction::makeTargetedHeroCommand(BattleSide::ATTACKER, action.command, action.target[0].unitValue);
	if(action.target.size() == 2)
		expected = BattleAction::makePairedHeroCommand(BattleSide::ATTACKER, action.command,
			action.target[0].unitValue, action.target[1].unitValue);
	EXPECT_EQ(wireHex(action), wireHex(expected));
	const auto mana = attackerSideHero->getManaAvailable();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana);
	EXPECT_TRUE(battle()->getHeroCommandUsed(BattleSide::ATTACKER));
	EXPECT_EQ(battle()->battleCastSpells(BattleSide::ATTACKER), 0);
	EXPECT_FALSE(live->battleGetSpellActionAllowance(BattleSide::ATTACKER, SpellID::IMPLOSION));
	recordAccepted("acceptedOrder");
}

TEST_F(SpellAdmissionCharacterizationTest, ExhaustedRoundRejectsAllActionsWithoutMutation)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto spend = BattleAction::makeHeroCommand(BattleSide::ATTACKER, HeroCommand::CHARGE);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), spend));
	ASSERT_FALSE(live->battleGetSpellActionAllowance(BattleSide::ATTACKER, SpellID::IMPLOSION));
	ASSERT_FALSE(live->battleCanUseHeroCommand(BattleSide::ATTACKER, HeroCommand::HOLD_THE_LINE));
	ASSERT_FALSE(live->battleCanUseMetamagicFollowup(BattleSide::ATTACKER));
	ASSERT_FALSE(live->battleGetDivineMandateStatus(BattleSide::ATTACKER).active);
	retainBranches(); // Capture AFTER the real paid Order, never seed a ledger.
	const auto result = select(true);
	expectReadOnlySelection(result);
	EXPECT_FALSE(result.canCast);
	EXPECT_FALSE(result.submitted);
	EXPECT_TRUE(callback->requests.empty());
	RecordProperty("acceptedSpendWire", wireHex(spend));
}
