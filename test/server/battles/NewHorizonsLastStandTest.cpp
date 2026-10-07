/*
 * NewHorizonsLastStandTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of the license available in license.txt file, in main folder
 */
#include "StdInc.h"

#include "HeroCommandFixture.h"
#include "../../hero/NewHorizonsHeroRulesFixture.h"

#include "../../../lib/GameConstants.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/CSkillHandler.h"
#include "../../../lib/CStack.h"
#include "../../../lib/battle/BattleAttackInfo.h"
#include "../../../lib/battle/BattleHexArray.h"
#include "../../../lib/battle/CUnitState.h"
#include "../../../lib/battle/NewHorizonsArmorer.h"
#include "../../../lib/gameState/CGameState.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/mapping/CMap.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/networkPacks/BattleChanges.h"
#include "../../../lib/networkPacks/PacksForClientBattle.h"
#include "../../../lib/networkPacks/SetStackEffect.h"
#include "../../SpellPointTestUtils.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../lib/serializer/ESerializationVersion.h"
#include "../../../server/CGameHandler.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
constexpr std::string_view armorerSkillId = newHorizonsArmorer::SKILL_ID;
constexpr std::string_view pavisePerkId = "new-horizons:armorer.pavise";
constexpr std::string_view veteranPerkId = "new-horizons:armorer.veteran";
constexpr std::string_view lastStandPerkId = newHorizonsArmorer::LAST_STAND_PERK_ID;

bool activatePerk(JsonNode & rules, std::string_view perkId)
{
	auto & perks = rules["skills"][std::string(armorerSkillId)]["perks"].Vector();
	const auto found = std::find_if(perks.begin(), perks.end(), [perkId](const JsonNode & perk)
	{
		return perk["id"].String() == perkId;
	});
	if(found == perks.end())
		return false;

	(*found)["effect"]["status"].String() = "active";
	return true;
}

