/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include "../../../lib/entities/hero/NewHorizonsHeroRules.h"
#include "../../../lib/entities/hero/CHero.h"
#include "../../../lib/battle/BattleAttackInfo.h"
#include "../../../lib/battle/NewHorizonsOrdinaryStrike.h"
#include "../../../lib/battle/NewHorizonsMagicalAbilityDamage.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../lib/networkPacks/SetStackEffect.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/MagicalDamageReduction.h"
#if ENABLE_BATTLE_AI
#include "../../../AI/BattleAI/HypotheticBattle.h"
#include "../../../AI/BattleAI/StackWithBonuses.h"
#include <vcmi/Environment.h>
#endif
namespace
{
class NewHorizonsOrdinaryStrikeSpecialtyTest : public HeroCommandFixture
{
protected:
	bool historical = false;
	bool grantDefenderSecondWind = false;
	CStack * actor = nullptr;
	CStack * victim = nullptr;
	CStack * competitor = nullptr;
	void mapLoaded(CMap * map) override
	{
		useRevisedOrders = true;
		HeroCommandFixture::mapLoaded(map);
		JsonNode rules(JsonPath::builtin("config/newHorizonsHeroes"));
		if(historical)
		{
			rules["additionalSpecialties"]["ordinaryCreatureSpecialties"].Struct().erase("wingbeatInitiativePercent");
			rules["additionalSpecialties"]["ordinaryCreatureSpecialties"].Struct().erase("measuredVolleyMdrPenetrationPercent");
		}
		rules.setOverrideFlag(true);
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, rules);
	}
	void prepare(bool volley = false, int competitorInitiative = 18)
	{
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		const char * hero = volley ? "core:theodorus" : "core:aeris";
		const char * creature = volley ? "core:archMage" : "core:silverPegasus";
		builder.size(36, false).playerActive(PlayerColor(0)).playerActive(PlayerColor(1))
			.hero({5, 5, 0}, HeroTypeID(HeroTypeID::decode(hero)), PlayerColor(0)).heroExperience(0)
			.heroGarrison({{CreatureID(CreatureID::decode(creature)), 10}})
			.hero({7, 7, 0}, HeroTypeID(HeroTypeID::decode("core:orrin")), PlayerColor(1)).heroExperience(0)
			.heroGarrison({{CreatureID(CreatureID::decode("core:pikeman")), 1000}});
		startWithMap(std::move(builder));
		server.gameState = gameState();
		gameHandler = std::make_shared<CGameHandler>(server, gameState());
		attackerSideHero = findHeroByOwner(PlayerColor(0));
		defenderSideHero = findHeroByOwner(PlayerColor(1));
		if(grantDefenderSecondWind)
			defenderSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:command")),
				MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		startBattle();
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
		actor = addStack(BattleSide::ATTACKER, creatureByName(creature), BattleHex(3, 5), 10);
		victim = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), volley ? BattleHex(12, 5) : BattleHex(4, 5), 1000);
		competitor = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(13, 8), 1);
		for(auto * unit : {actor, victim, competitor})
		{
			unit->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::NO_MORALE,
				BonusSource::OTHER, 1, BonusSourceID()));
			for(const auto bound : {BonusValueType::INDEPENDENT_MIN, BonusValueType::INDEPENDENT_MAX})
				unit->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::STACKS_INITIATIVE_BASE,
					BonusSource::OTHER, unit == actor ? 60 : unit == victim ? 20 : competitorInitiative,
					BonusSourceID(), BonusSubtypeID(), bound));
		}
		actor->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::BLOCKS_RETALIATION,
			BonusSource::OTHER, 1, BonusSourceID()));
	}
	void publish(const newHorizonsOrdinaryStrike::EffectPlan & plan)
	{
		SetStackEffect packet;
		packet.battleID = BattleID(0);
		if(!plan.remove.empty()) packet.toRemove.emplace_back(plan.target, plan.remove);
		if(!plan.add.empty()) packet.toAdd.emplace_back(plan.target, plan.add);
		gameHandler->sendAndApply(packet);
	}
};
#if ENABLE_BATTLE_AI
class StrikeEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit StrikeEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};
#endif
TEST_F(NewHorizonsOrdinaryStrikeSpecialtyTest, WingbeatChangesActualNextSelectionBeforeExpiryAndCopiedQueueAgrees)
{
	prepare();
	beginCombat();
	ASSERT_EQ(battle()->battleActiveUnit(), actor);
	const auto before = victim->getAvailableHealth();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeMeleeAttack(actor, victim, actor->getPosition())));
	ASSERT_LT(victim->getAvailableHealth(), before);
	ASSERT_EQ(victim->getInitiative(), 17);
	// Without Wingbeat the20-Initiative victim precedes the18 competitor.
	// The actual server router already chose the competitor before expiry.
	ASSERT_EQ(battle()->battleActiveUnit(), competitor);
