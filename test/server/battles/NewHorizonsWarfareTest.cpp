/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include "../../../lib/battle/NewHorizonsWarfare.h"
#include "../../../lib/battle/NewHorizonsCommandResources.h"
#include "../../../lib/battle/NewHorizonsDivineMandate.h"
#include "../../../lib/battle/BattleAttackInfo.h"
#include "../../../lib/battle/BattleLayout.h"
#include "../../../lib/battle/BattleProxy.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/combatScripts/IDamageCalculatorScript.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/ISpellMechanics.h"
#include "../../../lib/spells/effects/Effects.h"

namespace
{
constexpr auto skill = "new-horizons:command";

class NewHorizonsWarfareTest : public HeroCommandFixture
{
protected:
	NewHorizonsWarfareTest() { useRevisedOrders = true; }
	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		const JsonNode combat(JsonPath::builtin("config/newHorizonsCombat"));
		auto rules = combat["combat"]["heroCommands"];
		rules.Struct().erase("commandResourcePolicyVersion");
		rules["warfarePolicyVersion"].Integer() = 1;
		heroCommands::validateRules(rules);
		loaded->overrideGameSetting(EGameSettings::COMBAT_HERO_COMMANDS, rules);
		auto perks = JsonNode(JsonPath::builtin("config/newHorizonsPerks"));
		perks["warfarePolicyVersion"].Integer() = 1;
		for(auto & perk : perks["skills"][skill]["perks"].Vector())
			perk["effect"]["status"].String() = "active";
		newHorizonsWarfare::validatePolicy(perks);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, perks);
	}
	void select(const char * id, int rank)
	{
		attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode(skill)), rank, ChangeValueMode::ABSOLUTE);
		attackerSideHero->applyPerkSelection({skill, id});
		ASSERT_TRUE(attackerSideHero->hasActivePerk(skill, id));
	}
	void startWith(const char * advanced = nullptr, const char * expert = nullptr,
		const char * basic = "new-horizons:command.battlePlan")
	{
		startGame();
		select(basic, 1);
		if(advanced) select(advanced, 2);
		if(expert) select(expert, 3);
		startBattle();
		beginCombat();
	}
};
}

TEST_F(NewHorizonsWarfareTest, OrdersUseOnlyOrdinaryActionAcrossMoreThanFormerMaximumRounds)
{
	ASSERT_NO_FATAL_FAILURE(startWith());
	EXPECT_FALSE(battle()->getCommandResources(BattleSide::ATTACKER).initialized);
	const auto mana = attackerSideHero->getManaAvailable();
	for(int round = 0; round < 12; ++round)
	{
		SCOPED_TRACE(round);
		ASSERT_TRUE(issue(HeroCommand::CHARGE));
		EXPECT_FALSE(issue(HeroCommand::HOLD_THE_LINE));
		EXPECT_EQ(attackerSideHero->getManaAvailable(), mana);
		EXPECT_EQ(battle()->getCommandResources(BattleSide::ATTACKER), newHorizonsCommand::ResourceState{});
		advanceRound();
	}
	EXPECT_FALSE(heroCommands::hasBattlePlan(attackerSideHero));
	EXPECT_FALSE(heroCommands::hasDoubleCommand(attackerSideHero));
	EXPECT_FALSE(battle()->getPreCombatOrderState(BattleSide::ATTACKER).orderPending());
}