class NewHorizonsLastStandTest : public HeroCommandFixture
{
protected:
	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";
	}

	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, testHeroRules());

		JsonNode perkRules(JsonPath::builtin("config/newHorizonsPerks"));
		if(!activatePerk(perkRules, lastStandPerkId))
			throw std::runtime_error("Missing Last Stand from the New Horizons Armorer registry");
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, std::move(perkRules));
	}

	void acceptPerkThroughOffer(CGHeroInstance * hero, std::string_view perkId,
		MasteryLevel::Type requiredRank)
	{
		const auto rankLookup = [hero](const std::string & skillId)
		{
			return hero->getPerkSkillRank(skillId);
		};

		for(uint64_t seed = 0; seed < 4096; ++seed)
		{
			const auto offers = hero->getPerkState().prepareOffer(rankLookup, seed);
			const auto selected = std::find_if(offers.begin(), offers.end(), [perkId](const auto & offer)
			{
				return offer.selection.skillId == armorerSkillId
					&& offer.selection.perkId == perkId;
			});
			if(selected == offers.end())
				continue;

			ASSERT_EQ(selected->requiredRank, static_cast<int>(requiredRank));
			gameHandler->levelUpHero(hero, offers,
				static_cast<size_t>(std::distance(offers.begin(), selected)), seed, false);
			ASSERT_TRUE(hero->hasActivePerk(std::string(armorerSkillId), std::string(perkId)));
			return;
		}

		FAIL() << "No legal Armorer offer contained " << perkId;
	}

	void selectLastStand(CGHeroInstance * hero)
	{
		const int decoded = SecondarySkill::decode(std::string(armorerSkillId));
		ASSERT_GE(decoded, 0);
		const SecondarySkill armorer(decoded);

		hero->setSecSkillLevel(armorer, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		acceptPerkThroughOffer(hero, pavisePerkId, MasteryLevel::BASIC);
		hero->setSecSkillLevel(armorer, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
		acceptPerkThroughOffer(hero, veteranPerkId, MasteryLevel::ADVANCED);
		hero->setSecSkillLevel(armorer, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
		acceptPerkThroughOffer(hero, lastStandPerkId, MasteryLevel::EXPERT);
	}

	void removeDeployedUnits()
	{
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		if(!remove.changedStacks.empty())
			gameHandler->sendAndApply(remove);
	}

	void grantGuardianSpirit(CStack * stack, int64_t points, int16_t rounds = 2)
	{
		Bonus marker(BonusDuration::N_TURNS, BonusType::GUARDIAN_SPIRIT,
			BonusSource::SPELL_EFFECT, static_cast<int32_t>(points), BonusSourceID(SpellID(SpellID::HASTE)));
		marker.turnsRemain = rounds;
		SetStackEffect effects;
		effects.battleID = BattleID(0);
		effects.toAdd.emplace_back(stack->unitId(), std::vector<Bonus>{marker});
		gameHandler->sendAndApply(effects);
	}

	void reduceToHealth(CStack * stack, int64_t desiredHealth)
	{
		auto state = stack->acquireState();
		const auto healthBefore = state->getAvailableHealth();
		ASSERT_GT(healthBefore, desiredHealth);
		int64_t damage = healthBefore - desiredHealth;
		state->damage(damage, false);
		BattleUnitsChanged update;
		update.battleID = BattleID(0);
		UnitChanges change(stack->unitId(), UnitChanges::EOperation::UPDATE);
		change.data = state->save();
		change.healthDelta = state->getAvailableHealth() - healthBefore;
		update.changedStacks.push_back(std::move(change));
		gameHandler->sendAndApply(update);
	}

	void updateUnitState(CStack * stack, battle::CUnitState & state)
	{
		BattleUnitsChanged update;
		update.battleID = BattleID(0);
		UnitChanges change(stack->unitId(), UnitChanges::EOperation::UPDATE);
		change.data = state.save();
		update.changedStacks.push_back(std::move(change));
		gameHandler->sendAndApply(update);
	}

	std::size_t queueActivations(const CStack * stack) const
	{
		return static_cast<std::size_t>(std::ranges::count_if(server.stackActivations,
			[stack](const BattleSetActiveStack & activation)
		{
			return activation.stack == stack->unitId()
				&& activation.reason == BattleUnitTurnReason::TURN_QUEUE;
		}));
	}

	bool advanceUntilActivation(CStack * stack, std::size_t previousActivations)
	{
		for(int attempt = 0; attempt < 48; ++attempt)
		{
			const auto * active = battle()->battleActiveUnit();
			if(!active)
				return false;
			if(active->unitId() == stack->unitId() && queueActivations(stack) > previousActivations)
				return true;

			const auto player = battle()->sideToPlayer(active->unitSide());
			if(!gameHandler->battles->makePlayerBattleAction(BattleID(0), player,
				BattleAction::makeDefend(active)))
				return false;
		}
		return false;
	}

	const BattleStackAttacked * findHit(const CStack * attacker, const CStack * target) const
	{
		const auto attack = std::find_if(server.attacks.rbegin(), server.attacks.rend(), [attacker](const BattleAttack & candidate)
		{
			return candidate.stackAttacking == attacker->unitId() && !candidate.counter();
		});
		if(attack == server.attacks.rend())
			return nullptr;

		const auto hit = std::find_if(attack->bsa.begin(), attack->bsa.end(), [target](const BattleStackAttacked & candidate)
		{
			return candidate.stackAttacked == target->unitId();
		});
		return hit == attack->bsa.end() ? nullptr : &*hit;
	}

	bool attackWith(CStack * attacker, CStack * target)
	{
		battle()->activeStack = attacker->unitId();
		return attack(attacker, target->getPosition());
	}
};
}

TEST_F(NewHorizonsLastStandTest, DamageCapResolvesGuardianBeforeLethality)
{
	const auto absorbed = newHorizonsArmorer::resolveLastStandDamage(40, 50, 2, 20, true, false);
	EXPECT_FALSE(absorbed.triggered);
	EXPECT_EQ(absorbed.damageToApply, 40);

	const auto overflow = newHorizonsArmorer::resolveLastStandDamage(200, 40, 2, 100, true, false);
	EXPECT_TRUE(overflow.triggered);
	EXPECT_EQ(overflow.damageToApply, 139)
		<< "Guardian absorbs 40 first, then only 99 ordinary HP are applied";

	const auto expiredGuardian = newHorizonsArmorer::resolveLastStandDamage(100, 80, 0, 100, true, false);
	EXPECT_TRUE(expiredGuardian.triggered)
		<< "An expired Guardian Spirit pool does not absorb damage before the lethal check";
	EXPECT_EQ(expiredGuardian.damageToApply, 99);

	const auto spent = newHorizonsArmorer::resolveLastStandDamage(200, 40, 2, 100, true, true);
	EXPECT_FALSE(spent.triggered);
	EXPECT_EQ(spent.damageToApply, 200);
	EXPECT_FALSE(newHorizonsArmorer::resolveLastStandDamage(200, 0, 0, 100, false, false).triggered);
}

TEST_F(NewHorizonsLastStandTest, AcceptedPhysicalLethalHitLeavesOneAtOneHpAndDefending)
{
	startGame();
	selectLastStand(defenderSideHero);
	startBattle();
	removeDeployedUnits();
	auto * attacker = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(leftHex), 100);
	auto * target = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex), 2);
	auto * fasterDefender = addStack(BattleSide::DEFENDER, creatureByName("core:griffin"), BattleHex(11, 8), 1);
	ASSERT_NE(attacker, nullptr);
	ASSERT_NE(target, nullptr);
	ASSERT_NE(fasterDefender, nullptr);
	forceMaximumDamage(attacker);
	blockRetaliation(target);
	beginCombat();
	const auto targetHealthBefore = target->getAvailableHealth();
	const auto expectedStance = newHorizonsArmorer::buildDefendStance(target);

	ASSERT_TRUE(attackWith(attacker, target));
	const auto * hit = findHit(attacker, target);
	ASSERT_NE(hit, nullptr);
	EXPECT_EQ(hit->armorerLastStandSide, BattleSide::DEFENDER);
	EXPECT_FALSE(hit->armorerLastStandEndsActivation);
	EXPECT_EQ(hit->damageAmount, targetHealthBefore - 1)
		<< "The attack event itself caps lethal damage at one ordinary HP";
	EXPECT_EQ(hit->newState.data["state"]["health"]["firstHPleft"].Integer(), 1);
	EXPECT_EQ(hit->newState.data["state"]["health"]["fullUnits"].Integer(), 0);
	EXPECT_TRUE(hit->newState.data["state"]["armorerLastStandDefending"].Bool())
		<< "Inspect the accepted hit snapshot: the next activation may already have reset Defend";
	EXPECT_TRUE(target->alive());
	EXPECT_EQ(target->getCount(), 1);
	EXPECT_TRUE(target->defended());
	EXPECT_TRUE(target->hasBonusOfType(BonusType::UNIT_DEFENDING))
		<< "Last Stand applies the ordinary Defend bonus payload";
	EXPECT_EQ(target->acquireState()->defensiveStanceMeleeBonus, expectedStance.meleeDefenseBonus);
	EXPECT_EQ(target->acquireState()->defensiveStanceRangedBonus, expectedStance.rangedDefenseBonus);
	EXPECT_TRUE(battle()->armorerLastStandUsed(BattleSide::DEFENDER));
	EXPECT_FALSE(battle()->armorerLastStandUsed(BattleSide::ATTACKER));
}