#if ENABLE_BATTLE_AI
	StrikeEnvironment env(gameState());
	auto branch = std::make_shared<HypotheticBattle>(&env, std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0)));
	std::vector<battle::Units> penalized;
	branch->battleGetTurnOrder(penalized, 1, 0, -1);
	ASSERT_FALSE(penalized.empty());
	ASSERT_FALSE(penalized.front().empty());
	EXPECT_EQ(penalized.front().front()->unitId(), competitor->unitId());
	auto clean = std::make_shared<HypotheticBattle>(&env, branch);
	clean->getForUpdate(victim->unitId())->removeUnitBonus(CSelector([](const Bonus * b)
		{ return b && b->statusIdentity == "wingbeat"; }));
	std::vector<battle::Units> ordinary;
	clean->battleGetTurnOrder(ordinary, 1, 0, -1);
	ASSERT_FALSE(ordinary.empty());
	ASSERT_FALSE(ordinary.front().empty());
	EXPECT_EQ(ordinary.front().front()->unitId(), victim->unitId());
	branch->nextTurn(victim->unitId(), BattleUnitTurnReason::TURN_QUEUE);
	EXPECT_EQ(branch->battleGetUnitByID(victim->unitId())->getInitiative(), 20);
	EXPECT_EQ(victim->getInitiative(), 17) << "Copied expiry must not touch authority";
