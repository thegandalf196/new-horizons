/*
 * Part of VCMI / New Horizons.
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "BattleTestFixture.h"
#include "../../SpellPointTestUtils.h"
#include "../../../lib/battle/NewHorizonsCreatureAbilitySuppression.h"
#include "../../../lib/CPlayerState.h"
#include "../../../lib/battle/NewHorizonsSoulHarvest.h"
#include "../../../lib/battle/BattleLayout.h"
#include "../../../lib/CRandomGenerator.h"
#include "../../../lib/entities/hero/NewHorizonsGhostSpecialty.h"
#include "../../../lib/entities/hero/NewHorizonsReclaimer.h"
#include "../../../lib/gameState/UpgradeInfo.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/mapping/CMap.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../server/CGameHandler.h"
#include "../../../server/battles/BattleProcessor.h"
#include "../../../server/battles/BattleResultProcessor.h"
#include "../../../server/battles/BattleCreatureKillRewards.h"
#ifdef ENABLE_BATTLE_AI
#include "../../../AI/BattleAI/AttackPossibility.h"
#include "../../../AI/BattleAI/BattleExchangeVariant.h"
#include "../../../AI/BattleAI/HypotheticBattle.h"
#endif
namespace
{
class NewHorizonsSoulHarvestTest : public BattleTestFixture
{
protected:
	bool captured = true;
	bool vidomina = true;
	bool prepareForgetfulness = false;
	bool useVulnerabilityOrb = true;
	int attackerBulwark = 0;
	int defenderBulwark = 0;
	CStack * ghost = nullptr;
	CStack * target = nullptr;
	CStack * spare = nullptr;
	void SetUp() override
	{
		BattleTestFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires curated New Horizons preset";
	}
	void mapLoaded(CMap * loaded) override
	{
		BattleTestFixture::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
		JsonNode rules(JsonPath::builtin("config/newHorizonsHeroes"));
		if(!captured)
		{
			rules.Struct().erase("ghostSpecialty");
			auto & old = rules["reclamationSpecialty"];
			old["version"].Integer() = 1;
			old["conversionPercentPerLevel"].Integer() = 5;
			old["conversionMaximumPercent"].Integer() = 100;
		}
		rules.setOverrideFlag(true);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, rules);
		for(const auto & object : loaded->objects)
			if(auto * hero = dynamic_cast<CGHeroInstance *>(object.get()); hero && hero->getOwner() == PlayerColor(0))
				hero->setHeroType(HeroTypeID(HeroTypeID::decode(vidomina ? "core:vidomina" : "core:charna")));
	}
	void prepare(int count = 10, int victims = 1)
	{
		startGame();
		const auto bulwark = SecondarySkill(SecondarySkill::decode("new-horizons:bulwarkOfTheMire"));
		attackerSideHero->setSecSkillLevel(bulwark, attackerBulwark, ChangeValueMode::ABSOLUTE);
		defenderSideHero->setSecSkillLevel(bulwark, defenderBulwark, ChangeValueMode::ABSOLUTE);
		if(prepareForgetfulness)
		{
			giveArtifact(defenderSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
			defenderSideHero->addSpellToSpellbook(SpellID::FORGETFULNESS);
			defenderSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 0, ChangeValueMode::ABSOLUTE);
			defenderSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 1000, ChangeValueMode::ABSOLUTE);
			setTestSpellPointTotal(defenderSideHero, 1000);
			if(useVulnerabilityOrb)
				giveArtifact(defenderSideHero, ArtifactID(ArtifactID::decode("core:orbOfVulnerability")),
					ArtifactPosition::MISC1);
		}
		// Synthetic combat quantities retain the original test arithmetic. These
		// are original army slots, not acquisition/capacity-command evidence;
		// ProductionCasualtyCommit below separately uses a legal five-Ghost army.
		attackerSideHero->clearSlots();
		defenderSideHero->clearSlots();
		ASSERT_TRUE(attackerSideHero->setCreature(SlotID(0), creatureByName("new-horizons:ghost"), count));
		ASSERT_TRUE(defenderSideHero->setCreature(SlotID(0), creatureByName("core:pikeman"), victims));
		ASSERT_TRUE(defenderSideHero->setCreature(SlotID(1), creatureByName("core:pikeman"), 1));
		ASSERT_TRUE(defenderSideHero->setCreature(SlotID(2), creatureByName("core:peasant"), 1000));
		startBattle();
		ghost = originalStack(BattleSide::ATTACKER, SlotID(0));
		target = originalStack(BattleSide::DEFENDER, SlotID(0));
		spare = originalStack(BattleSide::DEFENDER, SlotID(1));
		auto * reserve = originalStack(BattleSide::DEFENDER, SlotID(2));
		ASSERT_NE(ghost, nullptr);
		ASSERT_NE(target, nullptr);
		ASSERT_NE(spare, nullptr);
		ASSERT_NE(reserve, nullptr);
		ghost->setPosition(BattleHex(leftHex));
		target->setPosition(BattleHex(rightHex));
		spare->setPosition(BattleHex(rightHex + 1));
		reserve->setPosition(BattleHex(rightHex + 4));
		forceMaximumDamage(ghost);
		blockRetaliation(target);
		beginCombat();
		ASSERT_TRUE(newHorizonsSoulHarvest::qualifies(*battle(), *ghost, *target, true));
	}
	CStack * originalStack(BattleSide side, SlotID slot) const
	{
		const auto units = battle()->battleGetStacksIf([=](const CStack * unit)
		{
			return unit->unitSide() == side && unit->unitSlot() == slot;
		});
		EXPECT_EQ(units.size(), 1u);
		if(units.size() != 1)
			return nullptr;
		auto * result = battle()->getStack(units.front()->unitId());
		const auto * army = side == BattleSide::ATTACKER ? attackerSideHero : defenderSideHero;
		EXPECT_EQ(result->base, &army->getStack(slot));
		EXPECT_FALSE(result->isSummoned());
		return result;
	}
};
#ifdef ENABLE_BATTLE_AI
class HarvestEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit HarvestEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};
// A selected candidate must transfer the same HP and temporary bodies while
// leaving its query model, sibling and native stack untouched.
int64_t harvestForecast(const std::shared_ptr<CGameState> & game, CBattleInfoCallback * battle,
	CStack * actor, CStack * victim, CStack * harvestingGhost, int expectedCount)
{
	HarvestEnvironment env(game);
	auto callback = std::shared_ptr<CBattleInfoCallback>(battle, [](CBattleInfoCallback *) {});
	auto model = std::make_shared<HypotheticBattle>(&env, callback);
	auto sibling = std::make_shared<HypotheticBattle>(&env, model);
	const auto native = harvestingGhost->save();
	DamageCache cache;
	cache.buildDamageCache(model, actor->unitSide());
	const auto preview = AttackPossibility::evaluate(BattleAttackInfo(actor, victim, 0, false),
		actor->getPosition(), cache, model);
	const battle::CUnitState * projected = preview.attackerState.get();
	if(actor != harvestingGhost)
	{
		const auto found = std::ranges::find_if(preview.affectedUnits, [&](const auto & unit)
		{
			return unit->unitId() == harvestingGhost->unitId();
		});
		EXPECT_NE(found, preview.affectedUnits.end());
		if(found == preview.affectedUnits.end())
			return -1;
		projected = found->get();
	}
	EXPECT_EQ(projected->getCount(), expectedCount);
	auto replay = std::make_shared<HypotheticBattle>(&env, model);
	BattleExchangeVariant exchange;
	DamageCache replayCache;
	exchange.trackAttack(preview, replay, replayCache);
	const auto result = replay->getForUpdate(harvestingGhost->unitId());
	EXPECT_EQ(result->getCount(), projected->getCount());
	EXPECT_EQ(result->getAvailableHealth(), projected->getAvailableHealth());
	EXPECT_EQ(result->health.getResurrected(), projected->health.getResurrected());
	EXPECT_EQ(model->battleGetUnitByID(harvestingGhost->unitId())->getAvailableHealth(), harvestingGhost->getAvailableHealth());
	EXPECT_EQ(sibling->battleGetUnitByID(harvestingGhost->unitId())->getAvailableHealth(), harvestingGhost->getAvailableHealth());
	EXPECT_EQ(harvestingGhost->save(), native);
	return projected->getAvailableHealth();
}
#endif
}
TEST_F(NewHorizonsSoulHarvestTest, LiveKillAddsExactlyOneTemporaryGhost)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(attack(ghost, target->getPosition()));
	EXPECT_FALSE(target->alive());
	EXPECT_EQ(ghost->getCount(), 11);
	EXPECT_EQ(ghost->health.getResurrected(), 1);
	ASSERT_FALSE(server.attacks.empty());
}
TEST_F(NewHorizonsSoulHarvestTest, WoundedActorGainsOneWholeBodyWithoutHealingExistingWound)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	int64_t damage = 3;
	ghost->damage(damage);
	const auto hp = ghost->getAvailableHealth();
	ASSERT_TRUE(attack(ghost, target->getPosition()));
	EXPECT_EQ(ghost->getCount(), 11);
	EXPECT_EQ(ghost->getAvailableHealth(), hp + ghost->getMaxHealth());
	ghost->health.takeResurrected();
	EXPECT_EQ(ghost->getCount(), 10);
	EXPECT_EQ(ghost->getAvailableHealth(), hp);
}
TEST_F(NewHorizonsSoulHarvestTest, CleanupRetainsRealLossAndNeverConvertsTemporaryBodiesToWorldSurvivors)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	int64_t damage = 18;
	ghost->damage(damage, false, battle::DamageProvenance::PHYSICAL_CREATURE);
	ASSERT_TRUE(attack(ghost, target->getPosition()));
	EXPECT_EQ(ghost->getCount(), 10);
	EXPECT_EQ(ghost->health.getResurrected(), 1);
	ghost->health.takeResurrected();
	EXPECT_EQ(ghost->getCount(), 9);
	EXPECT_EQ(ghost->health.getCasualtyCount(battle::DamageProvenance::PHYSICAL_CREATURE), 1);
}
TEST_F(NewHorizonsSoulHarvestTest, DamagingSurvivorDoesNotHarvest)
{
	ASSERT_NO_FATAL_FAILURE(prepare(1, 1000));
	blockRetaliation(ghost);
	ASSERT_TRUE(attack(ghost, target->getPosition()));
	EXPECT_TRUE(target->alive());
	EXPECT_EQ(target->getCount(), 1000); // No individual casualty, not merely a surviving stack.
	EXPECT_EQ(ghost->getCount(), 1);
}
TEST_F(NewHorizonsSoulHarvestTest, CloneAndSummonVictimsDoNotQualify)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	target->cloned = true;
	EXPECT_FALSE(newHorizonsSoulHarvest::qualifies(*battle(), *ghost, *target, true));
	target->cloned = false;
	auto * summonedVictim = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"),
		BattleHex(rightHex + 3), 1);
	ASSERT_TRUE(summonedVictim->isSummoned());
	EXPECT_FALSE(summonedVictim->summoned);
	EXPECT_FALSE(newHorizonsSoulHarvest::qualifies(*battle(), *ghost, *summonedVictim, true));
	EXPECT_TRUE(newHorizonsSoulHarvest::qualifies(*battle(), *ghost, *target, true));
}
TEST_F(NewHorizonsSoulHarvestTest, CloneAndSummonActorsDoNotQualify)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ghost->cloned = true;
	EXPECT_FALSE(newHorizonsSoulHarvest::qualifies(*battle(), *ghost, *target, true));
	ghost->cloned = false;
	auto * summonedActor = addStack(BattleSide::ATTACKER, creatureByName("new-horizons:ghost"),
		BattleHex(leftHex - 3), 10);
	ASSERT_TRUE(summonedActor->isSummoned());
	EXPECT_FALSE(summonedActor->summoned);
	EXPECT_FALSE(newHorizonsSoulHarvest::qualifies(*battle(), *summonedActor, *target, true));
	EXPECT_TRUE(newHorizonsSoulHarvest::qualifies(*battle(), *ghost, *target, true));
}
TEST_F(NewHorizonsSoulHarvestTest, NonAttackDamageAndNativeRebirthNeverGrant)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	EXPECT_FALSE(newHorizonsSoulHarvest::qualifies(*battle(), *ghost, *target, false));
	int64_t damage = target->getAvailableHealth();
	target->damage(damage);
	EXPECT_EQ(newHorizonsSoulHarvest::casualtiesAfterHit(*target, 1, true), 0);
	EXPECT_FALSE(newHorizonsSoulHarvest::grant(*ghost, 0));
	EXPECT_EQ(ghost->getCount(), 10);
}
TEST_F(NewHorizonsSoulHarvestTest, HistoricalCaptureKeepsReclaimerAndHasNoGhostUpgrade)
{
	captured = false;
	ASSERT_NO_FATAL_FAILURE(prepare());
	EXPECT_TRUE(newHorizonsHeroes::reclamationSpecialtyRules(attackerSideHero->getPrimaryGrowthRules()));
	EXPECT_FALSE(newHorizonsHeroes::hasGhostUpgrade(*attackerSideHero));
	ASSERT_TRUE(attack(ghost, target->getPosition()));
	EXPECT_EQ(ghost->getCount(), 11);
}
TEST_F(NewHorizonsSoulHarvestTest, AcquiredGhostRetainsIntrinsicAbilityUnderAnotherHero)
{
	vidomina = false;
	ASSERT_NO_FATAL_FAILURE(prepare());
	EXPECT_FALSE(newHorizonsHeroes::hasGhostUpgrade(*attackerSideHero));
	ASSERT_TRUE(attack(ghost, target->getPosition()));
	EXPECT_EQ(ghost->getCount(), 11);
}
TEST_F(NewHorizonsSoulHarvestTest, UpgradeOfferUsesExistingCostAndPreservesFamilyLeadership)
{
	startGame();
	const auto wraith = creatureByName("core:wraith");
	const auto output = creatureByName("new-horizons:ghost");
	attackerSideHero->clearSlots();
	ASSERT_TRUE(attackerSideHero->setCreature(SlotID(0), wraith, 1));
	UpgradeInfo offer(wraith);
	gameState()->fillUpgradeInfo(attackerSideHero, SlotID(0), offer);
	ASSERT_TRUE(vstd::contains(offer.getAvailableUpgrades(), output));
	EXPECT_EQ(attackerSideHero->getLeadershipSlotCapacity(wraith)->requirement,
		attackerSideHero->getLeadershipSlotCapacity(output)->requirement);
	const auto before = gameState()->getPlayerState(PlayerColor(0))->resources;
	EXPECT_TRUE(gameHandler->upgradeCreature(attackerSideHero->id, SlotID(0), output));
	EXPECT_EQ(attackerSideHero->getCreature(SlotID(0))->getId(), output);
	EXPECT_EQ(gameState()->getPlayerState(PlayerColor(0))->resources, before);
}
TEST_F(NewHorizonsSoulHarvestTest, UnauthorizedHeroCannotRequestExclusiveUpgrade)
{
	vidomina = false;
	startGame();
	const auto output = creatureByName("new-horizons:ghost");
	attackerSideHero->clearSlots();
	ASSERT_TRUE(attackerSideHero->setCreature(SlotID(0), creatureByName("core:wraith"), 1));
	EXPECT_FALSE(gameHandler->upgradeCreature(attackerSideHero->id, SlotID(0), output));
	EXPECT_EQ(attackerSideHero->getCreature(SlotID(0))->getId(), creatureByName("core:wraith"));
}
TEST_F(NewHorizonsSoulHarvestTest, KilledTemporaryBodyDoesNotBecomeAnotherCasualty)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(attack(ghost, target->getPosition()));
	int64_t damage = ghost->getMaxHealth();
	ghost->damage(damage, false, battle::DamageProvenance::PHYSICAL_CREATURE);
	EXPECT_EQ(ghost->getCount(), 10);
	EXPECT_EQ(ghost->health.getResurrected(), 0);
	ghost->health.takeResurrected();
	EXPECT_EQ(ghost->getCount(), 10);
	EXPECT_EQ(ghost->health.getCasualtyCount(battle::DamageProvenance::PHYSICAL_CREATURE), 0);
}
TEST_F(NewHorizonsSoulHarvestTest, HostileCollateralGrantsPerVictimWithoutIncreasingThisBlowsDamage)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto * second = spare;
	ghost->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::TWO_HEX_ATTACK_BREATH, BonusSource::OTHER, 1, BonusSourceID()));
	ASSERT_TRUE(attack(ghost, target->getPosition()));
	EXPECT_FALSE(target->alive());
	EXPECT_FALSE(second->alive());
	EXPECT_EQ(ghost->getCount(), 12);
	EXPECT_EQ(ghost->health.getResurrected(), 2);
}
TEST_F(NewHorizonsSoulHarvestTest, EffectiveFriendlyMindControlledVictimCannotFuelHarvest)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	target->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::HYPNOTIZED, BonusSource::OTHER, 1, BonusSourceID()));
	EXPECT_EQ(battle()->battleGetOwner(ghost), battle()->battleGetOwner(target));
	EXPECT_FALSE(newHorizonsSoulHarvest::qualifies(*battle(), *ghost, *target, true));
}
TEST_F(NewHorizonsSoulHarvestTest, ExhaustedCountRepresentationDoesNotOverflow)
{
	ASSERT_NO_FATAL_FAILURE(prepare(std::numeric_limits<int32_t>::max()));
	int64_t damage = target->getAvailableHealth();
	target->damage(damage);
	EXPECT_FALSE(newHorizonsSoulHarvest::grant(*ghost, 1));
	EXPECT_EQ(ghost->getCount(), std::numeric_limits<int32_t>::max());
}
TEST_F(NewHorizonsSoulHarvestTest, DeadActorCannotHarvestOrResurrectItself)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	int64_t actorDamage = ghost->getAvailableHealth();
	ghost->damage(actorDamage);
	int64_t victimDamage = target->getAvailableHealth();
	target->damage(victimDamage);
	EXPECT_FALSE(newHorizonsSoulHarvest::grant(*ghost, 1));
	EXPECT_FALSE(ghost->alive());
}
#ifdef ENABLE_BATTLE_AI
TEST_F(NewHorizonsSoulHarvestTest, PreviewReplayAndLiveAgreeWithoutMutatingSibling)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	HarvestEnvironment env(gameState());
	auto callback = std::shared_ptr<CBattleInfoCallback>(battle(), [](CBattleInfoCallback *) {});
	auto model = std::make_shared<HypotheticBattle>(&env, callback);
	auto sibling = std::make_shared<HypotheticBattle>(&env, model);
	const auto live = ghost->save();
	DamageCache cache;
	cache.buildDamageCache(model, BattleSide::ATTACKER);
	const auto preview = AttackPossibility::evaluate(BattleAttackInfo(ghost, target, 0, false),
		ghost->getPosition(), cache, model);
	EXPECT_EQ(preview.attackerState->getCount(), 11);
	EXPECT_EQ(model->battleGetUnitByID(ghost->unitId())->getCount(), 10);
	EXPECT_EQ(sibling->battleGetUnitByID(ghost->unitId())->getCount(), 10);
	EXPECT_EQ(ghost->save(), live);
	auto replay = std::make_shared<HypotheticBattle>(&env, model);
	BattleExchangeVariant exchange;
	DamageCache replayCache;
	exchange.trackAttack(preview, replay, replayCache);
	EXPECT_EQ(replay->getForUpdate(ghost->unitId())->getCount(), 11);
	EXPECT_FALSE(replay->getForUpdate(target->unitId())->alive());
	ASSERT_TRUE(attack(ghost, target->getPosition()));
	EXPECT_EQ(replay->getForUpdate(ghost->unitId())->getCount(), ghost->getCount());
	EXPECT_EQ(replay->getForUpdate(ghost->unitId())->health.getResurrected(), ghost->health.getResurrected());
}
TEST_F(NewHorizonsSoulHarvestTest, FutureStrikeAppliesGrowthButReadOnlyProbeLeavesUnitsUnchanged)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	HarvestEnvironment env(gameState());
	auto callback = std::shared_ptr<CBattleInfoCallback>(battle(), [](CBattleInfoCallback *) {});
	auto model = std::make_shared<HypotheticBattle>(&env, callback);
	auto actor = model->getForUpdate(ghost->unitId());
	auto victim = model->getForUpdate(target->unitId());
	DamageCache cache;
	cache.buildDamageCache(model, BattleSide::ATTACKER);
	BattleExchangeVariant exchange;
	exchange.trackAttack(actor, victim, false, true, cache, model, true, false);
	EXPECT_EQ(actor->getCount(), 10);
	EXPECT_TRUE(victim->alive());
	exchange.trackAttack(actor, victim, false, true, cache, model, false, false);
	EXPECT_EQ(actor->getCount(), 11);
	EXPECT_FALSE(victim->alive());
	EXPECT_EQ(ghost->getCount(), 10);
}
#endif

TEST_F(NewHorizonsSoulHarvestTest, WorldSaveRoundTripKeepsNewIdentityAndHistoricalAbsence)
{
	startGame();
	const auto capture = attackerSideHero->getPrimaryGrowthRules();
	ASSERT_TRUE(capture.Struct().contains("ghostSpecialty"));
	ASSERT_FALSE(capture.Struct().contains("reclamationSpecialty"));
	CGameState restored;
	restored.preInit(LIBRARY);
	restored.loadFromMemory(gameState()->saveToMemory());
	const auto * hero = restored.getHero(attackerSideHero->id);
	ASSERT_NE(hero, nullptr);
	EXPECT_EQ(hero->getPrimaryGrowthRules(), capture);
	EXPECT_TRUE(newHorizonsHeroes::hasGhostUpgrade(*hero));
}

TEST_F(NewHorizonsSoulHarvestTest, HeroLessGhostStillHarvestsAsAnIntrinsicSpeciesAbility)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto savedHero = battle()->getSide(BattleSide::ATTACKER).heroID;
	battle()->getSide(BattleSide::ATTACKER).heroID = ObjectInstanceID();
	EXPECT_EQ(battle()->battleGetFightingHero(BattleSide::ATTACKER), nullptr);
	const bool qualified = newHorizonsSoulHarvest::qualifies(*battle(), *ghost, *target, true);
	battle()->getSide(BattleSide::ATTACKER).heroID = savedHero;
	EXPECT_TRUE(qualified);
	int64_t damage = target->getAvailableHealth();
	target->damage(damage);
	EXPECT_TRUE(newHorizonsSoulHarvest::grant(*ghost, 1));
	EXPECT_EQ(ghost->getCount(), 11);
}

TEST_F(NewHorizonsSoulHarvestTest, ProductionCasualtyCommitRemovesGrowthAndPreservesActualWorldArmyLoss)
{
	startGame();
	const auto species = creatureByName("new-horizons:ghost");
	attackerSideHero->clearSlots();
	defenderSideHero->clearSlots();
	ASSERT_TRUE(attackerSideHero->setCreature(SlotID(0), species, 5));
	ASSERT_TRUE(defenderSideHero->setCreature(SlotID(0), creatureByName("core:pikeman"), 1));
	ASSERT_TRUE(defenderSideHero->setCreature(SlotID(1), creatureByName("core:peasant"), 5));
	ASSERT_TRUE(attackerSideHero->getLeadershipSlotCapacity(species)->accepts(5));
	startBattle();
	const auto actors = battle()->battleGetStacksIf([](const CStack * stack)
	{
		return stack->unitSide() == BattleSide::ATTACKER && stack->unitSlot() == SlotID(0);
	});
	const auto victims = battle()->battleGetStacksIf([](const CStack * stack)
	{
		return stack->unitSide() == BattleSide::DEFENDER && stack->unitSlot() == SlotID(0);
	});
	ASSERT_EQ(actors.size(), 1);
	ASSERT_EQ(victims.size(), 1);
	ghost = battle()->getStack(actors.front()->unitId());
	target = battle()->getStack(victims.front()->unitId());
	ASSERT_NE(ghost->base, nullptr);
	ghost->setPosition(BattleHex(leftHex));
	target->setPosition(BattleHex(rightHex));
	forceMaximumDamage(ghost);
	blockRetaliation(target);
	beginCombat();
	int64_t realLoss = ghost->getMaxHealth();
	ghost->damage(realLoss, false, battle::DamageProvenance::PHYSICAL_CREATURE);
	ASSERT_TRUE(attack(ghost, target->getPosition()));
	ASSERT_EQ(ghost->getCount(), 5);
	ASSERT_EQ(ghost->health.getResurrected(), 1);
	EXPECT_EQ(attackerSideHero->getStackCount(SlotID(0)), 5);
	CasualtiesAfterBattle casualties(*battle(), BattleSide::ATTACKER);
	casualties.updateArmy(gameHandler.get());
	EXPECT_EQ(attackerSideHero->getStackCount(SlotID(0)), 4);
	EXPECT_EQ(attackerSideHero->getCreature(SlotID(0))->getId(), species);
}

TEST_F(NewHorizonsSoulHarvestTest, ActualSecondKillAfterResurrectionGrantsAnotherTemporaryGhost)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(attack(ghost, target->getPosition()));
	ASSERT_EQ(ghost->getCount(), 11);
	int64_t healing = target->getMaxHealth();
	target->heal(healing, EHealLevel::RESURRECT, EHealPower::PERMANENT);
	ASSERT_TRUE(target->alive());
	endRound();
	ASSERT_TRUE(attack(ghost, target->getPosition()));
	EXPECT_FALSE(target->alive());
	EXPECT_EQ(ghost->getCount(), 12);
	EXPECT_EQ(ghost->health.getResurrected(), 2);
}
TEST_F(NewHorizonsSoulHarvestTest, ActualNativeRebirthDefersHarvestUntilFinalDestruction)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	target->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::REBIRTH, BonusSource::OTHER, 100, BonusSourceID()));
	target->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::CASTS, BonusSource::OTHER, 1, BonusSourceID()));
	ASSERT_TRUE(target->canCast());
	ASSERT_TRUE(attack(ghost, target->getPosition()));
	EXPECT_TRUE(target->alive());
	EXPECT_EQ(ghost->getCount(), 10);
	ASSERT_FALSE(target->canCast());
	endRound();
	ASSERT_TRUE(attack(ghost, target->getPosition()));
	EXPECT_FALSE(target->alive());
	EXPECT_EQ(ghost->getCount(), 11);
}

TEST_F(NewHorizonsSoulHarvestTest, DuplicateVictimRecordsInOnePendingBlowCreditOnlyOneDestruction)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto destroyed = target->acquireState();
	int64_t damage = destroyed->getAvailableHealth();
	destroyed->damage(damage);
	ASSERT_FALSE(destroyed->alive());
	BattleStackAttacked hit;
	hit.attackerID = ghost->unitId();
	hit.stackAttacked = target->unitId();
	hit.flags = BattleStackAttacked::KILLED;
	hit.killedAmount = 1;
	hit.damageAmount = damage;
	hit.newState = UnitChanges(target->unitId(), UnitChanges::EOperation::UPDATE);
	hit.newState.data = destroyed->save();
	std::vector<BattleStackAttacked> duplicated{hit, hit};
	battleCreatureKillRewards::finishStrike(*battle(), *ghost, duplicated, false, false, true);
	EXPECT_EQ(ghost->getCount(), 11);
	EXPECT_EQ(ghost->health.getResurrected(), 1);
}
TEST_F(NewHorizonsSoulHarvestTest, ReapplyingCompletionForAlreadyDeadVictimCannotAwardAgain)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(attack(ghost, target->getPosition()));
	ASSERT_FALSE(target->alive());
	ASSERT_EQ(ghost->getCount(), 11);
	ASSERT_FALSE(server.attacks.empty());
	auto repeated = server.attacks.back().bsa;
	battleCreatureKillRewards::finishStrike(*battle(), *ghost, repeated, false, false, true);
	EXPECT_EQ(ghost->getCount(), 11);
	EXPECT_EQ(ghost->health.getResurrected(), 1);
}

TEST_F(NewHorizonsSoulHarvestTest, UndeadImmunityStillRejectsForgetfulnessWithoutVulnerabilityOrb)
{
	prepareForgetfulness = true;
	useVulnerabilityOrb = false;
	ASSERT_NO_FATAL_FAILURE(prepare());
	EXPECT_FALSE(castOn(defenderSideHero, SpellID::FORGETFULNESS, ghost));
	EXPECT_EQ(newHorizonsCreatureAbilitySuppression::suppressionLevel(*ghost), 0);
	ASSERT_TRUE(attack(ghost, target->getPosition()));
	EXPECT_EQ(ghost->getCount(), 11);
}
TEST_F(NewHorizonsSoulHarvestTest, LegalForgetfulnessSuppressesHarvestAndNaturalExpiryRestoresIt)
{
	prepareForgetfulness = true;
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(castOn(defenderSideHero, SpellID::FORGETFULNESS, ghost));
	ASSERT_EQ(newHorizonsCreatureAbilitySuppression::suppressionLevel(*ghost), 1);
#ifdef ENABLE_BATTLE_AI
	const auto suppressedHP = harvestForecast(gameState(), battle(), ghost, target, ghost, 10);
#endif
	ASSERT_TRUE(attack(ghost, target->getPosition()));
	EXPECT_FALSE(target->alive());
	EXPECT_EQ(ghost->getCount(), 10);
	EXPECT_EQ(ghost->health.getResurrected(), 0);
#ifdef ENABLE_BATTLE_AI
	EXPECT_EQ(ghost->getAvailableHealth(), suppressedHP);
#endif
	for(int round = 0; round < 4 && newHorizonsCreatureAbilitySuppression::suppressionLevel(*ghost); ++round)
		endRound();
	ASSERT_EQ(newHorizonsCreatureAbilitySuppression::suppressionLevel(*ghost), 0);
	target = spare;
	target->setPosition(BattleHex(rightHex));
	blockRetaliation(target);
#ifdef ENABLE_BATTLE_AI
	const auto restoredHP = harvestForecast(gameState(), battle(), ghost, target, ghost, 11);
#endif
	ASSERT_TRUE(attack(ghost, target->getPosition()));
	EXPECT_FALSE(target->alive());
	EXPECT_EQ(ghost->getCount(), 11);
	EXPECT_EQ(ghost->health.getResurrected(), 1);
#ifdef ENABLE_BATTLE_AI
	EXPECT_EQ(ghost->getAvailableHealth(), restoredHP);
#endif
}
TEST_F(NewHorizonsSoulHarvestTest, RevertedBattleFormRetainsIdentityThroughCodecAndHarvestsAgain)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ghost->beginBattleForm(creatureByName("new-horizons:ghost"), 2);
	ASSERT_TRUE(ghost->hasBattleForm());
	EXPECT_FALSE(newHorizonsSoulHarvest::qualifies(*battle(), *ghost, *target, true));
	ghost->endBattleForm();
	ASSERT_FALSE(ghost->hasBattleForm());
	ASSERT_TRUE(ghost->hasBattleFormState());
	const auto saved = ghost->save();
	auto restored = ghost->acquireState();
	restored->load(saved);
	EXPECT_FALSE(restored->hasBattleForm());
	EXPECT_TRUE(restored->hasBattleFormState());
	EXPECT_EQ(restored->save(), saved);
	ghost->load(restored->save());
#ifdef ENABLE_BATTLE_AI
	const auto projectedHP = harvestForecast(gameState(), battle(), ghost, target, ghost, 11);
#endif
	ASSERT_TRUE(attack(ghost, target->getPosition()));
	EXPECT_FALSE(target->alive());
	EXPECT_EQ(ghost->getCount(), 11);
	EXPECT_EQ(ghost->health.getResurrected(), 1);
#ifdef ENABLE_BATTLE_AI
	EXPECT_EQ(ghost->getAvailableHealth(), projectedHP);
#endif
}
TEST_F(NewHorizonsSoulHarvestTest, BulwarkPreemptiveGhostKillAgreesInPreviewSelectedReplayAndNative)
{
	attackerBulwark = MasteryLevel::EXPERT;
	ASSERT_NO_FATAL_FAILURE(prepare());
	ghost->defending = true;
	ASSERT_FALSE(ghost->bulwarkPreemptiveUsed);
#ifdef ENABLE_BATTLE_AI
	const auto projectedHP = harvestForecast(gameState(), battle(), target, ghost, ghost, 11);
#endif
	ASSERT_TRUE(attack(target, ghost->getPosition()));
	EXPECT_FALSE(target->alive());
	EXPECT_TRUE(ghost->bulwarkPreemptiveUsed);
	EXPECT_EQ(ghost->getCount(), 11);
	EXPECT_EQ(ghost->health.getResurrected(), 1);
#ifdef ENABLE_BATTLE_AI
	EXPECT_EQ(ghost->getAvailableHealth(), projectedHP);
#endif
}
TEST_F(NewHorizonsSoulHarvestTest, SoulHarvestGainCannotMaskBulwarkReflectedLossInSelectedReplay)
{
	defenderBulwark = MasteryLevel::ADVANCED;
	ASSERT_NO_FATAL_FAILURE(prepare());
	target->defending = true;
	target->bulwarkPreemptiveUsed = true;
	const auto before = ghost->getAvailableHealth();
#ifdef ENABLE_BATTLE_AI
	const auto projectedHP = harvestForecast(gameState(), battle(), ghost, target, ghost, 11);
	EXPECT_LT(projectedHP, before + ghost->getMaxHealth());
	EXPECT_GT(projectedHP, before);
#endif
	ASSERT_TRUE(attack(ghost, target->getPosition()));
	EXPECT_FALSE(target->alive());
	EXPECT_EQ(ghost->getCount(), 11);
	EXPECT_EQ(ghost->health.getResurrected(), 1);
	EXPECT_LT(ghost->getAvailableHealth(), before + ghost->getMaxHealth());
	EXPECT_GT(ghost->getAvailableHealth(), before);
#ifdef ENABLE_BATTLE_AI
	EXPECT_EQ(ghost->getAvailableHealth(), projectedHP);
#endif
}

TEST_F(NewHorizonsSoulHarvestTest, SuppressedGhostKeepsZeroGrowthAndOrdinaryReflectedDamageAccounting)
{
	prepareForgetfulness = true;
	defenderBulwark = MasteryLevel::ADVANCED;
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(castOn(defenderSideHero, SpellID::FORGETFULNESS, ghost));
	ASSERT_EQ(newHorizonsCreatureAbilitySuppression::suppressionLevel(*ghost), 1);
	target->defending = true;
	target->bulwarkPreemptiveUsed = true;
	const auto before = ghost->getAvailableHealth();
#ifdef ENABLE_BATTLE_AI
	const auto projectedHP = harvestForecast(gameState(), battle(), ghost, target, ghost, 10);
	EXPECT_LT(projectedHP, before);
#endif
	ASSERT_TRUE(attack(ghost, target->getPosition()));
	EXPECT_FALSE(target->alive());
	EXPECT_EQ(ghost->getCount(), 10);
	EXPECT_EQ(ghost->health.getResurrected(), 0);
	EXPECT_LT(ghost->getAvailableHealth(), before);
#ifdef ENABLE_BATTLE_AI
	EXPECT_EQ(ghost->getAvailableHealth(), projectedHP);
#endif
}

TEST_F(NewHorizonsSoulHarvestTest, DestroyingThreeCreaturesAddsThreeTemporaryGhosts)
{
	ASSERT_NO_FATAL_FAILURE(prepare(10, 3));
	ASSERT_TRUE(attack(ghost, target->getPosition()));
	ASSERT_FALSE(target->alive());
	EXPECT_EQ(ghost->getCount(), 13);
	EXPECT_EQ(ghost->health.getResurrected(), 3);
	ghost->health.takeResurrected();
	EXPECT_EQ(ghost->getCount(), 10);
}
TEST_F(NewHorizonsSoulHarvestTest, WoundedGhostHarvestsThreeWholeBodiesWithoutFillingOriginalWound)
{
	ASSERT_NO_FATAL_FAILURE(prepare(10, 3));
	int64_t wound = 3;
	ghost->damage(wound);
	const auto before = ghost->getAvailableHealth();
	ASSERT_TRUE(attack(ghost, target->getPosition()));
	EXPECT_EQ(ghost->getCount(), 13);
	EXPECT_EQ(ghost->getAvailableHealth(), before + 3LL * ghost->getMaxHealth());
	ghost->health.takeResurrected();
	EXPECT_EQ(ghost->getCount(), 10);
	EXPECT_EQ(ghost->getAvailableHealth(), before);
}
TEST_F(NewHorizonsSoulHarvestTest, PartialCasualtiesWithUnusedRebirthAgreeInPreviewReplayAndNative)
{
	ASSERT_NO_FATAL_FAILURE(prepare(10, 1000));
	blockRetaliation(ghost);
	target->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::REBIRTH, BonusSource::OTHER, 100, BonusSourceID()));
	target->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::CASTS, BonusSource::OTHER, 1, BonusSourceID()));
	ASSERT_TRUE(target->canCast());
	const auto damage = battle()->calculateDmgRange(BattleAttackInfo(ghost, target, 0, false));
	const int casualties = static_cast<int>(damage.kills.max);
	ASSERT_GT(casualties, 1);
	ASSERT_LT(casualties, 1000);
#ifdef ENABLE_BATTLE_AI
	const auto projectedHP = harvestForecast(gameState(), battle(), ghost, target, ghost, 10 + casualties);
#endif
	ASSERT_TRUE(attack(ghost, target->getPosition()));
	ASSERT_TRUE(target->alive());
	EXPECT_EQ(target->getCount(), 1000 - casualties);
	EXPECT_TRUE(target->canCast()); // No whole-stack Rebirth was triggered.
	EXPECT_EQ(ghost->getCount(), 10 + casualties);
	EXPECT_EQ(ghost->health.getResurrected(), casualties);
#ifdef ENABLE_BATTLE_AI
	EXPECT_EQ(ghost->getAvailableHealth(), projectedHP);
#endif
}
TEST_F(NewHorizonsSoulHarvestTest, ReplayedPartialCompletionCannotCreditSameAppliedCasualtiesAgain)
{
	ASSERT_NO_FATAL_FAILURE(prepare(10, 1000));
	blockRetaliation(ghost);
	ASSERT_TRUE(attack(ghost, target->getPosition()));
	ASSERT_TRUE(target->alive());
	ASSERT_GT(ghost->getCount(), 10);
	ASSERT_FALSE(server.attacks.empty());
	const auto count = ghost->getCount();
	const auto temporary = ghost->health.getResurrected();
	battleCreatureKillRewards::finishStrike(*battle(), *ghost, server.attacks.back().bsa, false, false, true);
	EXPECT_EQ(ghost->getCount(), count);
	EXPECT_EQ(ghost->health.getResurrected(), temporary);
}
TEST_F(NewHorizonsSoulHarvestTest, ResurrectedThreeCreatureStackCanFuelThreeNewCasualtiesAgain)
{
	ASSERT_NO_FATAL_FAILURE(prepare(10, 3));
	ASSERT_TRUE(attack(ghost, target->getPosition()));
	ASSERT_EQ(ghost->getCount(), 13);
	int64_t healing = 3LL * target->getMaxHealth();
	target->heal(healing, EHealLevel::RESURRECT, EHealPower::PERMANENT);
	ASSERT_EQ(target->getCount(), 3);
	endRound();
	ASSERT_TRUE(attack(ghost, target->getPosition()));
	EXPECT_FALSE(target->alive());
	EXPECT_EQ(ghost->getCount(), 16);
	EXPECT_EQ(ghost->health.getResurrected(), 6);
}
TEST_F(NewHorizonsSoulHarvestTest, ExistingVersionOneCaptureLoadsUnchangedThenHarvestsNextThreeCasualties)
{
	ASSERT_NO_FATAL_FAILURE(prepare(10, 1000));
	const auto heroRules = attackerSideHero->getPrimaryGrowthRules();
	ASSERT_EQ(heroRules["ghostSpecialty"]["version"].Integer(), 1);
	ASSERT_EQ(attackerSideHero->getStackCount(SlotID(0)), 10);
	ASSERT_EQ(attackerSideHero->getCreature(SlotID(0))->getId(), creatureByName("new-horizons:ghost"));
	CGameState restored;
	restored.preInit(LIBRARY);
	ASSERT_NO_THROW(restored.loadFromMemory(gameState()->saveToMemory()));
	// Adventure replay snapshots intentionally omit active battles. Verify the
	// saved world army, then use its captured rules for a new ordinary battle.
	ASSERT_TRUE(restored.currentBattles.empty());
	auto * loadedAttacker = restored.getHero(attackerSideHero->id);
	auto * loadedDefender = restored.getHero(defenderSideHero->id);
	ASSERT_NE(loadedAttacker, nullptr);
	ASSERT_NE(loadedDefender, nullptr);
	ASSERT_EQ(loadedAttacker->getPrimaryGrowthRules(), heroRules);
	ASSERT_EQ(loadedAttacker->getStackCount(SlotID(0)), 10);
	ASSERT_EQ(loadedAttacker->getCreature(SlotID(0))->getId(), creatureByName("new-horizons:ghost"));
	const int3 tile(4, 4, 0);
	const auto layout = BattleLayout::createDefaultLayout(restored, loadedAttacker, loadedDefender);
	auto loadedBattle = BattleInfo::setupBattle(&restored, tile, restored.getTile(tile)->getTerrainID(),
		battle()->getBattlefieldType(), {loadedAttacker, loadedDefender},
		{loadedAttacker, loadedDefender}, layout, nullptr);
	ASSERT_NO_THROW(loadedBattle->localInit());
	const auto findOriginal = [&](BattleSide side)
	{
		const auto found = std::ranges::find_if(loadedBattle->stacks, [=](const auto & stack)
			{ return stack->unitSide() == side && stack->unitSlot() == SlotID(0); });
		return found == loadedBattle->stacks.end() ? nullptr : found->get();
	};
	auto * loadedGhost = findOriginal(BattleSide::ATTACKER);
	auto * loadedTarget = findOriginal(BattleSide::DEFENDER);
	ASSERT_NE(loadedGhost, nullptr);
	ASSERT_NE(loadedTarget, nullptr);
	ASSERT_EQ(loadedGhost->getCount(), 10); // No retroactive recalculation.
	BattleStackAttacked hit;
	hit.attackerID = loadedGhost->unitId();
	hit.stackAttacked = loadedTarget->unitId();
	hit.damageAmount = 3LL * loadedTarget->getMaxHealth();
	CRandomGenerator random(19);
	loadedTarget->prepareAttacked(hit, random);
	ASSERT_EQ(hit.killedAmount, 3u);
	ASSERT_FALSE(hit.killed());
	battleCreatureKillRewards::finishStrike(*loadedBattle, *loadedGhost, {hit}, false, false, true);
	EXPECT_EQ(loadedGhost->getCount(), 13);
	EXPECT_EQ(loadedGhost->health.getResurrected(), 3);
}
TEST_F(NewHorizonsSoulHarvestTest, CombinedCasualtiesRejectCountOverflowWithoutPartialGrowth)
{
	ASSERT_NO_FATAL_FAILURE(prepare(std::numeric_limits<int32_t>::max() - 1));
	const auto before = ghost->getAvailableHealth();
	EXPECT_FALSE(newHorizonsSoulHarvest::grant(*ghost, 2));
	EXPECT_FALSE(newHorizonsSoulHarvest::grant(*ghost, std::numeric_limits<int64_t>::max()));
	EXPECT_EQ(ghost->getCount(), std::numeric_limits<int32_t>::max() - 1);
	EXPECT_EQ(ghost->getAvailableHealth(), before);
}
TEST_F(NewHorizonsSoulHarvestTest, GhostCounterHarvestCreditsThreeCreaturesExactlyOnce)
{
	ASSERT_NO_FATAL_FAILURE(prepare(1000, 3));
	const auto blocks = target->getBonuses(Selector::type()(BonusType::BLOCKS_RETALIATION));
	ASSERT_EQ(blocks->size(), 1);
	target->removeBonus((*blocks)[0]);
	ASSERT_FALSE(target->hasBonusOfType(BonusType::BLOCKS_RETALIATION));
	forceMaximumDamage(target);
	const auto damage = battle()->calculateDmgRange(BattleAttackInfo(target, ghost, 0, false));
	const int lost = static_cast<int>(damage.kills.max);
	ASSERT_LT(lost, 1000);
#ifdef ENABLE_BATTLE_AI
	const auto projectedHP = harvestForecast(gameState(), battle(), target, ghost, ghost, 1000 - lost + 3);
#endif
	const auto activationsBefore = server.stackActivations.size();
	ASSERT_TRUE(attack(target, ghost->getPosition()));
	EXPECT_FALSE(target->alive());
	EXPECT_EQ(ghost->getCount(), 1000 - lost + 3);
	EXPECT_EQ(ghost->health.getResurrected(), 3);
	const auto counter = std::ranges::find_if(server.attacks, [&](const auto & packet)
		{ return packet.counter() && packet.stackAttacking == ghost->unitId(); });
	ASSERT_NE(counter, server.attacks.end());
	const auto snapshot = std::ranges::find_if(counter->attackerChanges.changedStacks, [&](const auto & change)
		{ return change.id == ghost->unitId(); });
	ASSERT_NE(snapshot, counter->attackerChanges.changedStacks.end());
	auto completedCounter = ghost->acquireState();
	completedCounter->load(snapshot->data);
	EXPECT_EQ(completedCounter->getCount(), 1000 - lost + 3);
	EXPECT_EQ(completedCounter->health.getResurrected(), 3);
#ifdef ENABLE_BATTLE_AI
	EXPECT_EQ(completedCounter->getAvailableHealth(), projectedHP);
#endif
	// attack() also advances turn flow: Ghost regeneration heals its wounded
	// front creature before the next ordinary activation, after this receipt.
	const auto missingFrontHP = ghost->getMaxHealth() - completedCounter->getFirstHPleft();
	ASSERT_GT(missingFrontHP, 0);
	ASSERT_GE(ghost->valOfBonuses(BonusType::HP_REGENERATION), missingFrontHP);
	EXPECT_TRUE(std::any_of(server.stackActivations.begin() + activationsBefore,
		server.stackActivations.end(), [&](const auto & packet)
			{ return packet.stack == ghost->unitId() && packet.reason == BattleUnitTurnReason::TURN_QUEUE; }));
	EXPECT_EQ(ghost->getAvailableHealth(), completedCounter->getAvailableHealth() + missingFrontHP);
}

TEST_F(NewHorizonsSoulHarvestTest, MetadataIdentityRetainsGhostAndRejectsCorePhysicalActor)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_EQ(ghost->unitType()->getJsonKey(), "new-horizons:ghost");
	EXPECT_EQ(ghost->unitType()->getId(), ghost->creatureId());
	EXPECT_TRUE(newHorizonsSoulHarvest::qualifies(*battle(), *ghost, *target, true));
	EXPECT_FALSE(newHorizonsSoulHarvest::qualifies(*battle(), *target, *ghost, true));
	ghost->beginBattleForm(creatureByName("core:pikeman"), 1);
	EXPECT_EQ(ghost->unitType()->getId(), ghost->creatureId());
	EXPECT_FALSE(newHorizonsSoulHarvest::qualifies(*battle(), *ghost, *target, true));
	ghost->endBattleForm();
	EXPECT_EQ(ghost->unitType()->getId(), ghost->creatureId());
	EXPECT_TRUE(newHorizonsSoulHarvest::qualifies(*battle(), *ghost, *target, true));
}
