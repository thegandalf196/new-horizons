/*
 * NewHorizonsBloodScentTest.cpp, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "BattleTestFixture.h"

#include "../../../AI/BattleAI/StackWithBonuses.h"
#include "../../../lib/GameConstants.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/battle/BattleAttackInfo.h"
#include "../../../lib/battle/BattleInfo.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/battle/NewHorizonsBloodrage.h"
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/gameState/CGameState.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/networkPacks/PacksForClientBattle.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../lib/serializer/ESerializationVersion.h"
#include "../../../server/CGameHandler.h"

namespace
{
constexpr std::string_view BLOODRAGE_SKILL = "new-horizons:bloodrage";
constexpr std::string_view BLOOD_SCENT = "new-horizons:bloodrage.bloodScent";

class BloodScentEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit BloodScentEnvironment(std::shared_ptr<CGameState> state_) : state(std::move(state_)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class NewHorizonsBloodScentTest : public BattleTestFixture
{
protected:
	void SetUp() override
	{
		BattleTestFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";
	}

	void mapLoaded(CMap * loaded) override
	{
		BattleTestFixture::mapLoaded(loaded);
		JsonNode rules(JsonPath::builtin("config/newHorizonsPerks"));
		int activated = 0;
		for(auto & perk : rules["skills"][std::string(BLOODRAGE_SKILL)]["perks"].Vector())
		{
			if(perk["id"].String() != BLOOD_SCENT)
				continue;
			const auto status = perk["effect"]["status"].String();
			if(status != "planned" && status != "active")
				throw std::runtime_error("Blood Scent must be planned or active in canonical content");
			perk["effect"]["status"].String() = "active";
			++activated;
		}
		if(activated != 1)
			throw std::runtime_error("Blood Scent is missing from the New Horizons perk registry");
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, std::move(rules));
	}

	SecondarySkill bloodrageSkill() const
	{
		const int decoded = SecondarySkill::decode(std::string(BLOODRAGE_SKILL));
		if(decoded < 0)
			throw std::runtime_error("Missing New Horizons Bloodrage skill");
		return SecondarySkill(decoded);
	}

	void selectBloodScent(CGHeroInstance * hero)
	{
		hero->setSecSkillLevel(bloodrageSkill(), MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		const auto rankLookup = [hero](const std::string & skill)
		{
			return hero->getPerkSkillRank(skill);
		};
		for(uint64_t seed = 0; seed < 4096; ++seed)
		{
			const auto offers = hero->getPerkState().prepareOffer(rankLookup, seed);
			for(size_t choice = 0; choice < offers.size(); ++choice)
			{
				if(offers[choice].selection.skillId == BLOODRAGE_SKILL
					&& offers[choice].selection.perkId == BLOOD_SCENT)
				{
					gameHandler->levelUpHero(hero, offers, choice, seed, false);
					ASSERT_TRUE(hero->hasActivePerk(std::string(BLOODRAGE_SKILL), std::string(BLOOD_SCENT)));
					return;
				}
			}
		}
		FAIL() << "No legal Basic Blood Scent offer";
	}

	void setAvailableHealth(CStack * stack, int64_t desiredHealth)
	{
		auto state = stack->acquireState();
		ASSERT_GE(desiredHealth, 0);
		ASSERT_LE(desiredHealth, state->getAvailableHealth());
		int64_t damage = state->getAvailableHealth() - desiredHealth;
		state->damage(damage);
		BattleUnitsChanged update;
		update.battleID = BattleID(0);
		UnitChanges change(stack->unitId(), UnitChanges::EOperation::UPDATE);
		change.data = state->save();
		change.healthDelta = -damage;
		update.changedStacks.push_back(std::move(change));
		gameHandler->sendAndApply(update);
	}

	auto persistentBloodrageReference(const BattleAttackInfo & attack, BattleSide side)
	{
		auto & snapshot = battle()->getSide(side);
		const int32_t originalPercent = snapshot.bloodrageDamagePercent;
		const int32_t originalIncrement = snapshot.bloodrageLowHealthIncrement;
		snapshot.bloodrageLowHealthIncrement = 0;
		snapshot.bloodrageDamagePercent = std::min(snapshot.bloodrageCapPercent,
			originalPercent + originalIncrement);
		const auto reference = battle()->calculateDmgRange(attack).damage;
		snapshot.bloodrageDamagePercent = originalPercent;
		snapshot.bloodrageLowHealthIncrement = originalIncrement;
		return reference;
	}

};

class NewHorizonsBloodScentLegacySerializationTest : public NewHorizonsBloodScentTest
{
protected:
	void mapLoaded(CMap * loaded) override
	{
		NewHorizonsBloodScentTest::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::COMBAT_HERO_COMMANDS, JsonNode());
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, JsonNode());
		auto magicRules = LIBRARY->settingsHandler->getValue(EGameSettings::MAGIC_NEW_HORIZONS);
		magicRules["warcasting"] = JsonNode(false);
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, magicRules);
	}
};
}

TEST_F(NewHorizonsBloodScentTest, LegalBasicOfferUsesStrictHalfHealthAndCapsOnlyTheAttack)
{
	startGame();
	selectBloodScent(attackerSideHero);
	startBattle();
	ASSERT_EQ(battle()->getBloodrageLowHealthIncrement(BattleSide::ATTACKER), 5);
	ASSERT_EQ(battle()->getBloodrageCapPercent(BattleSide::ATTACKER), 20);
	ASSERT_EQ(battle()->getBloodrageDamagePercent(BattleSide::ATTACKER), 0);

	auto * attacker = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(3, 5), 1);
	auto * evenTarget = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(13, 5), 1);
	ASSERT_EQ(evenTarget->getTotalHealth(), 10);
	setAvailableHealth(evenTarget, 5);
	EXPECT_EQ(battle()->battleGetBloodrageDamagePercent(attacker, evenTarget), 0)
		<< "Exactly half maximum HP is not below half";
	setAvailableHealth(evenTarget, 4);
	EXPECT_EQ(battle()->battleGetBloodrageDamagePercent(attacker, evenTarget), 5);

	auto * oddTarget = addStack(BattleSide::DEFENDER, creatureByName("core:griffin"), BattleHex(11, 7), 1);
	ASSERT_EQ(oddTarget->getTotalHealth(), 25);
	setAvailableHealth(oddTarget, 13);
	EXPECT_EQ(battle()->battleGetBloodrageDamagePercent(attacker, oddTarget), 0)
		<< "For odd maximum HP, ceil(maximum / 2) remains outside the strict threshold";
	setAvailableHealth(oddTarget, 12);
	EXPECT_EQ(battle()->battleGetBloodrageDamagePercent(attacker, oddTarget), 5);

	battle()->getSide(BattleSide::ATTACKER).bloodrageDamagePercent = 18;
	EXPECT_EQ(battle()->battleGetBloodrageDamagePercent(attacker, oddTarget), 20)
		<< "The temporary increment is bounded by the saved cap";
	EXPECT_EQ(battle()->getBloodrageDamagePercent(BattleSide::ATTACKER), 18)
		<< "Evaluating Blood Scent does not change persistent Bloodrage";
}

TEST_F(NewHorizonsBloodScentTest, SavedRankIncrementUsesEachCurrentController)
{
	startGame();
	selectBloodScent(attackerSideHero);
	selectBloodScent(defenderSideHero);
	attackerSideHero->setSecSkillLevel(bloodrageSkill(), MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	defenderSideHero->setSecSkillLevel(bloodrageSkill(), MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
	startBattle();
	ASSERT_EQ(battle()->getBloodrageLowHealthIncrement(BattleSide::ATTACKER), 12);
	ASSERT_EQ(battle()->getBloodrageLowHealthIncrement(BattleSide::DEFENDER), 8);

	auto * attacker = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(3, 5), 1);
	auto * defender = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(13, 5), 1);
	setAvailableHealth(defender, 99);
	setAvailableHealth(attacker, 99);
	EXPECT_EQ(battle()->battleGetBloodrageDamagePercent(attacker, defender), 12);
	EXPECT_EQ(battle()->battleGetBloodrageDamagePercent(defender, attacker), 8);

	defenderSideHero->setSecSkillLevel(bloodrageSkill(), MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	EXPECT_EQ(battle()->getBloodrageLowHealthIncrement(BattleSide::DEFENDER), 8)
		<< "The saved Advanced increment is stable after later hero rank changes";
}

TEST_F(NewHorizonsBloodScentTest, PhysicalMeleeRangedAndRetaliationReceiveTheAttackLocalIncrement)
{
	startGame();
	selectBloodScent(attackerSideHero);
	selectBloodScent(defenderSideHero);
	startBattle();
	auto * melee = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(3, 5), 10);
	auto * archer = addStack(BattleSide::ATTACKER, creatureByName("core:archer"), BattleHex(7, 5), 10);
	auto * target = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(13, 5), 10);
	setAvailableHealth(target, 50);
	const auto meleeBaseline = battle()->calculateDmgRange(BattleAttackInfo(melee, target, 0, false)).damage;
	const auto rangedBaseline = battle()->calculateDmgRange(BattleAttackInfo(archer, target, 0, true)).damage;
	auto * retaliationSource = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(13, 7), 10);
	auto * retaliationTarget = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(9, 7), 10);
	setAvailableHealth(retaliationTarget, 50);
	BattleAttackInfo retaliation(retaliationSource, retaliationTarget, 0, false);
	retaliation.retaliation = true;
	const auto retaliationBaseline = battle()->calculateDmgRange(retaliation).damage;

	setAvailableHealth(target, 49);
	setAvailableHealth(retaliationTarget, 49);
	const BattleAttackInfo meleeAttack(melee, target, 0, false);
	const BattleAttackInfo rangedAttack(archer, target, 0, true);
	const auto meleeBoosted = battle()->calculateDmgRange(meleeAttack).damage;
	const auto rangedBoosted = battle()->calculateDmgRange(rangedAttack).damage;
	const auto retaliationBoosted = battle()->calculateDmgRange(retaliation).damage;
	const auto meleeReference = persistentBloodrageReference(meleeAttack, BattleSide::ATTACKER);
	const auto rangedReference = persistentBloodrageReference(rangedAttack, BattleSide::ATTACKER);
	const auto retaliationReference = persistentBloodrageReference(retaliation, BattleSide::DEFENDER);
	EXPECT_EQ(meleeBoosted.min, meleeReference.min);
	EXPECT_EQ(meleeBoosted.max, meleeReference.max);
	EXPECT_EQ(rangedBoosted.min, rangedReference.min);
	EXPECT_EQ(rangedBoosted.max, rangedReference.max);
	EXPECT_EQ(retaliationBoosted.min, retaliationReference.min);
	EXPECT_EQ(retaliationBoosted.max, retaliationReference.max);
	EXPECT_GT(meleeBoosted.min, meleeBaseline.min);
	EXPECT_GT(rangedBoosted.max, rangedBaseline.max);
	EXPECT_GT(retaliationBoosted.min, retaliationBaseline.min);

	BattleAttackInfo magical(melee, target, 0, false);
	magical.physicalDamage = false;
	const auto nonPhysical = battle()->calculateDmgRange(magical).damage;
	EXPECT_EQ(nonPhysical.min, meleeBaseline.min);
	EXPECT_EQ(nonPhysical.max, meleeBaseline.max)
		<< "The attack-local increment does not leak into magical damage";
	EXPECT_EQ(battle()->getBloodrageDamagePercent(BattleSide::ATTACKER), 0)
		<< "Neither physical damage previews nor their Blood Scent increment persist";
}

TEST_F(NewHorizonsBloodScentTest, HypnotizedAttackerUsesSavedIncrementWithoutRevealingEnemyHero)
{
	startGame();
	selectBloodScent(defenderSideHero);
	startBattle();
	auto * hypnotized = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(3, 5), 10);
	auto * friendlyTarget = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(5, 5), 10);
	auto * nextTarget = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(7, 5), 10);
	setAvailableHealth(friendlyTarget, 49);
	setAvailableHealth(nextTarget, 100);
	EXPECT_EQ(battle()->battleGetBloodrageDamagePercent(hypnotized, friendlyTarget), 0)
		<< "The unmodified attacker regards this same-side stack as friendly";

	hypnotized->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::HYPNOTIZED, BonusSource::OTHER, 1, BonusSourceID()));
	ASSERT_EQ(battle()->battleGetOwnerHero(hypnotized), defenderSideHero);
	EXPECT_EQ(battle()->battleGetBloodrageDamagePercent(hypnotized, friendlyTarget), 5)
		<< "After control changes, the saved increment belongs to the current controller";
	const BattleAttackInfo liveAttack(hypnotized, friendlyTarget, 0, false);
	const auto liveHealthyBaseline = battle()->calculateDmgRange(
		BattleAttackInfo(hypnotized, nextTarget, 0, false)).damage;
	const auto liveLowTarget = battle()->calculateDmgRange(liveAttack).damage;
	const auto liveReference = persistentBloodrageReference(liveAttack, BattleSide::DEFENDER);
	EXPECT_EQ(liveLowTarget.min, liveReference.min);
	EXPECT_EQ(liveLowTarget.max, liveReference.max)
		<< "The live physical damage payload uses the current controller's saved increment";
	EXPECT_GT(liveLowTarget.min, liveHealthyBaseline.min);
	EXPECT_GT(liveLowTarget.max, liveHealthyBaseline.max);
	EXPECT_EQ(battle()->battleGetBloodrageDamagePercent(hypnotized, nextTarget), 0)
		<< "A healthy hostile target does not receive the low-health increment";

	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	ASSERT_EQ(callback->battleGetFightingHero(BattleSide::DEFENDER), nullptr)
		<< "The player-scoped callback intentionally hides the enemy hero";
	auto environment = std::make_shared<BloodScentEnvironment>(gameState());
	auto parent = std::make_shared<HypotheticBattle>(environment.get(), callback);
	auto branch = std::make_shared<HypotheticBattle>(environment.get(), parent);
	auto sibling = std::make_shared<HypotheticBattle>(environment.get(), parent);
	auto parentAttacker = parent->getForUpdate(hypnotized->unitId());
	auto parentTarget = parent->getForUpdate(nextTarget->unitId());
	auto branchAttacker = branch->getForUpdate(hypnotized->unitId());
	auto branchTarget = branch->getForUpdate(nextTarget->unitId());
	auto siblingAttacker = sibling->getForUpdate(hypnotized->unitId());
	auto siblingTarget = sibling->getForUpdate(nextTarget->unitId());
	int64_t branchDamage = 51;
	branchTarget->damage(branchDamage);
	EXPECT_EQ(branch->battleGetBloodrageDamagePercent(branchAttacker.get(), branchTarget.get()), 5)
		<< "The detached branch uses the saved defender-side increment while the enemy hero is hidden";
	EXPECT_EQ(parent->battleGetBloodrageDamagePercent(parentAttacker.get(), parentTarget.get()), 0);
	EXPECT_EQ(sibling->battleGetBloodrageDamagePercent(siblingAttacker.get(), siblingTarget.get()), 0);
	const auto projectedBaseline = parent->calculateDmgRange(
		BattleAttackInfo(parentAttacker.get(), parentTarget.get(), 0, false)).damage;
	const auto projectedLowTarget = branch->calculateDmgRange(
		BattleAttackInfo(branchAttacker.get(), branchTarget.get(), 0, false)).damage;
	EXPECT_GT(projectedLowTarget.min, projectedBaseline.min);
	EXPECT_GT(projectedLowTarget.max, projectedBaseline.max)
		<< "Detached damage prediction applies the attack-local increment";
	EXPECT_EQ(battle()->battleGetBloodrageDamagePercent(hypnotized, nextTarget), 0)
		<< "Branch-local damage has not changed the live target";
	setAvailableHealth(nextTarget, 49);
	const BattleAttackInfo liveBranchAttack(hypnotized, nextTarget, 0, false);
	const auto liveBranchRange = battle()->calculateDmgRange(liveBranchAttack).damage;
	const auto liveBranchReference = persistentBloodrageReference(liveBranchAttack, BattleSide::DEFENDER);
	EXPECT_EQ(projectedLowTarget.min, liveBranchRange.min);
	EXPECT_EQ(projectedLowTarget.max, liveBranchRange.max)
		<< "The detached and live paths agree for the same wounded hostile stack";
	EXPECT_EQ(liveBranchRange.min, liveBranchReference.min);
	EXPECT_EQ(liveBranchRange.max, liveBranchReference.max);
	EXPECT_EQ(battle()->battleGetBloodrageDamagePercent(hypnotized, nextTarget), 5);
	EXPECT_EQ(parent->battleGetBloodrageDamagePercent(parentAttacker.get(), parentTarget.get()), 0);
	EXPECT_EQ(sibling->battleGetBloodrageDamagePercent(siblingAttacker.get(), siblingTarget.get()), 0);
}

TEST_F(NewHorizonsBloodScentLegacySerializationTest, CurrentRoundTripPreservesIncrementAndOlderWritersRejectLoss)
{
	startGame();
	selectBloodScent(attackerSideHero);
	startBattle();
	ASSERT_EQ(battle()->getBloodrageLowHealthIncrement(BattleSide::ATTACKER), 5);

	const auto restored = CMemorySerializer::deepCopy(*battle(), gameState().get());
	ASSERT_NE(restored, nullptr);
	EXPECT_EQ(restored->getBloodrageLowHealthIncrement(BattleSide::ATTACKER), 5);
	EXPECT_EQ(restored->getBloodrageLowHealthIncrement(BattleSide::DEFENDER), 0);

	CMemorySerializer rejected;
	rejected.oser.version = ESerializationVersion::NEW_HORIZONS_BLOODRAGE_THRESHOLD_BONUSES;
	try
	{
		rejected.oser & *battle();
		FAIL() << "A writer before Blood Scent support must not drop an enabled snapshot increment";
	}
	catch(const std::runtime_error & error)
	{
		EXPECT_EQ(std::string(error.what()), "Cannot discard Blood Scent in an older format");
	}
	EXPECT_TRUE(rejected.extractBuffer().empty());

	// Create a genuine pre-feature record: the increment was not present, even
	// though the attached hero has Blood Scent in the current game state.
	auto legacySource = CMemorySerializer::deepCopy(*battle(), gameState().get());
	ASSERT_NE(legacySource, nullptr);
	legacySource->getSide(BattleSide::ATTACKER).bloodrageLowHealthIncrement = 0;
	legacySource->getSide(BattleSide::DEFENDER).bloodrageLowHealthIncrement = 0;
	CMemorySerializer legacy;
	legacy.oser.version = legacy.iser.version = ESerializationVersion::NEW_HORIZONS_BLOODRAGE_THRESHOLD_BONUSES;
	legacy.oser & *legacySource;
	auto bytes = legacy.extractBuffer();
	ASSERT_FALSE(bytes.empty());
	CMemorySerializer reader(std::move(bytes));
	reader.iser.version = ESerializationVersion::NEW_HORIZONS_BLOODRAGE_THRESHOLD_BONUSES;
	reader.iser.cb = gameState().get();
	BattleInfo legacyRestored(gameState().get());
	reader.iser & legacyRestored;
	EXPECT_EQ(legacyRestored.getBloodrageLowHealthIncrement(BattleSide::ATTACKER), 0);
	EXPECT_EQ(legacyRestored.getBloodrageLowHealthIncrement(BattleSide::DEFENDER), 0);
}