TEST_F(NewHorizonsWarfareTest, BattlePlanIsCapturedOnlyOnFirstAcceptedIssue)
{
	ASSERT_NO_FATAL_FAILURE(startWith());
	const auto before = battle()->getHeroActionAllowances(BattleSide::ATTACKER);
	for(int preview = 0; preview < 3; ++preview)
	{
		const auto state = battle()->battlePrepareHeroOrderState(BattleSide::ATTACKER, HeroCommand::CHARGE, {});
		ASSERT_TRUE(state);
		EXPECT_EQ(state->battlePlanEfficiencyBonusPercent, 20);
	}
	auto invalid = BattleAction::makeHeroCommand(BattleSide::ATTACKER, HeroCommand::CHARGE);
	invalid.aimToUnit(battle()->battleActiveUnit());
	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), invalid));
	EXPECT_EQ(battle()->getHeroActionAllowances(BattleSide::ATTACKER), before);
	ASSERT_TRUE(issue(HeroCommand::CHARGE));
	EXPECT_TRUE(battle()->getHeroActionAllowances(BattleSide::ATTACKER).orderIssuedThisCombat);
	EXPECT_EQ(battle()->getHeroOrderState(BattleSide::ATTACKER, HeroCommand::CHARGE)->battlePlanEfficiencyBonusPercent, 20);
	advanceRound();
	ASSERT_TRUE(issue(HeroCommand::CHARGE));
	EXPECT_EQ(battle()->getHeroOrderState(BattleSide::ATTACKER, HeroCommand::CHARGE)->battlePlanEfficiencyBonusPercent, 0);
}

TEST_F(NewHorizonsWarfareTest, MasterAndBattlePlanScaleAttributesWithoutChangingFixedBase)
{
	ASSERT_NO_FATAL_FAILURE(startWith("new-horizons:command.veteranCommander", "new-horizons:command.doubleCommand"));
	attackerSideHero->setPrimarySkill(PrimarySkill::ATTACK, 100, ChangeValueMode::ABSOLUTE);
	const auto prepared = battle()->battlePrepareHeroOrderState(BattleSide::ATTACKER, HeroCommand::CHARGE, {});
	ASSERT_TRUE(prepared);
	EXPECT_EQ(heroCommands::efficiencyPercent(*attackerSideHero), 145);
	EXPECT_EQ(heroCommands::leadershipEfficiencyPercent(*attackerSideHero, 20), 165)
		<< "Veteran Commander no longer supplies Leadership-derived efficiency";
	JsonNode formula;
	formula["base"].Integer() = 17;
	formula["attack"].Integer() = 1;
	formula["defense"].Integer() = 0;
	EXPECT_EQ(heroCommands::coefficient(formula, *attackerSideHero, prepared->divineMandateEfficiencyBonusPercent()), 182);
	formula["attack"].Integer() = 0;
	EXPECT_EQ(heroCommands::coefficient(formula, *attackerSideHero, prepared->divineMandateEfficiencyBonusPercent()), 17);
}