TEST_F(NewHorizonsLastStandTest, GuardianAbsorptionDoesNotSpendAllowanceBeforeLethalOverflow)
{
	startGame();
	selectLastStand(defenderSideHero);
	startBattle();
	removeDeployedUnits();
	const BattleHex targetPosition(8, 5);
	const auto & adjacent = BattleHexArray::getNeighbouringTiles(targetPosition);
	ASSERT_GE(adjacent.size(), 2u);
	auto * weakAttacker = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), adjacent[0], 1);
	auto * strongAttacker = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), adjacent[1], 100);
	auto * target = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), targetPosition, 2);
	ASSERT_NE(weakAttacker, nullptr);
	ASSERT_NE(strongAttacker, nullptr);
	ASSERT_NE(target, nullptr);
	forceMaximumDamage(weakAttacker);
	forceMaximumDamage(strongAttacker);
	blockRetaliation(target);
	beginCombat();

	const auto weakMaximum = battle()->calculateDmgRange(
		BattleAttackInfo(weakAttacker, target, 0, false)).damage.max;
	ASSERT_GT(weakMaximum, 0);
	const auto targetHealthBefore = target->getAvailableHealth();
	grantGuardianSpirit(target, weakMaximum + 5);
	ASSERT_TRUE(attackWith(weakAttacker, target));
	EXPECT_EQ(target->getAvailableHealth(), targetHealthBefore);
	EXPECT_EQ(target->guardianSpiritHitPoints, 5);
	EXPECT_FALSE(battle()->armorerLastStandUsed(BattleSide::DEFENDER));
	ASSERT_GT(battle()->calculateDmgRange(
		BattleAttackInfo(strongAttacker, target, 0, false)).damage.max,
		target->getAvailableHealth() + target->guardianSpiritHitPoints);

	ASSERT_TRUE(attackWith(strongAttacker, target));
	ASSERT_NE(findHit(strongAttacker, target), nullptr);
	EXPECT_EQ(findHit(strongAttacker, target)->armorerLastStandSide, BattleSide::DEFENDER);
	EXPECT_EQ(target->guardianSpiritHitPoints, 0);
	EXPECT_TRUE(target->alive());
	EXPECT_EQ(target->getCount(), 1);
	EXPECT_EQ(target->getAvailableHealth(), 1);
	EXPECT_TRUE(target->defended());
	EXPECT_TRUE(battle()->armorerLastStandUsed(BattleSide::DEFENDER));
}

