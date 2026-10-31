/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include "../../../lib/battle/NewHorizonsCreatureBurn.h"
#include "../../../lib/battle/NewHorizonsReactiveDefense.h"
#include "../../../lib/battle/NewHorizonsCoordinatedMarks.h"
#include "../../../lib/bonuses/BonusParameters.h"
#include "../../../lib/entities/hero/NewHorizonsOrdinaryCreatureSpecialties.h"
#include <tuple>
namespace
{
namespace burn = newHorizonsCreatureBurn;
class BurnProducerServer : public RecordingGameServer
{
public:
	uint32_t source = 0, recipient = 0;
	int64_t actualBodyLoss = -1;
	uint32_t primaryFlags = 0;
	bool observeTick = false;
	int tickCount = 0;
	bool markerAtTick = true;
	uint32_t tickSource = 0;
	int64_t tickDamage = 0;
	void applyPack(CPackForClient & pack) override
	{
		const auto * strike = dynamic_cast<const BattleAttack *>(&pack);
		const bool sourceStrike = strike && strike->stackAttacking == source
			&& std::ranges::any_of(strike->bsa, [&](const auto & hit) { return hit.stackAttacked == recipient; });
		auto * before = gameState ? gameState->getBattle(BattleID(0)) : nullptr;
		const auto * target = before ? before->getStack(recipient, false) : nullptr;
		const auto health = sourceStrike && target ? target->health.getCreatureHealthAvailable() : 0;
		if(observeTick)
			if(const auto * injury = dynamic_cast<const StacksInjured *>(&pack))
				for(const auto & hit : injury->stacks)
					if(hit.stackAttacked == recipient)
					{
						++tickCount; markerAtTick = target && target->hasBonus(CSelector(burn::isBurn));
						tickSource = hit.attackerID; tickDamage += hit.damageAmount;
					}
		RecordingGameServer::applyPack(pack);
		if(sourceStrike)
		{
			const auto * after = gameState->getBattle(BattleID(0))->getStack(recipient, false);
			actualBodyLoss = health - after->health.getCreatureHealthAvailable();
			primaryFlags = strike->flags;
		}
	}
};
class BurnProducerFixture : public HeroCommandFixture
{
protected:
	int mode = 0;
	CStack * source = nullptr;
	CStack * recipient = nullptr;
	CStack * ally = nullptr;
	BurnProducerServer observed;
	void TearDown() override { gameHandler.reset(); observed.gameState.reset(); HeroCommandFixture::TearDown(); }
	void mapLoaded(CMap * map) override
	{
		useRevisedOrders = true; HeroCommandFixture::mapLoaded(map);
		JsonNode rules(JsonPath::builtin("config/newHorizonsHeroes"));
		auto & policy = rules["additionalSpecialties"]["ordinaryCreatureSpecialties"];
		for(const char * key : {"cinderReprisalDamagePercent", "emberbrandDamagePercent"})
			if(mode == 1) policy.Struct().erase(key);
			else policy[key].Integer() = mode == 2 ? 0 : 20;
		policy["ironHideReductionPercent"].Integer() = 30;
		rules.setOverrideFlag(true); map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, rules);
	}
	void setup(bool counter, bool upgraded, int captureMode = 0, bool alkinTarget = false)
	{
		mode = captureMode;
		const char * creature = counter ? (upgraded ? "core:efreetSultan" : "core:efreet")
			: (upgraded ? "core:energyElemental" : "core:fireElemental");
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).playerActive(PlayerColor(0)).playerActive(PlayerColor(1))
			.hero({5, 5, 0}, HeroTypeID(HeroTypeID::decode(counter ? "core:rashka" : "core:ignissa")), PlayerColor(0)).heroExperience(0)
			.heroGarrison({{creatureByName(creature), 100}, {creatureByName("core:pikeman"), 1}})
			.hero({7, 7, 0}, HeroTypeID(HeroTypeID::decode(alkinTarget ? "core:alkin" : "core:orrin")), PlayerColor(1)).heroExperience(0)
			.heroGarrison({{creatureByName(alkinTarget ? "core:gorgon" : "core:pikeman"), 1000}});
		startWithMap(std::move(builder)); server.gameState = gameState(); observed.gameState = gameState();
		gameHandler = std::make_shared<CGameHandler>(observed, gameState());
		attackerSideHero = findHeroByOwner(PlayerColor(0)); defenderSideHero = findHeroByOwner(PlayerColor(1));
		for(auto * hero : {attackerSideHero, defenderSideHero})
		{
			const auto skills = hero->secSkills;
			for(const auto & [skill, rank] : skills) hero->setSecSkillLevel(skill, 0, ChangeValueMode::ABSOLUTE);
			for(const auto skill : {PrimarySkill::ATTACK, PrimarySkill::DEFENSE, PrimarySkill::SPELL_POWER, PrimarySkill::KNOWLEDGE})
				hero->setPrimarySkill(skill, 0, ChangeValueMode::ABSOLUTE);
		}
		startBattle();
		for(const auto & stack : battle()->stacks)
			if(stack->unitSide() == BattleSide::DEFENDER) recipient = stack.get();
			else if(stack->unitSlot() == SlotID(0)) source = stack.get(); else ally = stack.get();
		ASSERT_NE(source, nullptr); ASSERT_NE(recipient, nullptr); ASSERT_NE(ally, nullptr);
		source->setPosition(BattleHex(3, 5)); recipient->setPosition(BattleHex(4, 5)); ally->setPosition(BattleHex(1, 2));
		for(auto * stack : {source, recipient, ally})
		{
			for(const auto type : {BonusType::NO_MORALE, BonusType::NO_LUCK})
				stack->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, type, BonusSource::OTHER, 1, BonusSourceID()));
			for(const auto bound : {BonusValueType::INDEPENDENT_MIN, BonusValueType::INDEPENDENT_MAX})
			{
				stack->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::STACKS_INITIATIVE_BASE,
					BonusSource::OTHER, stack == source ? 1000 : stack == ally ? 900 : counter ? 1500 : 500, BonusSourceID(), BonusSubtypeID(), bound));
				for(const auto subtype : {BonusCustomSubtype::creatureDamageMin, BonusCustomSubtype::creatureDamageMax})
					stack->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::CREATURE_DAMAGE,
						BonusSource::OTHER, 2, BonusSourceID(), subtype, bound));
			}
		}
		recipient->health.addTemporaryHitPoints(7); recipient->guardianSpiritHitPoints = 5; recipient->guardianSpiritRoundsRemaining = 3;
		observed.source = source->unitId(); observed.recipient = recipient->unitId();
		ASSERT_TRUE(battle()->battleUsesNewHorizonsMultiplicativeMDR());
	}
	void strike(bool counter)
	{
		beginCombat();
		const auto * attacker = counter ? recipient : source;
		const auto * defender = counter ? source : recipient;
		ASSERT_EQ(battle()->battleActiveUnit(), attacker);
		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), counter ? PlayerColor(1) : PlayerColor(0),
			BattleAction::makeMeleeAttack(attacker, defender, attacker->getPosition())));
	}
};
class BurnProducerCases : public BurnProducerFixture, public ::testing::WithParamInterface<std::tuple<bool, bool, int>> {};
TEST_P(BurnProducerCases, ActualPrimaryOrCounterUsesBodyLedgerAndTicksOnceAtRealActivation)
{
	const auto [counter, upgraded, capture] = GetParam();
	ASSERT_NO_FATAL_FAILURE(setup(counter, upgraded, capture));
	ASSERT_NO_FATAL_FAILURE(strike(counter));
	ASSERT_GT(observed.actualBodyLoss, 0);
	EXPECT_EQ((observed.primaryFlags & BattleAttack::COUNTER) != 0, counter);
	const auto pending = recipient->getUnstackedBonuses(CSelector(burn::isBurn));
	ASSERT_EQ(pending->size(), capture == 0 ? 1u : 0u);
	if(capture == 0)
	{
		EXPECT_EQ(pending->front()->val, std::max<int64_t>(1, observed.actualBodyLoss / 5));
		EXPECT_EQ(pending->front()->parameters->toNumber(), source->unitId());
		EXPECT_EQ(pending->front()->sid, BonusSourceID(HeroTypeID(HeroTypeID::decode(counter ? "core:rashka" : "core:ignissa"))));
	}
	const auto before = recipient->getAvailableHealth(); observed.observeTick = true;
	for(int step = 0; step < 6 && battle()->battleActiveUnit() != recipient; ++step)
	{
		const auto * active = battle()->battleActiveUnit(); ASSERT_NE(active, nullptr);
		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), battle()->battleGetOwner(active), BattleAction::makeDefend(active)));
	}
	ASSERT_EQ(battle()->battleActiveUnit(), recipient);
	EXPECT_FALSE(recipient->hasBonus(CSelector(burn::isBurn)));
	EXPECT_EQ(observed.tickCount, capture == 0 ? 1 : 0);
	if(capture == 0) { EXPECT_FALSE(observed.markerAtTick); EXPECT_EQ(observed.tickSource, source->unitId()); EXPECT_GT(observed.tickDamage, 0); }
	EXPECT_EQ(before - recipient->getAvailableHealth(), observed.tickDamage);
}
INSTANTIATE_TEST_SUITE_P(CounterPrimaryBothFormsCurrentHistoricalZero, BurnProducerCases,
	::testing::Combine(::testing::Bool(), ::testing::Bool(), ::testing::Values(0, 1, 2)));