TEST_F(NewHorizonsWarfareTest, FocusFireUsesSameBattlePlanAndHeroMoraleSnapshotAsAcceptedOrder)
{
	startGame();
	select("new-horizons:command.battlePlan", 1);
	select("new-horizons:command.combinedArms", 2);
	const SecondarySkill leadership(SecondarySkill::decode("new-horizons:discipline"));
	const std::array<const char *, 3> perks{"new-horizons:discipline.inspirationalLeader",
		"new-horizons:discipline.standardBearer", "new-horizons:discipline.heroicSpirit"};
	for(int rank = 1; rank <= 3; ++rank)
	{
		attackerSideHero->setSecSkillLevel(leadership, rank, ChangeValueMode::ABSOLUTE);
		attackerSideHero->applyPerkSelection({"new-horizons:discipline", perks[rank - 1]});
	}
	attackerSideHero->setPrimarySkill(PrimarySkill::ATTACK, 100, ChangeValueMode::ABSOLUTE);
	startBattle();
	beginCombat();
	attackerSideHero->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::MORALE, BonusSource::OTHER, 4 - attackerSideHero->moraleVal(), BonusSourceID()));
	ASSERT_EQ(attackerSideHero->moraleVal(), 4);
	const auto enemies = battle()->battleGetUnitsIf([this](const battle::Unit * unit)
		{ return unit->alive() && battle()->battleGetOwner(unit) == PlayerColor(1); });
	ASSERT_FALSE(enemies.empty());
	const auto id = enemies.front()->unitId();
	const auto prepared = battle()->battlePrepareHeroOrderState(BattleSide::ATTACKER, HeroCommand::FOCUS_FIRE, {id});
	const auto mark = battle()->battlePrepareFocusFireState(BattleSide::ATTACKER, id);
	ASSERT_TRUE(prepared);
	ASSERT_TRUE(mark);
	EXPECT_EQ(prepared->heroicSpiritEfficiencyBonusPercent, 20);
	EXPECT_EQ(prepared->battlePlanEfficiencyBonusPercent, 20);
	const auto & formula = battle()->getHeroCommandRules()["commands"]["focusFire"]["effects"]["rangedDamagePercent"];
	EXPECT_EQ(mark->rangedDamagePercent, heroCommands::coefficient(formula, *attackerSideHero,
		prepared->divineMandateEfficiencyBonusPercent()));
	const auto action = BattleAction::makeTargetedHeroCommand(BattleSide::ATTACKER, HeroCommand::FOCUS_FIRE, id);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	const auto issued = battle()->getHeroOrderState(BattleSide::ATTACKER, HeroCommand::FOCUS_FIRE);
	ASSERT_TRUE(issued);
	EXPECT_EQ(issued->heroicSpiritEfficiencyBonusPercent, 20);
	EXPECT_FALSE(issued->heroMorale);
	attackerSideHero->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::MORALE, BonusSource::OTHER, -8, BonusSourceID()));
	EXPECT_EQ(battle()->getFocusFireState(BattleSide::ATTACKER)->rangedDamagePercent, mark->rangedDamagePercent);
	EXPECT_EQ(battle()->getHeroOrderState(BattleSide::ATTACKER, HeroCommand::FOCUS_FIRE)->heroicSpiritEfficiencyBonusPercent, 20);
	EXPECT_FALSE(battle()->battleGetOrderActionAllowance(BattleSide::ATTACKER));
}

TEST_F(NewHorizonsWarfareTest, VeteranStatsFollowConsumptionAndDoNotMultiplyAcrossOrders)
{
	ASSERT_NO_FATAL_FAILURE(startWith("new-horizons:command.veteranCommander"));
	const auto * recipient = battle()->battleActiveUnit();
	ASSERT_NE(recipient, nullptr);
	const int attackBefore = recipient->getAttack(false);
	const int defenseBefore = recipient->getDefense(false);
	EXPECT_EQ(battle()->battleWarfareStatBonus(recipient), 0);
	ASSERT_TRUE(issue(HeroCommand::CHARGE));
	EXPECT_EQ(battle()->battleWarfareStatBonus(recipient), 2);
	EXPECT_EQ(recipient->getAttack(false), attackBefore + 2);
	EXPECT_EQ(recipient->getDefense(false), defenseBefore + 2);
	const auto detached = recipient->acquireState();
	EXPECT_EQ(detached->getAttack(false), attackBefore + 2);
	EXPECT_EQ(detached->getDefenseIgnoringDefensiveStance(false), defenseBefore + 2);
	auto charge = *battle()->getHeroOrderState(BattleSide::ATTACKER, HeroCommand::CHARGE);
	auto riposte = charge;
	riposte.command = HeroCommand::RIPOSTE;
	battle()->setHeroOrderState(BattleSide::ATTACKER, riposte);
	EXPECT_EQ(battle()->battleWarfareStatBonus(recipient), 2);
	charge.consumedUnitIds = {recipient->unitId()};
	battle()->setHeroOrderState(BattleSide::ATTACKER, charge);
	EXPECT_EQ(battle()->battleWarfareStatBonus(recipient), 2);
	battle()->setHeroOrderStates(BattleSide::ATTACKER, {charge});
	EXPECT_EQ(battle()->battleWarfareStatBonus(recipient), 0);
	EXPECT_EQ(recipient->getAttack(false), attackBefore);
	EXPECT_EQ(recipient->getDefense(false), defenseBefore);
	EXPECT_EQ(detached->getAttack(false), attackBefore);
	EXPECT_EQ(detached->getDefenseIgnoringDefensiveStance(false), defenseBefore);
}