TEST_F(NewHorizonsLastStandTest, OneHpTargetStillTriggersWithAZeroDamagePacket)
{
	startGame();
	selectLastStand(defenderSideHero);
	startBattle();
	removeDeployedUnits();
	auto * attacker = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(leftHex), 100);
	auto * target = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex), 1);
	ASSERT_NE(attacker, nullptr);
	ASSERT_NE(target, nullptr);
	forceMaximumDamage(attacker);
	reduceToHealth(target, 1);
	blockRetaliation(target);
	beginCombat();

	ASSERT_TRUE(attackWith(attacker, target));
	const auto * hit = findHit(attacker, target);
	ASSERT_NE(hit, nullptr);
	EXPECT_EQ(hit->damageAmount, 0);
	EXPECT_EQ(hit->armorerLastStandSide, BattleSide::DEFENDER);
	EXPECT_TRUE(hit->newState.data["state"]["armorerLastStandDefending"].Bool());
	EXPECT_EQ(hit->newState.data["state"]["health"]["firstHPleft"].Integer(), 1);
	EXPECT_EQ(hit->newState.data["state"]["health"]["fullUnits"].Integer(), 0);
	EXPECT_TRUE(target->alive());
	EXPECT_EQ(target->getCount(), 1);
	EXPECT_EQ(target->getAvailableHealth(), 1);
	EXPECT_TRUE(battle()->armorerLastStandUsed(BattleSide::DEFENDER));
}

TEST_F(NewHorizonsLastStandTest, AcceptedSpellDamageDoesNotSpendPhysicalAttackAllowance)
{
	startGame();
	selectLastStand(defenderSideHero);
	giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
	attackerSideHero->addSpellToSpellbook(SpellID::MAGIC_ARROW);
	setTestSpellPointTotal(attackerSideHero, 1000);
	startBattle();
	removeDeployedUnits();
	auto * friendly = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(leftHex), 1);
	auto * target = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex), 1);
	auto * reserve = addStack(BattleSide::DEFENDER, creatureByName("core:zombie"), BattleHex(13, 5), 100);
	ASSERT_NE(friendly, nullptr);
	ASSERT_NE(target, nullptr);
	ASSERT_NE(reserve, nullptr);
	reduceToHealth(target, 1);
	beginCombat();

	ASSERT_TRUE(castOn(attackerSideHero, SpellID::MAGIC_ARROW, target));
	EXPECT_FALSE(target->alive()) << "A lethal spell must not be converted into Last Stand's physical-attack rescue";
	EXPECT_FALSE(target->defended());
	EXPECT_FALSE(battle()->armorerLastStandUsed(BattleSide::DEFENDER));
}