#endif
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(1), BattleAction::makeDefend(competitor)));
	ASSERT_EQ(battle()->battleActiveUnit(), victim);
	EXPECT_EQ(victim->getInitiative(), 20);
}
TEST_F(NewHorizonsOrdinaryStrikeSpecialtyTest, PrimaryPhysicalOrdinaryCounterRefreshAndExclusionBoundaries)
{
	prepare();
	BattleAttackInfo attack(actor, victim, 0, false);
	EXPECT_TRUE(newHorizonsOrdinaryStrike::plan(*battle(), attack, 0).add.empty());
	const auto first = newHorizonsOrdinaryStrike::plan(*battle(), attack, 1);
	ASSERT_EQ(first.add.size(), 1);
	EXPECT_EQ(first.add.front().val, -15);
	publish(first);
	attack.retaliation = true;
	const auto refresh = newHorizonsOrdinaryStrike::plan(*battle(), attack, 1);
	ASSERT_EQ(refresh.remove.size(), 1);
	ASSERT_EQ(refresh.add.size(), 1);
	publish(refresh);
	auto stronger = newHorizonsOrdinaryStrike::plan(*battle(), attack, 1);
	ASSERT_EQ(stronger.add.size(), 1);
	stronger.add.front().val = -25;
	stronger.add.front().description = MetaString::createFromRawString("Wingbeat: Initiative -25%; until next activation.");
	publish(stronger);
	const auto retainStrongest = newHorizonsOrdinaryStrike::plan(*battle(), attack, 1);
	ASSERT_EQ(retainStrongest.add.size(), 1);
	EXPECT_EQ(retainStrongest.add.front().val, -25);
	EXPECT_EQ(retainStrongest.add.front().description, stronger.add.front().description);
	publish(retainStrongest);
	EXPECT_EQ(std::ranges::count_if(victim->getExportedBonusList(), [](const auto & b)
		{ return b && b->statusIdentity == "wingbeat"; }), 1);
	for(int flag = 0; flag < 6; ++flag)
	{
		auto excluded = attack;
		if(flag == 0) excluded.shooting = true;
		if(flag == 1) excluded.physicalDamage = false;
		if(flag == 2) excluded.secondaryAttack = true;
		if(flag == 3) excluded.bracePreemptive = true;
		if(flag == 4) excluded.preemptiveDamagePercent = 50;
		if(flag == 5) excluded.cleaveDamagePercent = 50;
		EXPECT_TRUE(newHorizonsOrdinaryStrike::plan(*battle(), excluded, 1).add.empty());
	}
}
TEST_F(NewHorizonsOrdinaryStrikeSpecialtyTest, MeasuredVolleyActualShotLowersPositiveMdrRelativelyInCurrentAndLegacyModes)
{
	// Keep an ordinary activation between the shot and its victim: the rider
	// expires when the victim's next activation begins, not after it acts.
	prepare(true, 30);
	beginCombat();
	ASSERT_EQ(battle()->battleActiveUnit(), actor);
	ASSERT_TRUE(battle()->battleCanShoot(actor, victim->getPosition()));
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), BattleAction::makeShotAttack(actor, victim)));
	ASSERT_EQ(battle()->battleActiveUnit(), competitor);
	EXPECT_EQ(spells::targetMdrPenetrations(*victim), (std::vector<int>{20}));
	const auto * spell = SpellID(SpellID::MAGIC_ARROW).toSpell();
	ASSERT_NE(spell, nullptr);
	victim->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::SPELL_DAMAGE_REDUCTION,
		BonusSource::OTHER, 50, BonusSourceID(), BonusSubtypeID(SpellSchool::ANY)));
	EXPECT_EQ(spell->adjustRawDamage(attackerSideHero, victim, 10000, 0, 0, 100, true, true, false), 6000);
	EXPECT_EQ(spell->adjustRawDamage(attackerSideHero, victim, 10000, 0, 0, 100, false, false, false), 6000);
	EXPECT_EQ(spell->adjustRawDamage(attackerSideHero, victim, 10000, 20, 0, 100, true, true, false), 6800);
	EXPECT_EQ(newHorizonsMagicalAbilityDamage::adjustDamage(*battle(), *victim, 10000), 6000);
	victim->removeBonuses(Selector::type()(BonusType::SPELL_DAMAGE_REDUCTION));
	EXPECT_EQ(spell->adjustRawDamage(attackerSideHero, victim, 10000, 0, 0, 100, true, true, false), 10000);
	victim->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::SPELL_DAMAGE_REDUCTION,
		BonusSource::OTHER, 100, BonusSourceID(), BonusSubtypeID(SpellSchool::ANY)));
	EXPECT_EQ(spell->adjustRawDamage(attackerSideHero, victim, 10000, 0, 0, 100, true, true, false), 2400)
		<< "95% cap precedes the20% relative contributor";
	EXPECT_EQ(spell->adjustRawDamage(attackerSideHero, victim, 10000, 0, 0, 100, false, false, false), 2000)
		<< "Historical100% source retains its old full reduction before the new relative modifier";
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(1), BattleAction::makeDefend(competitor)));
	ASSERT_EQ(battle()->battleActiveUnit(), victim);
	EXPECT_TRUE(spells::targetMdrPenetrations(*victim).empty());
	EXPECT_EQ(spell->adjustRawDamage(attackerSideHero, victim, 10000, 0, 0, 100, true, true, false), 500);
	EXPECT_EQ(spell->adjustRawDamage(attackerSideHero, victim, 10000, 0, 0, 100, false, false, false), 0);
}
TEST_F(NewHorizonsOrdinaryStrikeSpecialtyTest, MeasuredVolleyExpiresWhenShotImmediatelySelectsVictim)
{
	prepare(true);
	beginCombat();
	ASSERT_EQ(battle()->battleActiveUnit(), actor);
	ASSERT_TRUE(battle()->battleCanShoot(actor, victim->getPosition()));
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), BattleAction::makeShotAttack(actor, victim)));
	ASSERT_EQ(battle()->battleActiveUnit(), victim);
	EXPECT_TRUE(spells::targetMdrPenetrations(*victim).empty());
}
TEST_F(NewHorizonsOrdinaryStrikeSpecialtyTest, FullSpellImmunityStillRejectsSpellWhilePhysicalRiderCanRemain)
{
	prepare(true);
	BattleAttackInfo shot(actor, victim, 0, true);
	publish(newHorizonsOrdinaryStrike::plan(*battle(), shot, 1));
	victim->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::SPELL_IMMUNITY,
		BonusSource::OTHER, 1, BonusSourceID(), BonusSubtypeID(SpellID(SpellID::MAGIC_ARROW))));
	attackerSideHero->addSpellToSpellbook(SpellID::MAGIC_ARROW);
	setTestSpellPointTotal(attackerSideHero, 1000);
	beginCombat();
	ASSERT_EQ(battle()->battleActiveUnit(), actor);
	const auto before = victim->getAvailableHealth();
	EXPECT_FALSE(castOn(attackerSideHero, SpellID::MAGIC_ARROW, victim));
	EXPECT_EQ(victim->getAvailableHealth(), before);
	EXPECT_EQ(spells::targetMdrPenetrations(*victim), (std::vector<int>{20}));
	victim->removeBonuses(Selector::type()(BonusType::SPELL_IMMUNITY));
	ASSERT_TRUE(castOn(attackerSideHero, SpellID::MAGIC_ARROW, victim))
		<< "Same legal cast without immunity is the positive control";
	EXPECT_LT(victim->getAvailableHealth(), before);
}
TEST_F(NewHorizonsOrdinaryStrikeSpecialtyTest, CurrentEffectiveOwnerControlsEntitlementAndMissingKeyRemainsValidPolicy)
{
	prepare();
	BattleAttackInfo attack(actor, victim, 0, false);
	ASSERT_FALSE(newHorizonsOrdinaryStrike::plan(*battle(), attack, 1).add.empty());
	const auto controlled = std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::HYPNOTIZED,
		BonusSource::OTHER, 1, BonusSourceID());
	actor->addNewBonus(controlled);
	EXPECT_TRUE(newHorizonsOrdinaryStrike::plan(*battle(), attack, 1).add.empty());
	actor->removeBonus(controlled);
	EXPECT_FALSE(newHorizonsOrdinaryStrike::plan(*battle(), attack, 1).add.empty());
	JsonNode historical = attackerSideHero->getPrimaryGrowthRules();
	historical["additionalSpecialties"]["ordinaryCreatureSpecialties"].Struct().erase("wingbeatInitiativePercent");
	EXPECT_NO_THROW(newHorizonsHeroes::validateOrdinaryCreatureSpecialtiesSerialization(historical, true));
	EXPECT_NO_THROW(newHorizonsHeroes::validateOrdinaryCreatureExtensionsSerialization(historical, true));
}