TEST_F(NewHorizonsWarfareTest, IronWillCarryIsARealBenefitUntilGenuineActivationCompletes)
{
	ASSERT_NO_FATAL_FAILURE(startWith("new-horizons:command.veteranCommander", nullptr, "new-horizons:command.ironWill"));
	const auto * recipient = battle()->battleActiveUnit();
	ASSERT_TRUE(issue(HeroCommand::CHARGE));
	advanceRound();
	EXPECT_EQ(battle()->battleWarfareStatBonus(recipient), 2);
	newHorizonsDivineMandate::completeDisciplineActivation(*battle(), recipient->unitId(), false);
	EXPECT_EQ(battle()->battleWarfareStatBonus(recipient), 2);
	newHorizonsDivineMandate::completeDisciplineActivation(*battle(), recipient->unitId(), true);
	EXPECT_EQ(battle()->battleWarfareStatBonus(recipient), 0);
}

TEST_F(NewHorizonsWarfareTest, DecisiveAndUnbrokenCoverDirectMagicalAttacksAndRetaliationsOnce)
{
	ASSERT_NO_FATAL_FAILURE(startWith("new-horizons:command.crisisCommand", "new-horizons:command.decisiveOrders"));
	const auto * recipient = battle()->battleActiveUnit();
	const auto enemies = battle()->battleGetUnitsIf([this](const battle::Unit * unit)
		{ return unit->alive() && battle()->battleGetOwner(unit) == PlayerColor(1); });
	ASSERT_FALSE(enemies.empty());
	ASSERT_TRUE(issue(HeroCommand::CHARGE));
	for(bool shooting : {false, true})
		for(bool physical : {false, true})
			for(bool retaliation : {false, true})
			{
				BattleAttackInfo attack(recipient, enemies.front(), 0, shooting);
				attack.physicalDamage = physical;
				attack.retaliation = retaliation;
				DamageAttackInfo payload;
				newHorizonsWarfare::appendAttackModifiers(*battle(), attack, payload);
				ASSERT_EQ(payload.heroOrderFinalDamageMultipliers.size(), 1u);
				EXPECT_EQ(payload.heroOrderFinalDamageMultipliers.front(), 110);
			}
	EXPECT_EQ(newHorizonsCommand::mitigateIncomingDamage(*battle(), recipient, 40), 32);
	EXPECT_EQ(newHorizonsCommand::mitigateIncomingDamage(*battle(), recipient, 32), 25);
	auto spent = *battle()->getHeroOrderState(BattleSide::ATTACKER, HeroCommand::CHARGE);
	spent.consumedUnitIds = {recipient->unitId()};
	battle()->setHeroOrderState(BattleSide::ATTACKER, spent);
	BattleAttackInfo attack(recipient, enemies.front(), 0, false);
	EXPECT_EQ(newHorizonsWarfare::directAttackBonus(*battle(), attack), 0);
	EXPECT_EQ(newHorizonsCommand::mitigateIncomingDamage(*battle(), recipient, 40), 40);
}

TEST_F(NewHorizonsWarfareTest, CapturedFormationOrdersHaveNoRetiredLeadershipAdmissionGate)
{
	ASSERT_NO_FATAL_FAILURE(startWith());
	auto * spent = addStack(BattleSide::ATTACKER, CreatureID(CreatureID::decode("core:pikeman")), BattleHex(leftHex), 10);
	ASSERT_NE(spent, nullptr);
	spent->defending = true;
	for(auto command : {HeroCommand::SECOND_WIND, HeroCommand::FEINT, HeroCommand::VANGUARD})
	{
		EXPECT_TRUE(battle()->battleCanBeginHeroCommand(BattleSide::ATTACKER, command));
	}
}