class BurnProducerCompositionTest : public BurnProducerFixture {};
TEST_F(BurnProducerCompositionTest, SmallerNativeReapplicationKeepsPriorStrongerCapturedSourceAndBudget)
{
	ASSERT_NO_FATAL_FAILURE(setup(false, false));
	auto old = burn::marker(HeroTypeID(HeroTypeID::decode("core:rashka")), ally->unitId(), 500);
	recipient->addNewBonus(std::make_shared<Bonus>(old));
	ASSERT_NO_FATAL_FAILURE(strike(false));
	const auto result = recipient->getUnstackedBonuses(CSelector(burn::isBurn)); ASSERT_EQ(result->size(), 1u);
	EXPECT_EQ(result->front()->toJsonNode(), old.toJsonNode());
}
TEST_F(BurnProducerCompositionTest, BurnCreationPreservesAlkinSpentAndConsumesExistingAlliedMark)
{
	ASSERT_NO_FATAL_FAILURE(setup(false, false, 0, true));
	Bonus mark(BonusDuration::N_TURNS, BonusType::NONE, BonusSource::HERO_SPECIAL, 25,
		BonusSourceID(HeroTypeID(HeroTypeID::decode("core:ingham"))), BonusSubtypeID(), BonusValueType::BASE_NUMBER);
	mark.turnsRemain = 1; mark.stacking = "new-horizons:ordinary-creature-specialty:core:ingham:consecratedTarget";
	mark.statusIdentity = "consecratedTarget"; mark.statusTags = {BonusStatusTag::DEBUFF, BonusStatusTag::NON_TRANSFERABLE};
	mark.parameters = std::make_shared<BonusParameters>(std::vector<int32_t>{0, static_cast<int32_t>(source->unitId())});
	recipient->addNewBonus(std::make_shared<Bonus>(mark));
	ASSERT_NO_FATAL_FAILURE(strike(false));
	EXPECT_TRUE(recipient->hasBonus(CSelector(burn::isBurn)));
	EXPECT_TRUE(recipient->hasBonus(CSelector(newHorizonsReactiveDefense::isIronHideSpent)));
	EXPECT_FALSE(recipient->hasBonus(CSelector(newHorizonsCoordinatedMarks::isMark)));
}
}