TEST_F(NewHorizonsLastStandTest, ClonePhantomAndWarMachineTargetsAreIneligible)
{
	startGame();
	selectLastStand(defenderSideHero);
	giveArtifact(defenderSideHero, ArtifactID::BALLISTA, ArtifactPosition::MACH1);
	startBattle();
	auto * clone = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(8, 5), 5);
	auto * phantom = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(10, 5), 5);
	ASSERT_NE(clone, nullptr);
	ASSERT_NE(phantom, nullptr);

	const auto initialUnits = battle()->battleGetAllUnits(false);
	const auto ordinary = std::find_if(initialUnits.begin(), initialUnits.end(), [](const battle::Unit * unit)
		{
			return unit && unit->unitSide() == BattleSide::DEFENDER
				&& unit->unitSlot() != SlotID::WAR_MACHINES_SLOT && !unit->isClone();
		});
	ASSERT_NE(ordinary, initialUnits.end());
	EXPECT_TRUE(newHorizonsArmorer::canTriggerLastStand(defenderSideHero, *ordinary));

	auto cloneState = clone->acquireState();
	cloneState->cloned = true;
	UnitChanges cloneUpdate(clone->unitId(), UnitChanges::EOperation::UPDATE);
	cloneUpdate.data = cloneState->save();
	BattleUnitsChanged changedClone;
	changedClone.battleID = BattleID(0);
	changedClone.changedStacks.push_back(std::move(cloneUpdate));
	gameHandler->sendAndApply(changedClone);
	ASSERT_TRUE(clone->isClone());
	EXPECT_FALSE(newHorizonsArmorer::canTriggerLastStand(defenderSideHero, clone));

	auto phantomState = phantom->acquireState();
	phantomState->summoned = true;
	phantomState->initializePhantomProfile(phantom->getAvailableHealth(), 2);
	UnitChanges phantomUpdate(phantom->unitId(), UnitChanges::EOperation::UPDATE);
	phantomUpdate.data = phantomState->save();
	BattleUnitsChanged changedPhantom;
	changedPhantom.battleID = BattleID(0);
	changedPhantom.changedStacks.push_back(std::move(phantomUpdate));
	gameHandler->sendAndApply(changedPhantom);
	ASSERT_GT(phantom->getPhantomInitialIntegrity(), 0);
	EXPECT_FALSE(newHorizonsArmorer::canTriggerLastStand(defenderSideHero, phantom));

	const auto machines = battle()->battleGetAllUnits(false);
	const auto machine = std::find_if(machines.begin(), machines.end(), [](const battle::Unit * unit)
	{
		return unit && unit->unitSide() == BattleSide::DEFENDER
			&& unit->unitSlot() == SlotID::WAR_MACHINES_SLOT;
	});
	ASSERT_NE(machine, machines.end());
	EXPECT_FALSE(newHorizonsArmorer::canTriggerLastStand(defenderSideHero, *machine));
	EXPECT_FALSE(newHorizonsArmorer::isEligiblePhysicalAttack(*machine, true, false));

	auto * ordinaryStack = battle()->getStack(static_cast<int>((*ordinary)->unitId()));
	ASSERT_NE(ordinaryStack, nullptr);
	ordinaryStack->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::SPELL_LIKE_ATTACK, BonusSource::OTHER, 1, BonusSourceID()));
	ASSERT_TRUE(ordinaryStack->hasBonusOfType(BonusType::SPELL_LIKE_ATTACK));
	EXPECT_FALSE(newHorizonsArmorer::isEligiblePhysicalAttack(*ordinary, false, false));
	EXPECT_TRUE(newHorizonsArmorer::isEligiblePhysicalAttack(*ordinary, true, false))
		<< "A melee attack by a creature that also has a spell-like ranged ability is still a physical attack";
	EXPECT_FALSE(newHorizonsArmorer::isEligiblePhysicalAttack(*ordinary, true, true))
		<< "The creature's spell-like ranged attack is not a physical creature attack";
}