namespace
{
// Current Divine Mandate is passive. Exercise the retained Crown recipient
// consumer with an explicit Order-only snapshot, without inventing a current
// Crown perk/allowance or installing unsupported provenance in the live battle.
class SecondWindOrderSnapshot final : public BattleProxy
{
	HeroOrderState captured;
public:
	SecondWindOrderSnapshot(Subject subject, HeroOrderState order)
		: BattleProxy(std::move(subject)), captured(std::move(order))
	{
		captured.validateShape();
	}
	std::vector<HeroOrderState> getHeroOrderStates(BattleSide side) const override
	{
		return side == BattleSide::ATTACKER ? std::vector<HeroOrderState>{captured}
			: BattleProxy::getHeroOrderStates(side);
	}
	// Follow the read-only proxy contract: delegate reads and reject simulation mutations.
	BattleID getBattleID() const override { return subject->getBattle()->getBattleID(); }
	const scripting::Pool & getScriptContextPool() const override { return subject->getBattle()->getScriptContextPool(); }
	std::vector<SpellID> getUsedSpells(BattleSide side) const override { return subject->getBattle()->getUsedSpells(side); }
	uint32_t nextUnitId() const override { return subject->getBattle()->nextUnitId(); }
	int64_t getActualDamage(const DamageRange & damage, int32_t count, vstd::RNG & rng) const override
	{
		return subject->getBattle()->getActualDamage(damage, count, rng);
	}
	int3 getLocation() const override { return subject->getBattle()->getLocation(); }
	BattleLayout getLayout() const override { return subject->getBattle()->getLayout(); }
	void nextRound() override { refuseMutation(); }
	void nextTurn(uint32_t, BattleUnitTurnReason) override { refuseMutation(); }
	void addUnit(uint32_t, const JsonNode &) override { refuseMutation(); }
	void updateUnit(uint32_t, const JsonNode &, int64_t) override { refuseMutation(); }
	void moveUnit(uint32_t, const BattleHex &) override { refuseMutation(); }
	void removeUnit(uint32_t) override { refuseMutation(); }
	void addUnitBonus(uint32_t, const std::vector<Bonus> &) override { refuseMutation(); }
	void updateUnitBonus(uint32_t, const std::vector<Bonus> &) override { refuseMutation(); }
	void removeUnitBonus(uint32_t, const std::vector<Bonus> &) override { refuseMutation(); }
	void setWallState(EWallPart, EWallState) override { refuseMutation(); }
	void addObstacle(const ObstacleChanges &) override { refuseMutation(); }
	void updateObstacle(const ObstacleChanges &) override { refuseMutation(); }
	void removeObstacle(uint32_t) override { refuseMutation(); }
private:
	[[noreturn]] static void refuseMutation() { throw std::logic_error("Read-only Second Wind snapshot cannot mutate battle"); }
};

class NewHorizonsSecondWindFormationTest : public HeroCommandFixture
{
protected:
	CStack * actor = nullptr;
	CStack * window = nullptr;
	CStack * target = nullptr;

	NewHorizonsSecondWindFormationTest() { useRevisedOrders = true; }

	void selectBasicAuthority()
	{
		constexpr auto authority = "new-horizons:authority";
		constexpr auto conditioning = "new-horizons:authority.conditioning";
		const auto & rules = attackerSideHero->getPerkState().rules;
		ASSERT_EQ(rules["commandAuthorityPolicy"]["version"].Integer(), 1);
		ASSERT_EQ(rules["warfarePolicyVersion"].Integer(), 1);
		ASSERT_EQ(rules["divineMandatePolicy"]["version"].Integer(), 2);
		ASSERT_EQ(attackerSideHero->getPerkSkillRank(authority), 0);
		// Use ordinary rank advancement and a real legal Basic-tier offer.
		gameHandler->levelUpHero(attackerSideHero, SecondarySkill(SecondarySkill::decode(authority)), false);
		ASSERT_EQ(attackerSideHero->getPerkSkillRank(authority), MasteryLevel::BASIC);
		const auto lookup = [this](const std::string & id) { return attackerSideHero->getPerkSkillRank(id); };
		for(uint64_t offerSeed = 0; offerSeed < 4096; ++offerSeed)
		{
			const auto offers = attackerSideHero->getPerkState().prepareOffer(lookup, offerSeed,
				attackerSideHero->getPerkOfferWeightContext());
			for(size_t choice = 0; choice < offers.size(); ++choice)
				if(offers[choice].selection.skillId == authority && offers[choice].selection.perkId == conditioning)
				{
					gameHandler->levelUpHero(attackerSideHero, offers, choice, offerSeed, false);
					ASSERT_TRUE(attackerSideHero->hasActivePerk(authority, conditioning));
					return;
				}
		}
		FAIL() << "No legal Basic Authority Conditioning offer";
	}