class NewHorizonsStrikeCaptureTest : public NewHorizonsOrdinaryStrikeSpecialtyTest,
	public ::testing::WithParamInterface<bool> {};
TEST_P(NewHorizonsStrikeCaptureTest, CurrentCapturedPolicyAndStartingDevelopmentSurviveWorldRoundtrip)
{
	prepare(GetParam());
	ASSERT_TRUE(newHorizonsHeroes::hasOrdinaryCreatureSpecialty(attackerSideHero));
	CGameState restored;
	restored.preInit(LIBRARY);
	restored.loadFromMemory(gameState()->saveToMemory());
	const auto * loaded = restored.getHero(attackerSideHero->id);
	ASSERT_NE(loaded, nullptr);
	EXPECT_EQ(loaded->getPrimaryGrowthRules(), attackerSideHero->getPrimaryGrowthRules());
	EXPECT_EQ(loaded->secSkills, attackerSideHero->secSkills);
	EXPECT_EQ(loaded->getSpecialtyPresentation().name, GetParam() ? "Measured Volley" : "Wingbeat");
}
TEST_P(NewHorizonsStrikeCaptureTest, MissingOptionalPolicyRetainsHistoricalGenericSpecialtyAfterRoundtrip)
{
	historical = true;
	prepare(GetParam());
	EXPECT_FALSE(newHorizonsHeroes::hasOrdinaryCreatureSpecialty(attackerSideHero));
	BattleAttackInfo hit(actor, victim, 0, GetParam());
	EXPECT_TRUE(newHorizonsOrdinaryStrike::plan(*battle(), hit, 1).add.empty());
	CGameState restored;
	restored.preInit(LIBRARY);
	restored.loadFromMemory(gameState()->saveToMemory());
	const auto * loaded = restored.getHero(attackerSideHero->id);
	ASSERT_NE(loaded, nullptr);
	EXPECT_FALSE(newHorizonsHeroes::hasOrdinaryCreatureSpecialty(loaded));
	EXPECT_EQ(loaded->getPrimaryGrowthRules(), attackerSideHero->getPrimaryGrowthRules());
}
#if ENABLE_BATTLE_AI
TEST_P(NewHorizonsStrikeCaptureTest, DescendantStatusSnapshotSurvivesLiveAndParentExpiryUntilItsOwnActivation)
{
	prepare(GetParam());
	BattleAttackInfo hit(actor, victim, 0, GetParam());
	publish(newHorizonsOrdinaryStrike::plan(*battle(), hit, 1));
	const std::string identity = GetParam() ? "measuredVolley" : "wingbeat";
	const CSelector selected([identity](const Bonus * bonus) { return bonus && bonus->statusIdentity == identity; });
	StrikeEnvironment env(gameState());
	auto parent = std::make_shared<HypotheticBattle>(&env, std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0)));
	auto child = std::make_shared<HypotheticBattle>(&env, parent);
	beginCombat();
	for(int step = 0; step < 8 && battle()->battleActiveUnit() != victim; ++step)
	{
		const auto * active = battle()->battleActiveUnit();
		ASSERT_NE(active, nullptr);
		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), battle()->battleGetOwner(active),
			BattleAction::makeDefend(active)));
	}
	ASSERT_EQ(battle()->battleActiveUnit(), victim);
	EXPECT_FALSE(victim->hasBonus(selected));
	EXPECT_TRUE(parent->battleGetUnitByID(victim->unitId())->hasBonus(selected));
	// First child query occurs after live expiry; constructor must have owned it.
	EXPECT_TRUE(child->battleGetUnitByID(victim->unitId())->hasBonus(selected));
	parent->nextTurn(victim->unitId(), BattleUnitTurnReason::TURN_QUEUE);
	EXPECT_FALSE(parent->battleGetUnitByID(victim->unitId())->hasBonus(selected));
	EXPECT_TRUE(child->battleGetUnitByID(victim->unitId())->hasBonus(selected));
	child->nextTurn(victim->unitId(), BattleUnitTurnReason::TURN_QUEUE);
	EXPECT_FALSE(child->battleGetUnitByID(victim->unitId())->hasBonus(selected));
}
#endif