TEST_F(NewHorizonsLastStandTest, EachSideHasItsOwnOncePerCombatAllowance)
{
	startGame();
	selectLastStand(attackerSideHero);
	selectLastStand(defenderSideHero);
	startBattle();
	removeDeployedUnits();
	auto * attackerOne = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(7, 5), 100);
	auto * targetOne = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(8, 5), 2);
	auto * attackerTwo = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(10, 5), 100);
	auto * targetTwo = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(11, 5), 2);
	auto * attackerTarget = addStack(BattleSide::ATTACKER, creatureByName("core:peasant"), BattleHex(7, 8), 2);
	auto * defenderAttacker = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(6, 8), 100);
	ASSERT_NE(attackerOne, nullptr);
	ASSERT_NE(targetOne, nullptr);
	ASSERT_NE(attackerTwo, nullptr);
	ASSERT_NE(targetTwo, nullptr);
	ASSERT_NE(attackerTarget, nullptr);
	ASSERT_NE(defenderAttacker, nullptr);
	ASSERT_TRUE(BattleHexArray::getNeighbouringTiles(targetOne->getPosition()).contains(attackerOne->getPosition()));
	ASSERT_TRUE(BattleHexArray::getNeighbouringTiles(targetTwo->getPosition()).contains(attackerTwo->getPosition()));
	ASSERT_TRUE(BattleHexArray::getNeighbouringTiles(attackerTarget->getPosition()).contains(defenderAttacker->getPosition()));
	forceMaximumDamage(attackerOne);
	forceMaximumDamage(attackerTwo);
	forceMaximumDamage(defenderAttacker);
	blockRetaliation(targetOne);
	blockRetaliation(targetTwo);
	blockRetaliation(attackerTarget);
	beginCombat();

	ASSERT_TRUE(attackWith(attackerOne, targetOne));
	ASSERT_NE(findHit(attackerOne, targetOne), nullptr);
	EXPECT_TRUE(targetOne->alive());
	EXPECT_EQ(targetOne->getAvailableHealth(), 1);
	EXPECT_TRUE(battle()->armorerLastStandUsed(BattleSide::DEFENDER));
	EXPECT_FALSE(battle()->armorerLastStandUsed(BattleSide::ATTACKER));

	ASSERT_TRUE(attackWith(attackerTwo, targetTwo));
	EXPECT_FALSE(targetTwo->alive()) << "The same side cannot save another stack in this combat";
	EXPECT_TRUE(battle()->armorerLastStandUsed(BattleSide::DEFENDER));
	EXPECT_FALSE(battle()->armorerLastStandUsed(BattleSide::ATTACKER));

	ASSERT_TRUE(attackWith(defenderAttacker, attackerTarget));
	ASSERT_NE(findHit(defenderAttacker, attackerTarget), nullptr);
	EXPECT_TRUE(attackerTarget->alive());
	EXPECT_EQ(attackerTarget->getAvailableHealth(), 1)
		<< "The opposing hero's independent allowance remains available";
	EXPECT_TRUE(battle()->armorerLastStandUsed(BattleSide::ATTACKER));
	EXPECT_TRUE(attackerTarget->defended());
}

