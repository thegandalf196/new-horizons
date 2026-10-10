/*
 * NewHorizonsElementalCounterfireTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "BattleTestFixture.h"
#include "../../../lib/CSkillHandler.h"
#include "../../../lib/battle/NewHorizonsArchery.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/spells/CSpellHandler.h"
#include "../../../lib/spells/ISpellMechanics.h"
#include "../../../server/CGameHandler.h"
#include "../../../server/battles/BattleProcessor.h"

#ifdef ENABLE_BATTLE_AI
#include "../../../AI/BattleAI/AttackPossibility.h"
#include "../../../AI/BattleAI/StackWithBonuses.h"
#include "../../../include/vcmi/Environment.h"
#endif

namespace
{
class NewHorizonsElementalCounterfireTest : public BattleTestFixture
{
protected:
	CStack * shooter = nullptr;
	CStack * responder = nullptr;
	bool absentCapturedPolicy = false;
	void SetUp() override
	{
		BattleTestFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires curated New Horizons preset";
	}
	void mapLoaded(CMap * map) override
	{
		BattleTestFixture::mapLoaded(map);
		// This fixture grants Counterfire through normal selection after loading.
		// Default Valeska now authors Point Blank Shot in that same Basic tier;
		// opt only this fixture hero out of default development before initialization.
		JsonNode heroRules(JsonPath::builtin("config/newHorizonsHeroes"));
		heroRules["startingSkills"]["startingDevelopmentProfiles"].Struct().erase("core:valeska");
		if(absentCapturedPolicy)
		{
			map->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, JsonNode());
			heroRules["startingSkills"].Struct().erase("startingBookReplacements");
		}
		heroRules.setOverrideFlag(true);
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, std::move(heroRules));
	}
	void prepare(const char * creature = "core:magog", bool absentPolicy = false)
	{
		absentCapturedPolicy = absentPolicy;
		startGame();
		const int archery = SecondarySkill::decode(std::string(newHorizonsArchery::SKILL));
		ASSERT_GE(archery, 0);
		defenderSideHero->setSecSkillLevel(SecondarySkill(archery), MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		defenderSideHero->applyPerkSelection({std::string(newHorizonsArchery::SKILL), std::string(newHorizonsArchery::COUNTERFIRE)});
		ASSERT_TRUE(defenderSideHero->hasActivePerk(std::string(newHorizonsArchery::SKILL), std::string(newHorizonsArchery::COUNTERFIRE)));
		startBattle();
		if(absentPolicy)
			ASSERT_TRUE(battle()->getMagicRules().isNull());
		else
			ASSERT_EQ(battle()->getMagicRules()["rulesetVersion"].Integer(), 3);
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
		shooter = addStack(BattleSide::ATTACKER, creatureByName(creature), BattleHex(3, 5), 1000);
		responder = addStack(BattleSide::DEFENDER, creatureByName("core:titan"), BattleHex(11, 5), 100);
		forceMaximumDamage(shooter);
		forceMaximumDamage(responder);
		beginCombat();
		battle()->activeStack = shooter->unitId();
		ASSERT_TRUE(battle()->battleCanShoot(shooter, responder->getPosition()));
		ASSERT_TRUE(battle()->battleCanShoot(responder, shooter->getPosition()));
	}
	void shoot()
	{
		server.attacks.clear();
		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
			BattleAction::makeShotAttack(shooter, responder)));
	}
	void expectResponse()
	{
		const auto ammo = responder->shots.available();
		const auto health = shooter->getAvailableHealth();
		const auto ordinaryRetaliations = responder->counterAttacks.available();
		ASSERT_NO_FATAL_FAILURE(shoot());
		ASSERT_EQ(server.attacks.size(), 2u);
		EXPECT_TRUE(server.attacks[0].spellLike());
		EXPECT_EQ(server.attacks[1].stackAttacking, responder->unitId());
		EXPECT_TRUE(server.attacks[1].counter());
		EXPECT_EQ(responder->shots.available(), ammo - 1);
		EXPECT_EQ(responder->counterAttacks.available(), ordinaryRetaliations);
		EXPECT_EQ(responder->archeryCounterfireRound, battle()->battleGetRound());
		EXPECT_LT(shooter->getAvailableHealth(), health);
		ASSERT_TRUE(shooter->alive());
		ASSERT_EQ(server.attacks[1].bsa.size(), 1u);
		BattleAttackInfo forecast(responder, shooter, 0, true);
		forecast.archeryRangedDamageMultiplierPercent = newHorizonsArchery::COUNTERFIRE_DAMAGE_PERCENT;
		EXPECT_EQ(server.attacks[1].bsa.front().damageAmount, battle()->calculateDmgRange(forecast).damage.max);
	}
};
}

TEST_F(NewHorizonsElementalCounterfireTest, OrdinaryMagogPrimaryShotProvokesLegalHalfDamageResponse)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(expectResponse());
}

TEST_F(NewHorizonsElementalCounterfireTest, OrdinaryLichPrimaryShotProvokesResponseOnlyOncePerRound)
{
	ASSERT_NO_FATAL_FAILURE(prepare("core:lich"));
	ASSERT_NO_FATAL_FAILURE(expectResponse());
	auto * second = addStack(BattleSide::ATTACKER, creatureByName("core:lich"), BattleHex(3, 7), 1000);
	forceMaximumDamage(second);
	const auto ammo = responder->shots.available();
	battle()->activeStack = second->unitId();
	server.attacks.clear();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), BattleAction::makeShotAttack(second, responder)));
	ASSERT_EQ(server.attacks.size(), 1u);
	EXPECT_EQ(responder->shots.available(), ammo);
}

TEST_F(NewHorizonsElementalCounterfireTest, MagicalCollateralDoesNotProvokeAdditionalCounterfire)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto * collateral = addStack(BattleSide::DEFENDER, creatureByName("core:titan"), BattleHex(11, 6), 100);
	ASSERT_EQ(BattleHex::getDistance(collateral->getPosition(), responder->getPosition()), 1);
	ASSERT_TRUE(battle()->battleCanShoot(collateral, shooter->getPosition()));
	const auto health = collateral->getAvailableHealth();
	const auto ammo = collateral->shots.available();
	ASSERT_NO_FATAL_FAILURE(shoot());
	EXPECT_LT(collateral->getAvailableHealth(), health);
	EXPECT_EQ(collateral->shots.available(), ammo);
	EXPECT_EQ(collateral->archeryCounterfireRound, -1);
	ASSERT_EQ(server.attacks.size(), 2u);
	EXPECT_EQ(server.attacks[1].stackAttacking, responder->unitId());
}

TEST_F(NewHorizonsElementalCounterfireTest, ActualCreatureActiveDamageCastDoesNotProvokeCounterfire)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	shooter->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::SPELLCASTER,
		BonusSource::CREATURE_ABILITY, 1, BonusSourceID(), BonusSubtypeID(SpellID(SpellID::MAGIC_ARROW))));
	shooter->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::CASTS,
		BonusSource::CREATURE_ABILITY, 1, BonusSourceID()));
	ASSERT_TRUE(shooter->canCast());
	const auto health = responder->getAvailableHealth();
	const auto ammo = responder->shots.available();
	server.attacks.clear();
	const spells::Target target{spells::Destination(responder)};
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeCreatureSpellcast(shooter, target, SpellID::MAGIC_ARROW)));
	EXPECT_LT(responder->getAvailableHealth(), health);
	EXPECT_TRUE(server.attacks.empty());
	EXPECT_EQ(responder->shots.available(), ammo);
	EXPECT_EQ(responder->archeryCounterfireRound, -1);
}

TEST_F(NewHorizonsElementalCounterfireTest, AbsentCapturedPolicyRetainsHistoricalElementalExclusion)
{
	ASSERT_NO_FATAL_FAILURE(prepare("core:magog", true));
	const auto ammo = responder->shots.available();
	ASSERT_NO_FATAL_FAILURE(shoot());
	ASSERT_EQ(server.attacks.size(), 1u);
	EXPECT_EQ(responder->shots.available(), ammo);
	EXPECT_EQ(responder->archeryCounterfireRound, -1);
}

TEST_F(NewHorizonsElementalCounterfireTest, IllegalOutOfRangeResponseDoesNotConsumeAmmoOrRoundUse)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	responder->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::LIMITED_SHOOTING_RANGE, BonusSource::OTHER, 2, BonusSourceID()));
	ASSERT_FALSE(battle()->battleCanShoot(responder, shooter->getPosition()));
	const auto ammo = responder->shots.available();
	ASSERT_NO_FATAL_FAILURE(shoot());
	ASSERT_EQ(server.attacks.size(), 1u);
	EXPECT_EQ(responder->shots.available(), ammo);
	EXPECT_EQ(responder->archeryCounterfireRound, -1);
}

TEST_F(NewHorizonsElementalCounterfireTest, EmptyAmmoResponseDoesNotSpendRoundUse)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto state = responder->acquireState();
	ASSERT_GT(state->shots.available(), 0);
	state->shots.use(state->shots.available());
	BattleUnitsChanged exhausted;
	exhausted.battleID = BattleID(0);
	UnitChanges update(responder->unitId(), UnitChanges::EOperation::UPDATE);
	update.data = state->save();
	exhausted.changedStacks.push_back(std::move(update));
	gameHandler->sendAndApply(exhausted);
	ASSERT_EQ(responder->shots.available(), 0);
	ASSERT_NO_FATAL_FAILURE(shoot());
	ASSERT_EQ(server.attacks.size(), 1u);
	EXPECT_EQ(responder->shots.available(), 0);
	EXPECT_EQ(responder->archeryCounterfireRound, -1);
}

#ifdef ENABLE_BATTLE_AI
namespace
{
class CounterfireEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit CounterfireEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};
}

TEST_F(NewHorizonsElementalCounterfireTest, DetachedOrdinaryElementalForecastIncludesLegalResponseWithoutLiveMutation)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	CounterfireEnvironment environment(gameState());
	const auto callback = std::shared_ptr<CBattleInfoCallback>(battle(), [](CBattleInfoCallback *) {});
	auto model = std::make_shared<HypotheticBattle>(&environment, callback);
	auto source = model->getForUpdate(shooter->unitId());
	auto target = model->getForUpdate(responder->unitId());
	ASSERT_NE(static_cast<const battle::Unit *>(source.get()), static_cast<const battle::Unit *>(shooter));
	ASSERT_NE(static_cast<const battle::Unit *>(target.get()), static_cast<const battle::Unit *>(responder));
	const auto liveHealth = shooter->getAvailableHealth();
	const auto liveAmmo = responder->shots.available();
	DamageCache cache;
	cache.buildDamageCache(model, BattleSide::ATTACKER);
	BattleAttackInfo shot(source.get(), target.get(), 0, true);
	ASSERT_FALSE(shot.physicalDamage);
	const auto projected = AttackPossibility::evaluate(shot, shooter->getPosition(), cache, model);
	ASSERT_NE(projected.attackerState, nullptr);
	EXPECT_GT(projected.attackerDamageReduce, 0.f);
	EXPECT_LT(projected.attackerState->getAvailableHealth(), liveHealth);
	const auto responseState = std::ranges::find_if(projected.affectedUnits, [this](const auto & unit)
		{ return unit->unitId() == responder->unitId(); });
	ASSERT_NE(responseState, projected.affectedUnits.end());
	EXPECT_EQ((*responseState)->shots.available(), liveAmmo - 1);
	EXPECT_EQ((*responseState)->archeryCounterfireRound, battle()->battleGetRound());
	EXPECT_EQ(target->shots.available(), liveAmmo);
	EXPECT_EQ(target->archeryCounterfireRound, -1);
	auto nextExchange = std::make_shared<HypotheticBattle>(&environment, model);
	nextExchange->updateUnit(shooter->unitId(), projected.attackerState->save(), 0);
	nextExchange->updateUnit(responder->unitId(), (*responseState)->save(), 0);
	DamageCache nextCache;
	nextCache.buildDamageCache(nextExchange, BattleSide::ATTACKER);
	BattleAttackInfo repeatedShot(nextExchange->battleGetUnitByID(shooter->unitId()),
		nextExchange->battleGetUnitByID(responder->unitId()), 0, true);
	const auto repeated = AttackPossibility::evaluate(repeatedShot, shooter->getPosition(), nextCache, nextExchange);
	EXPECT_FLOAT_EQ(repeated.attackerDamageReduce, 0.f);
	EXPECT_EQ(nextExchange->getForUpdate(responder->unitId())->shots.available(), liveAmmo - 1);
	EXPECT_EQ(shooter->getAvailableHealth(), liveHealth);
	EXPECT_EQ(responder->shots.available(), liveAmmo);
	EXPECT_EQ(responder->archeryCounterfireRound, -1);
	ASSERT_NO_FATAL_FAILURE(shoot());
	EXPECT_EQ(responder->shots.available(), liveAmmo - 1);
	EXPECT_EQ(responder->archeryCounterfireRound, battle()->battleGetRound());
	EXPECT_EQ(source->getAvailableHealth(), liveHealth);
}
#endif