TEST_P(NewHorizonsStrikeCaptureTest, GenuineSecondWindExpiresBothRidersAfterActualDelayedAttack)
{
	grantDefenderSecondWind = true;
	prepare(GetParam());
	auto * window = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(10, 8), 1);
	window->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::NO_MORALE,
		BonusSource::OTHER, 1, BonusSourceID()));
	for(const auto bound : {BonusValueType::INDEPENDENT_MIN, BonusValueType::INDEPENDENT_MAX})
		window->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::STACKS_INITIATIVE_BASE,
			BonusSource::OTHER, 100, BonusSourceID(), BonusSubtypeID(), bound));
	auto act = [&](const battle::Unit * unit, const BattleAction & action)
	{
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), battle()->battleGetOwner(unit), action);
	};
	beginCombat();
	ASSERT_EQ(battle()->battleActiveUnit(), window);
	ASSERT_TRUE(act(window, BattleAction::makeWait(window)));
	ASSERT_EQ(battle()->battleActiveUnit(), actor);
	ASSERT_TRUE(act(actor, BattleAction::makeWait(actor)));
	ASSERT_EQ(battle()->battleActiveUnit(), victim);
	ASSERT_TRUE(act(victim, BattleAction::makeDefend(victim)));
	ASSERT_EQ(battle()->battleActiveUnit(), competitor);
	ASSERT_TRUE(act(competitor, BattleAction::makeDefend(competitor)));
	ASSERT_EQ(battle()->battleActiveUnit(), actor) << "Wait phase selects slower60 before100";
	ASSERT_TRUE(act(actor, GetParam() ? BattleAction::makeShotAttack(actor, victim)
		: BattleAction::makeMeleeAttack(actor, victim, actor->getPosition())));
	ASSERT_EQ(battle()->battleActiveUnit(), window);
	const std::string identity = GetParam() ? "measuredVolley" : "wingbeat";
	const CSelector selected([identity](const Bonus * bonus) { return bonus && bonus->statusIdentity == identity; });
	ASSERT_TRUE(victim->hasBonus(selected));
	ASSERT_TRUE(battle()->battleCanBeginHeroCommand(BattleSide::DEFENDER, HeroCommand::SECOND_WIND));