	void prepare()
	{
		startGame();
		ASSERT_EQ(ESerializationVersion::CURRENT, ESerializationVersion::NEW_HORIZONS_COMMAND_AUTHORITY);
		ASSERT_NO_FATAL_FAILURE(selectBasicAuthority());
		ASSERT_FALSE(newHorizonsDivineMandate::hasCrownAndAltarPerk(attackerSideHero));
		const auto command = attackerSideHero->getArmyCommand();
		ASSERT_TRUE(command);
		ASSERT_EQ(command->effective(), 2);
		attackerSideHero->setPrimarySkill(PrimarySkill::ATTACK, 80, ChangeValueMode::ABSOLUTE);
		attackerSideHero->setPrimarySkill(PrimarySkill::DEFENSE, 80, ChangeValueMode::ABSOLUTE);
		ASSERT_EQ(heroCommands::leadershipEfficiencyPercent(*attackerSideHero, 0), 100);
		ASSERT_EQ((attackerSideHero->getPrimSkillLevel(PrimarySkill::ATTACK)
			+ attackerSideHero->getPrimSkillLevel(PrimarySkill::DEFENSE)) / 2 + 10 * command->effective(), 100);
		startBattle();
		ASSERT_TRUE(newHorizonsWarfare::usesRules(battle()->getHeroCommandRules()));
		ASSERT_EQ(battle()->getHeroCommandRules()["commands"]["secondWind"]["effects"]
			["additionalActivationDamagePercent"]["base"].Integer(), 50);
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
		actor = addStack(BattleSide::ATTACKER, creatureByName("core:fairieDragon"), BattleHex(leftHex), 10);
		window = addStack(BattleSide::ATTACKER, CreatureID(CreatureID::decode("core:pikeman")), BattleHex(leftHex - 3), 10);
		target = addStack(BattleSide::DEFENDER, CreatureID(CreatureID::decode("core:pikeman")), BattleHex(rightHex), 10000);
		ASSERT_NE(actor, nullptr);
		ASSERT_NE(window, nullptr);
		ASSERT_NE(target, nullptr);
		for(auto * unit : {actor, window, target})
			unit->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
				BonusType::NO_MORALE, BonusSource::OTHER, 0, BonusSourceID()));
		for(auto [unit, initiative] : {std::pair{actor, 1000}, std::pair{window, 900}})
			unit->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
				BonusType::STACKS_INITIATIVE_BASE, BonusSource::OTHER, initiative,
				BonusSourceID(), BonusSubtypeID(), BonusValueType::BASE_NUMBER));
		for(const auto bound : {BonusValueType::INDEPENDENT_MIN, BonusValueType::INDEPENDENT_MAX})
		{
			actor->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::PRIMARY_SKILL,
				BonusSource::OTHER, 10, BonusSourceID(), BonusSubtypeID(PrimarySkill::ATTACK), bound));
			target->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::PRIMARY_SKILL,
				BonusSource::OTHER, 10, BonusSourceID(), BonusSubtypeID(PrimarySkill::DEFENSE), bound));
			for(const auto subtype : {BonusCustomSubtype::creatureDamageMin, BonusCustomSubtype::creatureDamageMax})
				actor->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::CREATURE_DAMAGE,
					BonusSource::OTHER, 100, BonusSourceID(), subtype, bound));
		}
		beginCombat();
		ASSERT_EQ(battle()->battleActiveUnit(), actor);
	}

	void grantSecondWind()
	{
		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
			BattleAction::makeDefend(actor)));
		ASSERT_EQ(battle()->battleActiveUnit(), window);
		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
			BattleAction::makeTargetedHeroCommand(BattleSide::ATTACKER, HeroCommand::SECOND_WIND, actor->unitId())));
		ASSERT_EQ(battle()->battleActiveUnit(), actor);
		const auto order = battle()->getHeroOrderState(BattleSide::ATTACKER, HeroCommand::SECOND_WIND);
		ASSERT_TRUE(order);
		ASSERT_TRUE(order->secondWindActive);
		ASSERT_EQ(order->primaryTargetUnitId, actor->unitId());
		ASSERT_EQ(order->divineMandateEfficiencyBonusPercent(), 0);
		ASSERT_TRUE(order->crownAndAltarRecipientUnitIds.empty());
	}

	void expectAttackDamage(const CBattleInfoCallback & callback, int expected)
	{
		const auto damage = callback.calculateDmgRange(BattleAttackInfo(actor, target, 0, false)).damage;
		EXPECT_EQ(damage.min, expected);
		EXPECT_EQ(damage.max, expected);
	}
};
}

