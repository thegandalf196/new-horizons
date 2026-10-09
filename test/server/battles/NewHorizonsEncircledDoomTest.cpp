/*
 * NewHorizonsEncircledDoomTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "BattleTestFixture.h"
#include "../../../AI/BattleAI/StackWithBonuses.h"
#include "../../../lib/CSkillHandler.h"
#include "../../../lib/GameConstants.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/battle/BattleAction.h"
#include "../../../lib/battle/BattleAttackInfo.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/battle/NewHorizonsShroud.h"
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/entities/hero/CHero.h"
#include "../../../lib/entities/hero/CHeroClass.h"
#include "../../../lib/entities/hero/CHeroHandler.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../server/CGameHandler.h"
#include "../../../server/battles/BattleProcessor.h"
#include <vcmi/Environment.h>

namespace
{
class EncircledDoomEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit EncircledDoomEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class NewHorizonsEncircledDoomTest : public BattleTestFixture
{
protected:
	CStack * shooter = nullptr;
	CStack * target = nullptr;
	CStack * first = nullptr;
	CStack * second = nullptr;

	void SetUp() override
	{
		BattleTestFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires New Horizons";
	}

	void select(CGHeroInstance * hero, const std::string & skillId, const std::string & perkId,
		MasteryLevel::Type rank)
	{
		hero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode(skillId)), rank, ChangeValueMode::ABSOLUTE);
		const auto lookup = [hero](const std::string & id) { return hero->getPerkSkillRank(id); };
		for(uint64_t seed = 0; seed < 4096; ++seed)
		{
			const auto offers = hero->getPerkState().prepareOffer(lookup, seed);
			for(size_t choice = 0; choice < offers.size(); ++choice)
				if(offers[choice].selection.skillId == skillId && offers[choice].selection.perkId == perkId)
				{
					gameHandler->levelUpHero(hero, offers, choice, seed, false);
					ASSERT_TRUE(hero->hasActivePerk(skillId, perkId));
					return;
				}
		}
		FAIL() << "No legal offer for " << perkId;
	}

	void prepare(bool selected = true)
	{
		HeroTypeID dungeon = HeroTypeID::NONE;
		for(const auto id : LIBRARY->heroh->getDefaultAllowed())
		{
			const auto * hero = dynamic_cast<const CHero *>(id.toHeroType());
			if(hero && hero->heroClass && hero->heroClass->faction == FactionID::DUNGEON)
			{
				dungeon = id;
				break;
			}
		}
		ASSERT_NE(dungeon, HeroTypeID::NONE);
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).playerActive(PlayerColor(0)).playerActive(PlayerColor(1))
			.hero({5, 5, 0}, dungeon, PlayerColor(0)).heroGarrison({{CreatureID(0), 1}})
			.hero({7, 7, 0}, HeroTypeID(0), PlayerColor(1)).heroGarrison({{CreatureID(0), 1}});
		startWithMap(std::move(builder));
		server.gameState = gameState();
		gameHandler = std::make_shared<CGameHandler>(server, gameState());
		gameHandler->randomizer->setSeed(seed);
		attackerSideHero = findHeroByOwner(PlayerColor(0));
		defenderSideHero = findHeroByOwner(PlayerColor(1));
		for(auto * hero : {attackerSideHero, defenderSideHero})
		{
			ASSERT_NE(hero, nullptr);
			for(const auto & bonus : hero->getHeroType()->specialty)
				hero->removeBonus(bonus);
			for(int i = 0; i < LIBRARY->skillh->size(); ++i)
				hero->setSecSkillLevel(SecondarySkill(i), 0, ChangeValueMode::ABSOLUTE);
			for(auto skill : {PrimarySkill::ATTACK, PrimarySkill::DEFENSE, PrimarySkill::SPELL_POWER, PrimarySkill::KNOWLEDGE})
				hero->setPrimarySkill(skill, 0, ChangeValueMode::ABSOLUTE);
		}
		select(attackerSideHero, std::string(newHorizonsShroud::SKILL_ID),
			std::string(newHorizonsShroud::BACKSTAB_PERK_ID), MasteryLevel::BASIC);
		attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode(std::string(newHorizonsShroud::SKILL_ID))),
			MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
		select(attackerSideHero, std::string(newHorizonsShroud::SKILL_ID),
			std::string(newHorizonsShroud::NO_ESCAPE_PERK_ID), MasteryLevel::ADVANCED);
		attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode(std::string(newHorizonsShroud::SKILL_ID))),
			MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
		if(selected)
			select(attackerSideHero, std::string(newHorizonsShroud::SKILL_ID),
				std::string(newHorizonsShroud::ENCIRCLED_DOOM_PERK_ID), MasteryLevel::EXPERT);
		startBattle();
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
		shooter = addStack(BattleSide::ATTACKER, creatureByName("core:archer"), BattleHex(70), 100);
		target = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(94), 1000);
	}

	CStack * contact(size_t ordinal)
	{
		size_t available = 0;
		for(const auto hex : target->getSurroundingHexes())
			if(hex.isAvailable() && !battle()->battleGetStackByPos(hex))
			{
				if(available++ == ordinal)
					return addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), hex, 100);
			}
		ADD_FAILURE() << "No legal adjacent contact";
		return nullptr;
	}

	void surround()
	{
		first = contact(0);
		second = contact(0);
		ASSERT_NE(first, nullptr);
		ASSERT_NE(second, nullptr);
	}

	void addFlanker()
	{
		for(const auto hex : target->getSurroundingHexes())
		{
			if(!hex.isAvailable() || battle()->battleGetStackByPos(hex))
				continue;
			auto * candidate = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), hex, 100);
			ASSERT_NE(candidate, nullptr);
			if(battle()->battleIsShroudFlankingAttack(BattleAttackInfo(candidate, target, 0, false)))
			{
				first = candidate;
				return;
			}
			BattleUnitsChanged remove;
			remove.battleID = BattleID(0);
			remove.changedStacks.emplace_back(candidate->unitId(), UnitChanges::EOperation::REMOVE);
			gameHandler->sendAndApply(remove);
		}
		FAIL() << "No rear-facing legal melee flank";
	}

	BattleAttackInfo flank() const { return BattleAttackInfo(first, target, 0, false); }
};
}


TEST(NewHorizonsEncircledDoomRulesTest, OnlyAdditionalDistinctDirectionsAddTenPercent)
{
	for(int count : {-10, 0, 1})
		EXPECT_EQ(newHorizonsShroud::encircledDoomDamagePercent(count), 0);
	EXPECT_EQ(newHorizonsShroud::encircledDoomDamagePercent(2), 10);
	EXPECT_EQ(newHorizonsShroud::encircledDoomDamagePercent(3), 20);
	EXPECT_EQ(newHorizonsShroud::encircledDoomDamagePercent(6), 50);
}

TEST_F(NewHorizonsEncircledDoomTest, ActualExpertSelectionNeedsAdditionalCurrentSide)
{
	prepare();
	EXPECT_TRUE(newHorizonsShroud::hasEncircledDoom(attackerSideHero));
	addFlanker();
	ASSERT_TRUE(battle()->battleIsShroudFlankingAttack(flank()));
	ASSERT_EQ(battle()->battleShroudMeleeContactSideCount(flank()), 1);
	EXPECT_EQ(battle()->battleEncircledDoomDamagePercent(flank()), 0);
	second = contact(0);
	ASSERT_NE(second, nullptr);
	EXPECT_EQ(battle()->battleShroudMeleeContactSideCount(flank()), 2);
	EXPECT_EQ(battle()->battleEncircledDoomDamagePercent(flank()), 10);
	attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode(std::string(newHorizonsShroud::SKILL_ID))),
		MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
	EXPECT_EQ(battle()->battleEncircledDoomDamagePercent(flank()), 0);
}

TEST_F(NewHorizonsEncircledDoomTest, UnselectedAndNonFlankCategoriesNeverGainPremium)
{
	prepare(false);
	addFlanker();
	second = contact(0);
	ASSERT_NE(second, nullptr);
	EXPECT_EQ(battle()->battleEncircledDoomDamagePercent(flank()), 0);
	select(attackerSideHero, std::string(newHorizonsShroud::SKILL_ID),
		std::string(newHorizonsShroud::ENCIRCLED_DOOM_PERK_ID), MasteryLevel::EXPERT);
	ASSERT_EQ(battle()->battleEncircledDoomDamagePercent(flank()), 10);
	auto attack = flank();
	attack.shooting = true;
	EXPECT_EQ(battle()->battleEncircledDoomDamagePercent(attack), 0);
	attack = flank();
	attack.physicalDamage = false;
	EXPECT_EQ(battle()->battleEncircledDoomDamagePercent(attack), 0);
	attack = flank();
	attack.secondaryAttack = true;
	EXPECT_EQ(battle()->battleEncircledDoomDamagePercent(attack), 0);
	attack = flank();
	attack.retaliation = true;
	EXPECT_EQ(battle()->battleEncircledDoomDamagePercent(attack), 10);
	bool checkedFront = false;
	for(const auto hex : target->getSurroundingHexes())
	{
		BattleAttackInfo front = flank();
		front.attackerPos = hex;
		if(!hex.isAvailable() || battle()->battleIsShroudFlankingAttack(front))
			continue;
		EXPECT_EQ(battle()->battleEncircledDoomDamagePercent(front), 0);
		checkedFront = true;
		break;
	}
	EXPECT_TRUE(checkedFront);
}

TEST_F(NewHorizonsEncircledDoomTest, DetachedContactMovementChangesActualDamageOnlyInBranch)
{
	prepare();
	addFlanker();
	second = contact(0);
	ASSERT_NE(second, nullptr);
	EncircledDoomEnvironment environment(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	auto parent = std::make_shared<HypotheticBattle>(&environment, callback);
	auto branch = std::make_shared<HypotheticBattle>(&environment, parent);
	BattleAttackInfo attack(branch->getForUpdate(first->unitId()).get(),
		branch->getForUpdate(target->unitId()).get(), 0, false);
	const auto enhanced = branch->calculateDmgRange(attack).damage;
	ASSERT_EQ(branch->battleEncircledDoomDamagePercent(attack), 10);
	branch->getForUpdate(second->unitId())->setPosition(BattleHex(20));
	EXPECT_EQ(branch->battleEncircledDoomDamagePercent(attack), 0);
	const auto ordinary = branch->calculateDmgRange(attack).damage;
	EXPECT_GT(enhanced.min, ordinary.min);
	EXPECT_GT(enhanced.max, ordinary.max);
	BattleAttackInfo parentAttack(parent->getForUpdate(first->unitId()).get(),
		parent->getForUpdate(target->unitId()).get(), 0, false);
	EXPECT_EQ(parent->battleEncircledDoomDamagePercent(parentAttack), 10);
	EXPECT_EQ(battle()->battleEncircledDoomDamagePercent(flank()), 10);
}

TEST_F(NewHorizonsEncircledDoomTest, HostileControlAndFormationFightingRemoveQualification)
{
	prepare();
	addFlanker();
	second = contact(0);
	ASSERT_NE(second, nullptr);
	auto control = std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::HYPNOTIZED, BonusSource::OTHER, 1, BonusSourceID());
	second->addNewBonus(control);
	ASSERT_NE(battle()->battleGetOwner(second), battle()->battleGetOwner(first));
	EXPECT_EQ(battle()->battleEncircledDoomDamagePercent(flank()), 0);
	second->removeBonus(control);
	EXPECT_EQ(battle()->battleEncircledDoomDamagePercent(flank()), 10);
	select(defenderSideHero, "new-horizons:armorer", "new-horizons:armorer.pavise", MasteryLevel::BASIC);
	select(defenderSideHero, "new-horizons:armorer", "new-horizons:armorer.formationFighting", MasteryLevel::ADVANCED);
	CStack * support = nullptr;
	for(const auto hex : target->getSurroundingHexes())
		if(hex.isAvailable() && !battle()->battleGetStackByPos(hex))
		{
			support = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), hex, 100);
			break;
		}
	ASSERT_NE(support, nullptr);
	ASSERT_TRUE(battle()->battleHasFormationFightingProtection(target));
	EXPECT_EQ(battle()->battleEncircledDoomDamagePercent(flank()), 0);
}

TEST_F(NewHorizonsEncircledDoomTest, AcceptedFlankingMeleeUsesSharedEnhancedPrediction)
{
	prepare();
	addFlanker();
	second = contact(0);
	ASSERT_NE(second, nullptr);
	beginCombat();
	BattleSetActiveStack active;
	active.battleID = BattleID(0);
	active.stack = first->unitId();
	active.reason = BattleUnitTurnReason::TURN_QUEUE;
	gameHandler->sendAndApply(active);
	ASSERT_TRUE(battle()->battleIsShroudFlankingAttack(flank()));
	ASSERT_EQ(battle()->battleEncircledDoomDamagePercent(flank()), 10);
	const auto predicted = battle()->calculateDmgRange(flank()).damage;
	const auto previousHP = target->getAvailableHealth();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeMeleeAttack(first, target->getPosition(), first->getPosition())));
	const auto actual = previousHP - target->getAvailableHealth();
	EXPECT_GE(actual, predicted.min);
	EXPECT_LE(actual, predicted.max);
	EXPECT_GT(actual, 0);
}