#if ENABLE_BATTLE_AI
	StrikeEnvironment env(gameState());
	auto branch = std::make_shared<HypotheticBattle>(&env, std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(1)));
	const auto allowance = branch->prepareHeroOrderAllowance(BattleSide::DEFENDER);
	ASSERT_TRUE(allowance);
	const auto effects = branch->beginProjectedHeroOrder(BattleSide::DEFENDER, HeroCommand::SECOND_WIND,
		{victim->unitId()}, *allowance);
	ASSERT_TRUE(effects);
	ASSERT_TRUE(branch->projectAcceptedHeroOrder(BattleSide::DEFENDER, *effects, *allowance));
	const auto continuation = branch->projectOrderContinuation(BattleSide::DEFENDER, *allowance, *effects);
	EXPECT_EQ(continuation.kind, HypotheticBattle::ProjectedOrderContinuation::Kind::SECOND_WIND_ACTOR);
	EXPECT_EQ(branch->battleActiveUnit()->unitId(), victim->unitId());
	EXPECT_FALSE(branch->battleGetUnitByID(victim->unitId())->hasBonus(selected));
	EXPECT_TRUE(victim->hasBonus(selected)) << "Projection must not expire authority";
#endif
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(1),
		BattleAction::makeTargetedHeroCommand(BattleSide::DEFENDER, HeroCommand::SECOND_WIND, victim->unitId())));
	ASSERT_EQ(battle()->battleActiveUnit(), victim);
	EXPECT_FALSE(victim->hasBonus(selected));
}
TEST_P(NewHorizonsStrikeCaptureTest, HeroSpellContinuationProtocolRetainsEffectWithoutBeginningActivation)
{
	prepare(GetParam());
	beginCombat();
	// The ordinary fixture actor is active. This isolates the existing
	// continuation protocol from hit admission, covered by actual attack cases.
	BattleAttackInfo hit(actor, victim, 0, GetParam());
	const auto plan = newHorizonsOrdinaryStrike::plan(*battle(), hit, 1);
	ASSERT_EQ(plan.add.size(), 1);
	SetStackEffect applied;
	applied.battleID = BattleID(0);
	applied.toAdd.emplace_back(actor->unitId(), plan.add);
	gameHandler->sendAndApply(applied);
	const std::string identity = GetParam() ? "measuredVolley" : "wingbeat";
	const CSelector selected([identity](const Bonus * bonus) { return bonus && bonus->statusIdentity == identity; });
	const auto serial = battle()->getActivationSerial();
#if ENABLE_BATTLE_AI
	StrikeEnvironment env(gameState());
	auto branch = std::make_shared<HypotheticBattle>(&env, std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0)));
	branch->nextTurn(actor->unitId(), BattleUnitTurnReason::HERO_SPELLCAST);
	EXPECT_TRUE(branch->battleGetUnitByID(actor->unitId())->hasBonus(selected));
	EXPECT_EQ(branch->getActivationSerial(), serial);