TEST_F(NewHorizonsSecondWindFormationTest, AcceptedOrderUsesSixtyPercentInActualAttackCalculation)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	expectAttackDamage(*battle(), 1000);
	ASSERT_NO_FATAL_FAILURE(grantSecondWind());
	const auto order = *battle()->getHeroOrderState(BattleSide::ATTACKER, HeroCommand::SECOND_WIND);
	EXPECT_EQ(battle()->battleSecondWindPercent(BattleSide::ATTACKER, order, actor->unitId()), 60);
	EXPECT_EQ(battle()->battleSecondWindPercent(BattleSide::ATTACKER, order, actor->unitId(), true), 60);
	expectAttackDamage(*battle(), 600);
}

TEST_F(NewHorizonsSecondWindFormationTest, CapturedCrownAddsOnlyTwoPointsInCallbackAndAttackConsumer)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(grantSecondWind());
	const auto liveOrder = *battle()->getHeroOrderState(BattleSide::ATTACKER, HeroCommand::SECOND_WIND);
	auto captured = liveOrder;
	captured.crownAndAltarRecipientUnitIds = {actor->unitId()};
	SecondWindOrderSnapshot snapshot(std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0)), captured);
	EXPECT_EQ(snapshot.battleSecondWindPercent(BattleSide::ATTACKER, captured, actor->unitId(), false), 60);
	EXPECT_EQ(snapshot.battleSecondWindPercent(BattleSide::ATTACKER, captured, actor->unitId(), true), 62);
	EXPECT_EQ(snapshot.battleSecondWindPercent(BattleSide::ATTACKER, captured, window->unitId(), true), 60);
	EXPECT_EQ(newHorizonsDivineMandate::crownSecondWindPercent(*attackerSideHero, captured, actor->unitId()), 62);
	EXPECT_EQ(newHorizonsDivineMandate::crownSecondWindPercent(*attackerSideHero, captured, window->unitId()), 60);
	expectAttackDamage(snapshot, 620); // 50 fixed + 12 derived, never 60 * 1.2 = 72.
	expectAttackDamage(*battle(), 600);
	EXPECT_EQ(*battle()->getHeroOrderState(BattleSide::ATTACKER, HeroCommand::SECOND_WIND), liveOrder);
}

