/*
 * NewHorizonsRealityWarpAITest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"

#include "../server/battles/HeroCommandFixture.h"
#include "../../AI/BattleAI/BattleEvaluator.h"
#include "../../AI/BattleAI/SpellTargetsEvaluator.h"
#include "../../AI/BattleAI/StackWithBonuses.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/CRandomGenerator.h"
#include "../../lib/battle/BattleAction.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/callback/CBattleCallback.h"
#include "../../lib/modding/CModHandler.h"
#include "../../lib/spells/CSpell.h"
#include "../../lib/spells/NewHorizonsMagic.h"
#include "../../lib/spells/NewHorizonsRealityWarp.h"
#include "../../lib/spells/Problem.h"

namespace
{
class WarpEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit WarpEnvironment(std::shared_ptr<CGameState> state) : state(std::move(state)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class WarpCallback final : public CBattleCallback
{
public:
	std::vector<BattleAction> submitted;
	WarpCallback() : CBattleCallback(PlayerColor(0), nullptr) {}
	void battleMakeSpellAction(const BattleID &, const BattleAction & action) override { submitted.push_back(action); }
};

SpellID warpSpell() { return SpellID(SpellID::decode(std::string(newHorizonsRealityWarp::SPELL_KEY))); }

struct RandomStateArchive
{
	bool saving = true;
	std::string state;
	void operator&(std::string & value) { state = value; }
};
}

class NewHorizonsRealityWarpAITest : public HeroCommandFixture
{
protected:
	CStack * active = nullptr;
	CStack * friendly = nullptr;
	CStack * enemy = nullptr;
	std::shared_ptr<WarpEnvironment> environment;
	std::shared_ptr<WarpCallback> callback;
	bool includeWarp = true;

	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires separate native curated preset";
	}

	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded); // Honor the fixture's authored command profile.
		JsonNode rules(JsonPath::builtin("config/newHorizonsMagic"));
		if(!includeWarp)
			rules["spells"].Struct().erase(std::string(newHorizonsRealityWarp::SPELL_KEY));
		newHorizonsMagic::validateRules(rules);
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, rules);
	}

	void prepare(bool breaker = false)
	{
		startGame();
		ASSERT_TRUE(warpSpell().hasValue());
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		attackerSideHero->removeAllSpells();
		attackerSideHero->addSpellToSpellbook(warpSpell());
		setTestSpellPointTotal(attackerSideHero, 1000);
		const SecondarySkill chaos(SecondarySkill::decode("new-horizons:chaosMagic"));
		attackerSideHero->setSecSkillLevel(chaos, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
		if(breaker)
		{
			attackerSideHero->applyPerkSelection({"new-horizons:chaosMagic", "new-horizons:chaosMagic.misfortuneWeaver"});
			attackerSideHero->applyPerkSelection({"new-horizons:chaosMagic", "new-horizons:chaosMagic.realityBreaker"});
			ASSERT_TRUE(attackerSideHero->hasActivePerk("new-horizons:chaosMagic", "new-horizons:chaosMagic.realityBreaker"));
		}
		startBattle();
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
		active = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(3, 5), 2000);
		friendly = addStack(BattleSide::ATTACKER, creatureByName("core:marksman"), BattleHex(3, 8), 200);
		enemy = addStack(BattleSide::DEFENDER, creatureByName("core:marksman"), BattleHex(14, 5), 1000);
		ASSERT_NE(active, nullptr);
		ASSERT_NE(friendly, nullptr);
		ASSERT_NE(enemy, nullptr);
		Bonus immobilized;
		immobilized.type = BonusType::STACKS_SPEED;
		immobilized.duration = BonusDuration::ONE_BATTLE;
		immobilized.val = -static_cast<int32_t>(active->getMovementRange());
		active->addNewBonus(std::make_shared<Bonus>(immobilized));
		beginCombat();
		for(int attempt = 0; attempt < 16 && battle()->battleActiveUnit() != active; ++attempt)
		{
			const auto * current = battle()->battleActiveUnit();
			ASSERT_NE(current, nullptr);
			const auto action = current == enemy && !enemy->waited()
				? BattleAction::makeWait(current) : BattleAction::makeDefend(current);
			ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), battle()->battleGetOwner(current), action));
		}
		ASSERT_EQ(battle()->battleActiveUnit(), active);
		ASSERT_TRUE(enemy->waited());
		ASSERT_TRUE(enemy->willMove(0));
		callback = std::make_shared<WarpCallback>();
		callback->onBattleStarted(battle());
		environment = std::make_shared<WarpEnvironment>(gameState());
	}

	void bless(CStack * target)
	{
		Bonus bonus(BonusDuration::N_TURNS, BonusType::ALWAYS_MAXIMUM_DAMAGE,
			BonusSource::SPELL_EFFECT, 0, BonusSourceID(SpellID(SpellID::BLESS)));
		bonus.valType = BonusValueType::INDEPENDENT_MAX;
		bonus.turnsRemain = 3;
		bonus.spellCasterOwner = target->unitOwner();
		target->addNewBonus(std::make_shared<Bonus>(bonus));
	}

	std::vector<spells::Target> candidates()
	{
		spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, warpSpell().toSpell());
		return SpellTargetEvaluator::getViableTargets(warpSpell().toSpell()->battleMechanics(&cast).get());
	}

	std::shared_ptr<HypotheticBattle> exchange(CStack * first, CStack * second)
	{
		auto projected = std::make_shared<HypotheticBattle>(environment.get(), callback->getBattle(BattleID(0)));
		const auto * one = projected->battleGetUnitByID(first->unitId());
		const auto * two = projected->battleGetUnitByID(second->unitId());
		spells::BattleCast cast(projected.get(), attackerSideHero, spells::Mode::HERO, warpSpell().toSpell());
		auto mechanics = warpSpell().toSpell()->battleMechanics(&cast);
		spells::detail::ProblemImpl problem;
		const spells::Target target{spells::Destination(one), spells::Destination(two)};
		EXPECT_TRUE(mechanics->canBeCastAt(target, problem));
		mechanics->castEval(projected->getServerCallback(), target);
		return projected;
	}
};

TEST_F(NewHorizonsRealityWarpAITest, CompletePairsUseProductionLegalityAndPreserveIDs)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	bless(enemy);
	const auto targets = candidates();
	ASSERT_FALSE(targets.empty());
	std::set<std::pair<uint32_t, uint32_t>> ids;
	spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, warpSpell().toSpell());
	const auto mechanics = warpSpell().toSpell()->battleMechanics(&cast);
	for(const auto & target : targets)
	{
		ASSERT_EQ(target.size(), 2u);
		ASSERT_NE(target[0].unitValue, nullptr);
		ASSERT_NE(target[1].unitValue, nullptr);
		EXPECT_EQ(battle()->battleGetOwner(target[0].unitValue), PlayerColor(0));
		EXPECT_NE(battle()->battleGetOwner(target[1].unitValue), PlayerColor(0));
		EXPECT_NE(target[0].unitValue->unitId(), target[1].unitValue->unitId());
		EXPECT_TRUE(ids.emplace(target[0].unitValue->unitId(), target[1].unitValue->unitId()).second);
		spells::detail::ProblemImpl problem;
		EXPECT_TRUE(mechanics->canBeCastAt(target, problem));
	}
	// Allegiance follows the current controller, not the saved deployment side.
	enemy->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::HYPNOTIZED, BonusSource::OTHER, 1, BonusSourceID()));
	ASSERT_EQ(battle()->battleGetOwner(enemy), PlayerColor(0));
	EXPECT_TRUE(candidates().empty());
}

TEST_F(NewHorizonsRealityWarpAITest, EmptyAndExcludedEffectsNeverOfferNoMoveCandidates)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	EXPECT_TRUE(candidates().empty());
	Bonus intrinsic(BonusDuration::ONE_BATTLE, BonusType::PRIMARY_SKILL,
		BonusSource::OTHER, 50, BonusSourceID(), BonusSubtypeID(PrimarySkill::ATTACK));
	enemy->addNewBonus(std::make_shared<Bonus>(intrinsic));
	EXPECT_TRUE(candidates().empty());
	bless(enemy);
	ASSERT_FALSE(candidates().empty());
	friendly->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::SPELL_IMMUNITY,
		BonusSource::OTHER, 1, BonusSourceID(), BonusSubtypeID(warpSpell())));
	const auto targets = candidates();
	EXPECT_TRUE(std::none_of(targets.begin(), targets.end(), [&](const auto & pair)
		{ return pair[0].unitValue == friendly || pair[1].unitValue == friendly; }));
}

TEST_F(NewHorizonsRealityWarpAITest, SavedRosterExclusionIsNotBroadenedByInstalledSpell)
{
	includeWarp = false;
	ASSERT_NO_FATAL_FAILURE(prepare());
	bless(enemy);
	EXPECT_TRUE(candidates().empty());
}

TEST_F(NewHorizonsRealityWarpAITest, ActiveRealityBreakerAllowsUsefulSameControllerPairs)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true));
	bless(friendly);
	const auto targets = candidates();
	EXPECT_TRUE(std::any_of(targets.begin(), targets.end(), [&](const auto & pair)
	{
		return (pair[0].unitValue == active && pair[1].unitValue == friendly)
			|| (pair[0].unitValue == friendly && pair[1].unitValue == active);
	}));
}

TEST_F(NewHorizonsRealityWarpAITest, AtomicDetachedExchangeValuesEffectsWithoutHPDeltaOrLiveMutation)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	bless(enemy);
	const auto liveEnemy = enemy->save();
	const auto liveFriendly = friendly->save();
	const auto projected = exchange(friendly, enemy);
	EXPECT_TRUE(projected->battleGetUnitByID(friendly->unitId())->hasBonusOfType(BonusType::ALWAYS_MAXIMUM_DAMAGE));
	EXPECT_FALSE(projected->battleGetUnitByID(enemy->unitId())->hasBonusOfType(BonusType::ALWAYS_MAXIMUM_DAMAGE));
	EXPECT_EQ(projected->battleGetUnitByID(friendly->unitId())->getAvailableHealth(), friendly->getAvailableHealth());
	EXPECT_EQ(projected->battleGetUnitByID(enemy->unitId())->getAvailableHealth(), enemy->getAvailableHealth());
	const auto before = callback->getBattle(BattleID(0));
	const spells::Target selection{spells::Destination(friendly), spells::Destination(enemy)};
	const float value = SpellTargetEvaluator::realityWarpExchangeValue(environment.get(), before, projected,
		PlayerColor(0), selection, PlayerColor(0));
	EXPECT_GT(value, 0.0f);
	EXPECT_LT(SpellTargetEvaluator::realityWarpExchangeValue(environment.get(), projected, before,
		PlayerColor(0), selection, PlayerColor(0)), 0.0f);
	EXPECT_FLOAT_EQ(SpellTargetEvaluator::realityWarpExchangeValue(environment.get(), before, before,
		PlayerColor(0), selection, PlayerColor(0)), 0.0f);
	EXPECT_EQ(enemy->save(), liveEnemy);
	EXPECT_EQ(friendly->save(), liveFriendly);
	enemy->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::MAGIC_RESISTANCE,
		BonusSource::OTHER, 100, BonusSourceID()));
	const int actualResistance = enemy->magicResistance();
	ASSERT_GT(actualResistance, 0);
	ASSERT_LE(actualResistance, 75);
	const auto resisted = exchange(friendly, enemy);
	EXPECT_NEAR(SpellTargetEvaluator::realityWarpExchangeValue(environment.get(), before, resisted,
		PlayerColor(0), selection, PlayerColor(0)), value * (1.0f - actualResistance / 100.0f), 0.01f);
}

TEST_F(NewHorizonsRealityWarpAITest, ActualPaidAIUsesCompletePairWithLegacyCommandsDisabled)
{
	// Isolate the paid-spell consumer, using the established fixture's legacy
	// no-Order setting. Do not claim Warp must beat an unrestricted Order choice.
	useCommands = false;
	ASSERT_NO_FATAL_FAILURE(prepare());
	bless(enemy);
	const auto mana = attackerSideHero->getManaAvailable();
	const auto enemyBefore = enemy->save();
	const auto friendlyBefore = friendly->save();
	auto * rng = dynamic_cast<CRandomGenerator *>(&gameHandler->getRandomGenerator());
	ASSERT_NE(rng, nullptr);
	RandomStateArchive randomBefore;
	rng->serialize(randomBefore);
	BattleEvaluator evaluator(environment, callback, active, PlayerColor(0), BattleID(0), BattleSide::ATTACKER, 1.0f, 2);
	evaluator.selectStackAction(active);
	ASSERT_TRUE(evaluator.attemptCastingSpell(active));
	ASSERT_EQ(callback->submitted.size(), 1u);
	const auto action = callback->submitted.front();
	ASSERT_EQ(action.actionType, EActionType::HERO_SPELL) << "command=" << static_cast<int>(action.command);
	ASSERT_EQ(action.spell, warpSpell());
	const auto targets = action.getTarget(battle());
	ASSERT_EQ(targets.size(), 2u);
	ASSERT_NE(targets[0].unitValue, nullptr);
	ASSERT_NE(targets[1].unitValue, nullptr);
	EXPECT_EQ(enemy->save(), enemyBefore);
	EXPECT_EQ(friendly->save(), friendlyBefore);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana);
	RandomStateArchive randomAfter;
	rng->serialize(randomAfter);
	EXPECT_EQ(randomAfter.state, randomBefore.state);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_LT(attackerSideHero->getManaAvailable(), mana);
	EXPECT_FALSE(enemy->hasBonusOfType(BonusType::ALWAYS_MAXIMUM_DAMAGE));
	EXPECT_TRUE(targets[0].unitValue->hasBonusOfType(BonusType::ALWAYS_MAXIMUM_DAMAGE)
		|| targets[1].unitValue->hasBonusOfType(BonusType::ALWAYS_MAXIMUM_DAMAGE));
}