#endif
	BattleSetActiveStack continuation;
	continuation.battleID = BattleID(0);
	continuation.stack = actor->unitId();
	continuation.reason = BattleUnitTurnReason::HERO_SPELLCAST;
	gameHandler->sendAndApply(continuation);
	EXPECT_EQ(battle()->battleActiveUnit(), actor);
	EXPECT_TRUE(actor->hasBonus(selected));
	EXPECT_EQ(battle()->getActivationSerial(), serial);
}

INSTANTIATE_TEST_SUITE_P(TwoStrikeIdentities, NewHorizonsStrikeCaptureTest, ::testing::Bool());

TEST(OrdinaryStrikeBonusSerialization, NewTargetMdrEnumRejects1144BeforeDirectAndPacketPrefixes)
{
	Bonus effect(BonusDuration::STACK_GETS_TURN, BonusType::TARGET_MAGICAL_DAMAGE_REDUCTION_PENETRATION,
		BonusSource::OTHER, 20, BonusSourceID());
	CMemorySerializer old;
	old.oser.version = ESerializationVersion::NEW_HORIZONS_ORDINARY_CREATURE_SPECIALTIES;
	EXPECT_THROW(old.oser & effect, std::runtime_error);
	EXPECT_TRUE(old.extractBuffer().empty());
	SetStackEffect packet;
	packet.battleID = BattleID(0);
	packet.toAdd.emplace_back(7, std::vector<Bonus>{effect});
	CMemorySerializer oldPacket;
	oldPacket.oser.version = ESerializationVersion::NEW_HORIZONS_ORDINARY_CREATURE_SPECIALTIES;
	EXPECT_THROW(packet.serialize(oldPacket.oser), std::runtime_error);
	EXPECT_TRUE(oldPacket.extractBuffer().empty());
	CMemorySerializer current;
	EXPECT_NO_THROW(current.oser & effect);
	Bonus restored;
	EXPECT_NO_THROW(current.iser & restored);
	EXPECT_EQ(restored.toJsonNode(), effect.toJsonNode());
}
TEST(OrdinaryStrikeBonusSerialization, Legacy1144PacketRetainsExactFieldOrderAndRoundtrip)
{
	SetStackEffect packet;
	packet.battleID = BattleID(0);
	packet.toAdd.emplace_back(7, std::vector<Bonus>{Bonus(BonusDuration::STACK_GETS_TURN,
		BonusType::STACKS_INITIATIVE, BonusSource::OTHER, -15, BonusSourceID())});
	CMemorySerializer actual;
	actual.oser.version = ESerializationVersion::NEW_HORIZONS_ORDINARY_CREATURE_SPECIALTIES;
	ASSERT_NO_THROW(packet.serialize(actual.oser));
	CMemorySerializer original;
	original.oser.version = ESerializationVersion::NEW_HORIZONS_ORDINARY_CREATURE_SPECIALTIES;
	original.oser & packet.battleID;
	original.oser & packet.toAdd;
	original.oser & packet.toUpdate;
	original.oser & packet.toRemove;
	original.oser & packet.exchange;
	const auto bytes = actual.extractBuffer();
	EXPECT_EQ(bytes, original.extractBuffer());
	CMemorySerializer reader(bytes);
	reader.iser.version = ESerializationVersion::NEW_HORIZONS_ORDINARY_CREATURE_SPECIALTIES;
	SetStackEffect restored;
	ASSERT_NO_THROW(restored.serialize(reader.iser));
	ASSERT_EQ(restored.toAdd.size(), 1);
	ASSERT_EQ(restored.toAdd.front().second.size(), 1);
	EXPECT_EQ(restored.toAdd.front().second.front().toJsonNode(), packet.toAdd.front().second.front().toJsonNode());
}

}