TEST_F(NewHorizonsSecondWindFormationTest, CreatureActiveSpellDamageUsesNativeSixtyAndCapturedCrownSixtyTwo)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(grantSecondWind());
	auto captured = *battle()->getHeroOrderState(BattleSide::ATTACKER, HeroCommand::SECOND_WIND);
	captured.crownAndAltarRecipientUnitIds = {actor->unitId()};
	SecondWindOrderSnapshot snapshot(std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0)), captured);
	const SpellID arrow(SpellID::MAGIC_ARROW);
	spells::BattleCast ordinaryCast(battle(), actor, spells::Mode::CREATURE_ACTIVE, arrow.toSpell());
	spells::BattleCast crownCast(&snapshot, actor, spells::Mode::CREATURE_ACTIVE, arrow.toSpell());
	ordinaryCast.setSpellLevel(MasteryLevel::ADVANCED);
	crownCast.setSpellLevel(MasteryLevel::ADVANCED);
	const auto ordinary = arrow.toSpell()->battleMechanics(&ordinaryCast);
	const auto crown = arrow.toSpell()->battleMechanics(&crownCast);
	ASSERT_EQ(ordinary->getDirectCreatureActivationDamagePercent(), 60);
	ASSERT_EQ(crown->getDirectCreatureActivationDamagePercent(), 62);
	const auto full = ordinary->adjustEffectValue(target);
	ASSERT_GT(full, 0);
	EXPECT_EQ(crown->adjustEffectValue(target), full);
	JsonNode effect;
	effect["damage"]["type"].String() = "core:damage";
	const auto effects = spells::effects::Effects::loadJson(effect, "core", "magicArrow");
	const battle::Target aim{spells::Destination(target)};
	EXPECT_EQ(-effects.at("damage")->getHealthChange(ordinary.get(), aim).hpDelta, full * 60 / 100);
	EXPECT_EQ(-effects.at("damage")->getHealthChange(crown.get(), aim).hpDelta, full * 62 / 100);
	EXPECT_GT(full * 62 / 100, full * 60 / 100);
}

TEST_F(NewHorizonsSecondWindFormationTest, CurrentFormatCopiedCrownEligibilitySurvivesSourceAndLiveOrderCompletion)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(grantSecondWind());
	auto outgoing = *battle()->getHeroOrderState(BattleSide::ATTACKER, HeroCommand::SECOND_WIND);
	outgoing.crownAndAltarRecipientUnitIds = {actor->unitId()};
	CMemorySerializer wire;
	wire.oser.version = ESerializationVersion::NEW_HORIZONS_COMMAND_AUTHORITY;
	wire.iser.version = ESerializationVersion::NEW_HORIZONS_COMMAND_AUTHORITY;
	ASSERT_NO_THROW(wire.oser & outgoing);
	HeroOrderState restored;
	ASSERT_NO_THROW(wire.iser & restored);
	ASSERT_EQ(restored, outgoing);
	SecondWindOrderSnapshot snapshot(std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0)), restored);
	outgoing.crownAndAltarRecipientUnitIds.clear();
	restored.crownAndAltarRecipientUnitIds.clear();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeDefend(actor)));
	const auto completed = battle()->getHeroOrderState(BattleSide::ATTACKER, HeroCommand::SECOND_WIND);
	ASSERT_TRUE(completed);
	ASSERT_FALSE(completed->secondWindActive);
	const auto frozen = snapshot.getHeroOrderState(BattleSide::ATTACKER, HeroCommand::SECOND_WIND);
	ASSERT_TRUE(frozen);
	EXPECT_TRUE(frozen->secondWindActive);
	EXPECT_EQ(frozen->crownAndAltarRecipientUnitIds, std::vector<uint32_t>{actor->unitId()});
	EXPECT_EQ(snapshot.battleSecondWindPercent(BattleSide::ATTACKER, *frozen, actor->unitId(), true), 62);
	EXPECT_EQ(snapshot.battleSecondWindPercent(BattleSide::ATTACKER, *frozen, window->unitId(), true), 60);
	expectAttackDamage(snapshot, 620);
	expectAttackDamage(*battle(), 1000);
}
