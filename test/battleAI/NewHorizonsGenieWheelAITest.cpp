/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include <vcmi/Environment.h>
#include <sstream>
#include "../server/battles/NewHorizonsGenieSageFixture.h"
#include "../../AI/BattleAI/BattleEvaluator.h"
#include "../../AI/BattleAI/CreatureSpellTiming.h"
#include "../../lib/callback/CBattleCallback.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/spells/CSpell.h"
#include "../../lib/battle/NewHorizonsPuppetMaster.h"
#include "../../lib/CRandomGenerator.h"
#include "../../lib/battle/BattleAttackInfo.h"
namespace
{
class WheelEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit WheelEnvironment(std::shared_ptr<CGameState> state) : state(std::move(state)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};
class WheelCallback final : public CBattleCallback
{
public:
	explicit WheelCallback(PlayerColor player) : CBattleCallback(player, nullptr) {}
	void battleMakeUnitAction(const BattleID &, const BattleAction &) override {}
	void battleMakeSpellAction(const BattleID &, const BattleAction &) override {}
	std::optional<BattleAction> makeSurrenderRetreatDecision(const BattleID &, const BattleStateInfoForRetreat &) override
	{ return std::nullopt; }
};
}
class NewHorizonsGenieWheelAITest : public NewHorizonsGenieSageFixture
{
protected:
	void prepareUsefulPressure(bool master = false)
	{
		ASSERT_NO_FATAL_FAILURE(prepareGenie(master));
		// Avoid an overkill fixture: twenty Archangels erase the original
		// Pikemen even without Luck, so the legal buff can have zero utility.
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : {ally, enemy})
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
		ally = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(9, 5), 100);
		enemy = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(11, 5), 100);
		for(const auto & [attacker, defender] : {std::pair{ally, enemy}, std::pair{enemy, ally}})
		{
			const auto damage = battle()->calculateDmgRange(BattleAttackInfo(attacker, defender, 0, false)).damage;
			ASSERT_GT(damage.min, 0);
			ASSERT_LT(damage.max, defender->getAvailableHealth());
		}
	}
	void recordPressure(const std::shared_ptr<HypotheticBattle> & state, std::ostringstream & info)
	{
		DamageCache cache;
		cache.buildCreaturePressureCache(state, state->battleGetMySide());
		for(const auto * original : {ally, enemy})
		{
			const auto * unit = state->battleGetUnitByID(original->unitId());
			const auto * opponent = state->battleGetUnitByID((original == ally ? enemy : ally)->unitId());
			const auto outgoing = cache.getDamage(unit, opponent, state);
			const auto reverse = cache.getOriginalDamage(opponent, unit, state);
			PotentialTargets targets(unit, cache, state);
			info << " unit=" << unit->unitId() << " controller=" << state->battleGetActionController(unit).getNum()
				<< " expected=" << outgoing << " reverseDps=" << reverse
				<< " attacks=" << targets.possibleAttacks.size() << " best=" << targets.bestActionValue();
			if(!targets.possibleAttacks.empty())
			{
				const auto & best = targets.bestAction();
				info << " victim=" << best.attack.defender->unitId()
					<< " outgoing=" << best.defenderDamageReduce << " incoming=" << best.attackerDamageReduce
					<< " collateral=" << best.collateralDamageReduce << " exact=" << best.attackValue();
			}
		}
	}
	void characterizeLuckProjection(const std::shared_ptr<WheelEnvironment> & environment,
		const std::shared_ptr<WheelCallback> & callback, SpellID spell)
	{
		auto projected = std::make_shared<HypotheticBattle>(environment.get(), callback->getBattle(BattleID(0)));
		const auto * caster = projected->battleGetUnitByID(genie->unitId());
		const auto * target = battle()->battleMatchActionController(genie, ally, spell.toSpell()->isPositive())
			? ally : enemy;
		const auto targetId = target->unitId();
		const auto opponentId = target == ally ? enemy->unitId() : ally->unitId();
		const auto * beforeTarget = projected->battleGetUnitByID(targetId);
		const auto * opponent = projected->battleGetUnitByID(opponentId);
		newHorizonsPuppetMaster::ActionControllerCaster actionCaster(caster,
			projected->battleGetActionController(caster));
		spells::BattleCast cast(projected.get(), &actionCaster, spells::Mode::CREATURE_ACTIVE, spell.toSpell());
		const spells::Target destination{spells::Destination(beforeTarget)};
		ASSERT_TRUE(spell.toSpell()->canBeCast(projected.get(), spells::Mode::CREATURE_ACTIVE, &actionCaster));
		ASSERT_TRUE(spell.toSpell()->battleMechanics(&cast)->canBeCastAt(destination));
		const auto rules = projected->getLuckRollRules();
		const auto rawBefore = beforeTarget->valOfBonuses(BonusType::LUCK);
		const auto luckBefore = projected->battleGetAttackLuck(beforeTarget, opponent, false);
		const auto damageBefore = projected->battleExpectedLuckDamage(BattleAttackInfo(beforeTarget, opponent, 0, false));
		std::ostringstream pressure;
		pressure << "before";
		recordPressure(projected, pressure);
		cast.castEval(projected->getServerCallback(), destination);
		pressure << " after";
		recordPressure(projected, pressure);
		const auto * afterTarget = projected->battleGetUnitByID(targetId);
		const auto rawAfter = afterTarget->valOfBonuses(BonusType::LUCK);
		const auto luckAfter = projected->battleGetAttackLuck(afterTarget, opponent, false);
		const auto damageAfter = projected->battleExpectedLuckDamage(BattleAttackInfo(afterTarget, opponent, 0, false));
		std::ostringstream info;
		info << "spell=" << spell.getNum() << " target=" << targetId
			<< " raw=" << rawBefore << ":" << rawAfter << " attackLuck=" << luckBefore << ":" << luckAfter
			<< " expectedDamage=" << damageBefore << ":" << damageAfter
			<< " curves=" << rules.goodChance.size() << ":" << rules.badChance.size() << " dice=" << rules.diceSize
			<< " noLuck=" << afterTarget->hasBonusOfType(BonusType::NO_LUCK)
			<< " hasCap=" << afterTarget->hasBonusOfType(BonusType::MAXIMUM_LUCK)
			<< " maxLuck=" << afterTarget->valOfBonuses(BonusType::MAXIMUM_LUCK);
		info << " good=";
		for(const auto chance : rules.goodChance) info << chance << ",";
		info << " bad=";
		for(const auto chance : rules.badChance) info << chance << ",";
		const auto player = projected->battleGetActionController(caster);
		const auto outcome = BattleEvaluator(environment, callback, genie, player, BattleID(0),
			projected->playerToSide(player), 1.0f, 2).beneficialCreatureSpellOutcomeValue(genie, target, spell.toSpell());
		info << " pressure=" << pressure.str() << " outcomeValue=" << outcome;
		RecordProperty("wheelProjection" + std::to_string(spell.getNum()), info.str());
		EXPECT_EQ(rawAfter, rawBefore + (spell == bestow() ? 3 : -3)) << info.str();
		if(luckAfter > luckBefore)
			EXPECT_GT(damageAfter, damageBefore) << info.str();
		else if(luckAfter < luckBefore)
			EXPECT_LT(damageAfter, damageBefore) << info.str();
		else
			EXPECT_EQ(damageAfter, damageBefore) << info.str();
	}
	std::optional<PossibleSpellcast> choice(PlayerColor player = PlayerColor(0))
	{
		auto callback = std::make_shared<WheelCallback>(player);
		callback->onBattleStarted(battle());
		const auto environment = std::make_shared<WheelEnvironment>(gameState());
		for(const auto spell : {bestow(), turn()})
			if(newHorizonsHeroes::isExplicitGenieLuckCast(*genie, spell))
				characterizeLuckProjection(environment, callback, spell);
		creatureSpellTiming::Sample timing;
		const auto result = BattleEvaluator(environment, callback,
			genie, player, BattleID(0), battle()->playerToSide(player), 1.0f, 2).findBestCreatureSpell(genie, &timing);
		EXPECT_GT(timing.ordinarySpells, 0u) << creatureSpellTiming::fields(timing);
		EXPECT_GT(timing.ordinaryTargets, 0u) << creatureSpellTiming::fields(timing);
		EXPECT_TRUE(result.has_value()) << creatureSpellTiming::fields(timing);
		return result;
	}
	void controlGenie()
	{
		const auto puppet = SpellID(SpellID::decode("new-horizons:puppetMaster"));
		genie->addNewBonus(std::make_shared<Bonus>(
			newHorizonsPuppetMaster::controlMarker(puppet, PlayerColor(1))));
		ASSERT_TRUE(newHorizonsPuppetMaster::hasValidControlMarker(*battle(), genie));
		ASSERT_EQ(battle()->battleGetActionController(genie), PlayerColor(1));
	}
	bool controlledCast(CStack * target, SpellID spell)
	{
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(1),
			BattleAction::makeCreatureSpellcast(genie, {spells::Destination(target)}, spell));
	}
	std::optional<PossibleSpellcast> controlledChoiceRejecting(CStack * wrongTarget, SpellID spell)
	{
		const auto gameRng = wire(*gameHandler->randomizer);
		const auto defaultRng = wire(CRandomGenerator::getDefault());
		const auto state = wire(*gameState()->getBattle(BattleID(0)));
		const auto casts = genie->casts.available();
		EXPECT_FALSE(controlledCast(wrongTarget, spell));
		const auto result = choice(PlayerColor(1));
		EXPECT_EQ(genie->casts.available(), casts);
		EXPECT_EQ(wire(*gameHandler->randomizer), gameRng);
		EXPECT_EQ(wire(CRandomGenerator::getDefault()), defaultRng);
		EXPECT_EQ(wire(*gameState()->getBattle(BattleID(0))), state);
		return result;
	}
	void blockRandomPools()
	{
		for(auto * target : {genie, ally, enemy})
			for(const auto spell : battle()->getAvailableRandomCreatureSpells(genie, target))
				target->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::NONE,
					BonusSource::SPELL_EFFECT, 0, BonusSourceID(spell)));
		EXPECT_EQ(genie->casts.available(), 3);
		for(auto * target : {genie, ally, enemy})
			EXPECT_TRUE(battle()->getAvailableRandomCreatureSpells(genie, target).empty());
	}
};
TEST_F(NewHorizonsGenieWheelAITest, ExplicitBestowRemainsVisibleOnRandomSpellcasterAndForecastIsReadOnly)
{
	ASSERT_NO_FATAL_FAILURE(prepareUsefulPressure());
	blockRandomPools();
	const auto gameRng = wire(*gameHandler->randomizer);
	const auto state = wire(*gameState()->getBattle(BattleID(0)));
	const auto before = genie->casts.available();
	const auto best = choice();
	ASSERT_TRUE(best);
	EXPECT_EQ(best->spell->getId(), bestow());
	EXPECT_GT(best->value, 0);
	EXPECT_EQ(genie->casts.available(), before);
	EXPECT_EQ(wire(*gameHandler->randomizer), gameRng);
	EXPECT_EQ(wire(*gameState()->getBattle(BattleID(0))), state);
}
TEST_F(NewHorizonsGenieWheelAITest, MasterCanValueNamedHostileLuckAlongsideRandomAndFriendlyOptions)
{
	ASSERT_NO_FATAL_FAILURE(prepareUsefulPressure(true));
	blockRandomPools();
	ally->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::LUCK, BonusSource::OTHER, 10, BonusSourceID()));
	const auto best = choice();
	ASSERT_TRUE(best);
	EXPECT_EQ(best->spell->getId(), turn());
	ASSERT_EQ(best->dest.size(), 1);
	EXPECT_EQ(best->dest.front().unitValue, enemy);
	EXPECT_GT(best->value, 0);
}
TEST_F(NewHorizonsGenieWheelAITest, ControlledBestowTargetsControllerAllyAndAgreesWithAuthority)
{
	ASSERT_NO_FATAL_FAILURE(prepareUsefulPressure());
	ASSERT_NO_FATAL_FAILURE(controlGenie());
	blockRandomPools();
	const auto best = controlledChoiceRejecting(ally, bestow());
	ASSERT_TRUE(best);
	EXPECT_EQ(best->spell->getId(), bestow());
	ASSERT_EQ(best->dest.size(), 1);
	EXPECT_EQ(best->dest.front().unitValue, enemy);
	EXPECT_GT(best->value, 0);
	const auto luck = enemy->valOfBonuses(BonusType::LUCK);
	ASSERT_TRUE(controlledCast(enemy, bestow()));
	EXPECT_EQ(genie->casts.available(), 2);
	EXPECT_EQ(enemy->valOfBonuses(BonusType::LUCK), luck + 3);
}
TEST_F(NewHorizonsGenieWheelAITest, ControlledTurnTargetsOriginalAllyAndAgreesWithAuthority)
{
	ASSERT_NO_FATAL_FAILURE(prepareUsefulPressure(true));
	ASSERT_NO_FATAL_FAILURE(controlGenie());
	blockRandomPools();
	enemy->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::LUCK,
		BonusSource::OTHER, 10, BonusSourceID()));
	const auto best = controlledChoiceRejecting(enemy, turn());
	ASSERT_TRUE(best);
	EXPECT_EQ(best->spell->getId(), turn());
	ASSERT_EQ(best->dest.size(), 1);
	EXPECT_EQ(best->dest.front().unitValue, ally);
	EXPECT_GT(best->value, 0);
	const auto luck = ally->valOfBonuses(BonusType::LUCK);
	ASSERT_TRUE(controlledCast(ally, turn()));
	EXPECT_EQ(genie->casts.available(), 2);
	EXPECT_EQ(ally->valOfBonuses(BonusType::LUCK), luck - 3);
}