TEST_F(NewHorizonsLastStandTest, LethalRetaliationEndsAnExtraAttackActivation)
{
	startGame();
	selectLastStand(attackerSideHero);
	const auto angel = creatureByName("core:angel");
	attackerSideHero->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::HERO_GRANTS_ATTACKS, BonusSource::OTHER, 1, BonusSourceID(), BonusSubtypeID(angel)));
	startBattle();
	removeDeployedUnits();
	auto * attacker = addStack(BattleSide::ATTACKER, angel, BattleHex(leftHex), 1);
	auto * defender = addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), BattleHex(rightHex), 1000);
	auto * reserve = addStack(BattleSide::ATTACKER, creatureByName("core:zombie"), BattleHex(3, 9), 1);
	ASSERT_NE(attacker, nullptr);
	ASSERT_NE(defender, nullptr);
	ASSERT_NE(reserve, nullptr);
	forceMaximumDamage(attacker);
	forceMaximumDamage(defender);

	// The counterattack must be lethal without Last Stand; its non-consuming
	// mitigation therefore exercises the in-action activation-stop marker.
	auto state = attacker->acquireState();
	const auto healthBefore = state->getAvailableHealth();
	ASSERT_GT(healthBefore, 1);
	int64_t preparationDamage = healthBefore - 1;
	state->damage(preparationDamage, false);
	BattleUnitsChanged prepare;
	prepare.battleID = BattleID(0);
	UnitChanges prepared(attacker->unitId(), UnitChanges::EOperation::UPDATE);
	prepared.data = state->save();
	prepared.healthDelta = state->getAvailableHealth() - healthBefore;
	prepare.changedStacks.push_back(std::move(prepared));
	gameHandler->sendAndApply(prepare);
	beginCombat();
	ASSERT_GT(battle()->calculateDmgRange(BattleAttackInfo(defender, attacker, 0, false)).damage.max, 1);
	const auto previousActivations = queueActivations(attacker);

	const auto firstAttackIndex = server.attacks.size();
	ASSERT_TRUE(attackWith(attacker, defender));
	EXPECT_TRUE(attacker->alive());
	EXPECT_EQ(attacker->getAvailableHealth(), 1);
	EXPECT_TRUE(attacker->defended());
	EXPECT_TRUE(attacker->acquireState()->armorerLastStandEndedActivation);
	EXPECT_TRUE(battle()->armorerLastStandUsed(BattleSide::ATTACKER));

	const auto attackerPrimaryStrikes = std::ranges::count_if(
		server.attacks.begin() + static_cast<std::ptrdiff_t>(firstAttackIndex), server.attacks.end(),
		[attacker](const BattleAttack & event)
		{
			return event.stackAttacking == attacker->unitId() && !event.counter();
		});
	EXPECT_EQ(attackerPrimaryStrikes, 1)
		<< "The lethal retaliation prevents the stack's eligible second attack from resolving";
	const auto retaliation = std::find_if(server.attacks.begin() + static_cast<std::ptrdiff_t>(firstAttackIndex),
		server.attacks.end(), [attacker](const BattleAttack & event)
		{
			return event.stackAttacking != attacker->unitId() && event.counter();
		});
	ASSERT_NE(retaliation, server.attacks.end());
	const auto savedAttackerHit = std::find_if(retaliation->bsa.begin(), retaliation->bsa.end(), [attacker](const auto & hit)
	{
		return hit.stackAttacked == attacker->unitId();
	});
	ASSERT_NE(savedAttackerHit, retaliation->bsa.end());
	EXPECT_EQ(savedAttackerHit->armorerLastStandSide, BattleSide::ATTACKER);
	EXPECT_TRUE(savedAttackerHit->armorerLastStandEndsActivation);
	EXPECT_TRUE(savedAttackerHit->newState.data["state"]["armorerLastStandDefending"].Bool());
	EXPECT_TRUE(savedAttackerHit->newState.data["state"]["armorerLastStandEndedActivation"].Bool());

	// A later ordinary enemy hit before the next activation carries the marker,
	// but the side-wide allowance prevents a second rescue or a duplicate stop.
	grantGuardianSpirit(attacker, 10000, 5);
	const auto followupIndex = server.attacks.size();
	ASSERT_TRUE(attackWith(defender, attacker));
	EXPECT_TRUE(attacker->alive());
	EXPECT_EQ(attacker->getAvailableHealth(), 1);
	EXPECT_GT(attacker->guardianSpiritHitPoints, 0);
	EXPECT_TRUE(battle()->armorerLastStandUsed(BattleSide::ATTACKER));
	EXPECT_TRUE(attacker->acquireState()->armorerLastStandEndedActivation);
	const auto followupHit = std::find_if(server.attacks.begin() + static_cast<std::ptrdiff_t>(followupIndex),
		server.attacks.end(), [defender](const BattleAttack & event)
		{
			return event.stackAttacking == defender->unitId() && !event.counter();
		});
	ASSERT_NE(followupHit, server.attacks.end());
	const auto exhaustedSideHit = std::find_if(followupHit->bsa.begin(), followupHit->bsa.end(), [attacker](const auto & hit)
	{
		return hit.stackAttacked == attacker->unitId();
	});
	ASSERT_NE(exhaustedSideHit, followupHit->bsa.end());
	EXPECT_EQ(exhaustedSideHit->armorerLastStandSide, BattleSide::NONE);
	EXPECT_FALSE(exhaustedSideHit->armorerLastStandEndsActivation);
	EXPECT_TRUE(exhaustedSideHit->newState.data["state"]["armorerLastStandEndedActivation"].Bool())
		<< "An already-spent side keeps the activation marker on a later accepted hit";

	ASSERT_TRUE(advanceUntilActivation(attacker, previousActivations));
	EXPECT_FALSE(attacker->acquireState()->armorerLastStandEndedActivation);
	EXPECT_FALSE(attacker->acquireState()->armorerLastStandDefending);
	EXPECT_TRUE(battle()->armorerLastStandUsed(BattleSide::ATTACKER))
		<< "The activation marker clears, but the once-per-combat award remains spent";
}