TEST_F(NewHorizonsGenieWheelAITest, LuckOutcomesRetainPrecastCasualtyWeightsAndFractionalPressure)
{
	ASSERT_NO_FATAL_FAILURE(prepareUsefulPressure(true));
	auto callback = std::make_shared<WheelCallback>(PlayerColor(0));
	callback->onBattleStarted(battle());
	const auto environment = std::make_shared<WheelEnvironment>(gameState());
	auto evaluator = BattleEvaluator(environment, callback, genie, PlayerColor(0), BattleID(0),
		BattleSide::ATTACKER, 1.0f, 2);
	const auto battleBefore = wire(*gameState()->getBattle(BattleID(0)));
	const auto gameRng = wire(*gameHandler->randomizer);
	const auto defaultRng = wire(CRandomGenerator::getDefault());
	auto baseline = std::make_shared<HypotheticBattle>(environment.get(), callback->getBattle(BattleID(0)));
	DamageCache original;
	original.buildDamageCache(baseline, BattleSide::ATTACKER);
	const auto reference = original.captureSnapshot();
	for(const auto spell : {bestow(), turn()})
	{
		SCOPED_TRACE(spell.getNum());
		const auto * recipient = spell == bestow() ? ally : enemy;
		const auto * opponent = recipient == ally ? enemy : ally;
		auto projected = std::make_shared<HypotheticBattle>(environment.get(), callback->getBattle(BattleID(0)));
		const auto * caster = projected->battleGetUnitByID(genie->unitId());
		newHorizonsPuppetMaster::ActionControllerCaster actionCaster(caster, PlayerColor(0));
		spells::BattleCast cast(projected.get(), &actionCaster, spells::Mode::CREATURE_ACTIVE, spell.toSpell());
		cast.castEval(projected->getServerCallback(),
			{spells::Destination(projected->battleGetUnitByID(recipient->unitId()))});

		// Use ordinary pre-cast preparation as an independent casualty reference.
		// Only current attack damage may change after the beneficial outcome.
		const auto * beforeRecipient = baseline->battleGetUnitByID(recipient->unitId());
		const auto * beforeOpponent = baseline->battleGetUnitByID(opponent->unitId());
		const auto * afterRecipient = projected->battleGetUnitByID(recipient->unitId());
		const auto * afterOpponent = projected->battleGetUnitByID(opponent->unitId());
		const auto originalDamage = original.getOriginalDamage(beforeRecipient, beforeOpponent, baseline);
		DamageCache before(&original), after(&original);
		EXPECT_NE(after.getDamage(afterRecipient, afterOpponent, projected), originalDamage);
		EXPECT_EQ(after.getOriginalDamage(afterRecipient, afterOpponent, projected), originalDamage);

		PotentialTargets beforeAlly(baseline->battleGetUnitByID(ally->unitId()), before, baseline);
		PotentialTargets beforeEnemy(baseline->battleGetUnitByID(enemy->unitId()), before, baseline);
		PotentialTargets afterAlly(projected->battleGetUnitByID(ally->unitId()), after, projected);
		PotentialTargets afterEnemy(projected->battleGetUnitByID(enemy->unitId()), after, projected);
		ASSERT_FALSE(beforeAlly.possibleAttacks.empty());
		ASSERT_FALSE(beforeEnemy.possibleAttacks.empty());
		ASSERT_FALSE(afterAlly.possibleAttacks.empty());
		ASSERT_FALSE(afterEnemy.possibleAttacks.empty());
		const float expected = afterAlly.bestAction().attackValue() - afterEnemy.bestAction().attackValue()
			- beforeAlly.bestAction().attackValue() + beforeEnemy.bestAction().attackValue();
		EXPECT_GT(expected, 0.0f);
		EXPECT_NE(expected, static_cast<float>(static_cast<int64_t>(expected)));
		EXPECT_NEAR(evaluator.beneficialCreatureSpellOutcomeValue(genie, recipient, spell.toSpell()), expected, 0.00001f);
		EXPECT_TRUE(original.matchesSnapshot(reference));
	}
	EXPECT_EQ(wire(*gameState()->getBattle(BattleID(0))), battleBefore);
	EXPECT_EQ(wire(*gameHandler->randomizer), gameRng);
	EXPECT_EQ(wire(CRandomGenerator::getDefault()), defaultRng);
}