TEST_F(NewHorizonsLastStandTest, AttackAndBattleDescriptorsPreserveOrRejectTransientState)
{
	startGame();
	selectLastStand(defenderSideHero);
	startBattle();
	removeDeployedUnits();
	auto * attacker = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(leftHex), 100);
	auto * target = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex), 2);
	ASSERT_NE(attacker, nullptr);
	ASSERT_NE(target, nullptr);
	forceMaximumDamage(attacker);
	blockRetaliation(target);
	beginCombat();
	ASSERT_TRUE(attackWith(attacker, target));
	const auto * hit = findHit(attacker, target);
	ASSERT_NE(hit, nullptr);

	CMemorySerializer currentAttack;
	currentAttack.oser.version = ESerializationVersion::CURRENT;
	currentAttack.iser.version = ESerializationVersion::CURRENT;
	currentAttack.oser & *hit;
	currentAttack.iser.cb = gameState().get();
	BattleStackAttacked decodedHit;
	currentAttack.iser & decodedHit;
	EXPECT_EQ(decodedHit.armorerLastStandSide, BattleSide::DEFENDER);
	EXPECT_FALSE(decodedHit.armorerLastStandEndsActivation);

	CMemorySerializer oldAttack;
	oldAttack.oser.version = ESerializationVersion::NEW_HORIZONS_BATTLEFIELD_MASTERY;
	EXPECT_THROW(oldAttack.oser & *hit, std::runtime_error);
	EXPECT_TRUE(oldAttack.extractBuffer().empty());
	EXPECT_FALSE(hit->armorerLastStandEndsActivation);
}

TEST_F(NewHorizonsLastStandTest, BattleDescriptorRejectsEachTransientMarker)
{
	startGame();
	startBattle();
	removeDeployedUnits();
	auto * attacker = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(leftHex), 100);
	auto * target = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex), 2);
	ASSERT_NE(attacker, nullptr);
	ASSERT_NE(target, nullptr);
	beginCombat();

	// The current BattleInfo binary descriptor refuses to omit either passive-Defend
	// provenance or the activation-ending marker; these remain UnitChanges state.
	auto targetState = target->acquireState();
	targetState->armorerLastStandDefending = true;
	updateUnitState(target, *targetState);
	EXPECT_TRUE(battle()->hasArmorerLastStandTransientUnitState());
	EXPECT_THROW(CMemorySerializer::deepCopy(*battle(), gameState().get()), std::runtime_error)
		<< "Passive-Defend provenance must not be silently omitted from the compact descriptor";

	targetState = target->acquireState();
	targetState->armorerLastStandDefending = false;
	updateUnitState(target, *targetState);
	EXPECT_FALSE(battle()->hasArmorerLastStandTransientUnitState());
	EXPECT_NO_THROW(static_cast<void>(CMemorySerializer::deepCopy(*battle(), gameState().get())));

	auto attackerState = attacker->acquireState();
	attackerState->armorerLastStandEndedActivation = true;
	updateUnitState(attacker, *attackerState);
	EXPECT_TRUE(battle()->hasArmorerLastStandTransientUnitState());
	EXPECT_THROW(CMemorySerializer::deepCopy(*battle(), gameState().get()), std::runtime_error)
		<< "The compact descriptor must also reject the separate activation-ending marker";
}

TEST_F(NewHorizonsLastStandTest, HistoricalSideUseRoundTripsAndOldWriterCannotDropIt)
{
	startGame();
	selectLastStand(defenderSideHero);
	startBattle();
	beginCombat();
	battle()->consumeArmorerLastStand(BattleSide::DEFENDER);
	EXPECT_TRUE(battle()->armorerLastStandUsed(BattleSide::DEFENDER));
	EXPECT_FALSE(battle()->armorerLastStandUsed(BattleSide::ATTACKER));

	const auto saved = CMemorySerializer::deepCopy(*battle(), gameState().get());
	ASSERT_NE(saved, nullptr);
	EXPECT_TRUE(saved->armorerLastStandUsed(BattleSide::DEFENDER));
	EXPECT_FALSE(saved->armorerLastStandUsed(BattleSide::ATTACKER));

	CMemorySerializer older;
	older.oser.version = ESerializationVersion::NEW_HORIZONS_BATTLEFIELD_MASTERY;
	EXPECT_THROW(older.oser & *battle(), std::runtime_error);
	EXPECT_TRUE(older.extractBuffer().empty());
}
