/*
 * NewHorizonsBloodScentTest.cpp, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "BattleTestFixture.h"
#include "../../NewHorizonsHistoricalAdventurePolicyTestUtils.h"

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
#include "../../../server/battles/BattleProcessor.h"
#include "../../../lib/spells/ISpellMechanics.h"
#include "../../../lib/spells/NewHorizonsMagic.h"

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
	bool absentMagicRules = false;
	void SetUp() override
	{
		BattleTestFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";
	}

	void mapLoaded(CMap * loaded) override
	{
		BattleTestFixture::mapLoaded(loaded);
		if(absentMagicRules)
		{
			loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, JsonNode());
			// The current book table requires current magic. Isolate only that future
			// metadata, retaining every other captured hero profile and rule.
			JsonNode heroRules(JsonPath::builtin("config/newHorizonsHeroes"));
			heroRules["startingSkills"].Struct().erase("startingBookReplacements");
			heroRules.setOverrideFlag(true);
			loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, std::move(heroRules));
		}
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
		// This record predates independent combat-policy and spell-clause captures.
		// Blood Scent is resolved from the selected perk, not the magic profile.
		loaded->overrideGameSetting(EGameSettings::COMBAT_NEW_HORIZONS_FINAL_LUCK, JsonNode(false));
		loaded->overrideGameSetting(EGameSettings::COMBAT_MORALE_EXTRA_DAMAGE_PERCENT, JsonNode(100));
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, JsonNode());
		auto perks = loaded->getSettings().getValue(EGameSettings::HEROES_NEW_HORIZONS_PERKS);
		for(auto & perk : perks["skills"]["new-horizons:command"]["perks"].Vector())
			if(perk["id"].String() == "new-horizons:command.crisisCommand")
				perk["effect"]["status"].String() = "planned";
		perks.setOverrideFlag(true);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, std::move(perks));
		isolateHistoricalAdventurePolicies(*loaded);
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

	// An explicit odd-HP fixture, independent of current creature balance values.
	auto * oddTarget = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(11, 7), 1);
	oddTarget->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::STACK_HEALTH, BonusSource::OTHER, 15, BonusSourceID()));
	ASSERT_EQ(oddTarget->getTotalHealth(), 25);
	// A maximum-HP bonus does not heal the existing10HP creature. Fill the
	// synthetic maximum through the ordinary health API and authoritative packet.
	auto fullOddState = oddTarget->acquireState();
	int64_t healing = 25;
	const auto healed = fullOddState->heal(healing, EHealLevel::HEAL, EHealPower::PERMANENT).healedHealthPoints;
	ASSERT_EQ(healed, 15);
	BattleUnitsChanged filled;
	filled.battleID = BattleID(0);
	UnitChanges filledHealth(oddTarget->unitId(), UnitChanges::EOperation::UPDATE);
	filledHealth.data = fullOddState->save();
	filledHealth.healthDelta = healed;
	filled.changedStacks.push_back(std::move(filledHealth));
	gameHandler->sendAndApply(filled);
	ASSERT_EQ(oddTarget->getAvailableHealth(), 25);
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
	ASSERT_FALSE(battle()->getLuckRollRules().finalDirectPhysicalMultiplier);
	ASSERT_EQ(battle()->getMoraleExtraDamagePercent(), 100);
	ASSERT_TRUE(battle()->getMagicRules().isNull());
	ASSERT_EQ(battle()->getBloodrageLowHealthIncrement(BattleSide::ATTACKER), 5);

	const auto restored = CMemorySerializer::deepCopy(*battle(), gameState().get());
	ASSERT_NE(restored, nullptr);
	EXPECT_EQ(restored->getBloodrageLowHealthIncrement(BattleSide::ATTACKER), 5);
	EXPECT_EQ(restored->getBloodrageLowHealthIncrement(BattleSide::DEFENDER), 0);

	// Actual battles now unconditionally capture the later Army Value metadata.
	// An old-format control must explicitly represent its historical absence,
	// without dropping the selected Blood Scent receipt under test.
	auto historicalContext = CMemorySerializer::deepCopy(*battle(), gameState().get());
	ASSERT_NE(historicalContext, nullptr);
	ASSERT_TRUE(historicalContext->hasInitialArmyValueState());
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
	{
		historicalContext->getSide(side).initialArmyValue.reset();
		historicalContext->getSide(side).initialArmyIsWandering = false;
	}
	ASSERT_FALSE(historicalContext->hasInitialArmyValueState());
	ASSERT_EQ(historicalContext->getBloodrageLowHealthIncrement(BattleSide::ATTACKER), 5);

	CMemorySerializer rejected;
	rejected.oser.version = ESerializationVersion::NEW_HORIZONS_BLOODRAGE_THRESHOLD_BONUSES;
	try
	{
		rejected.oser & *historicalContext;
		FAIL() << "A writer before Blood Scent support must not drop an enabled snapshot increment";
	}
	catch(const std::runtime_error & error)
	{
		EXPECT_EQ(std::string(error.what()), "Cannot discard Blood Scent in an older format");
	}
	EXPECT_TRUE(rejected.extractBuffer().empty());

	// Create a genuine pre-feature record: the increment was not present, even
	// though the attached hero has Blood Scent in the current game state.
	auto legacySource = CMemorySerializer::deepCopy(*historicalContext, gameState().get());
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

TEST_F(NewHorizonsBloodScentTest, OrdinaryMagogShotUsesRankAndStrictHalfPremiumInLiveAndDetachedDamage)
{
	startGame();
	ASSERT_NO_FATAL_FAILURE(selectBloodScent(attackerSideHero));
	startBattle();
	auto * source = addStack(BattleSide::ATTACKER, creatureByName("core:magog"), BattleHex(3, 5), 100);
	auto * target = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(12, 5), 1000);
	forceMaximumDamage(source);
	ASSERT_TRUE(source->hasBonusOfType(BonusType::SPELL_LIKE_ATTACK));
	ASSERT_TRUE(battle()->battleCanShoot(source, target->getPosition()));
	BattleAttackInfo shot(source, target, 0, true);
	ASSERT_FALSE(shot.physicalDamage);
	ASSERT_TRUE(newHorizonsMagic::rulesActive(battle()->getMagicRules()));
	ASSERT_EQ(battle()->getMagicRules()["rulesetVersion"].Integer(),
		newHorizonsMagic::SCHOOL_RANK_POWER_COEFFICIENT_RULESET_VERSION);
	ASSERT_NO_FATAL_FAILURE(setAvailableHealth(target, target->getTotalHealth() / 2));
	const auto noRage = battle()->calculateDmgRange(shot).damage;
	battle()->getSide(BattleSide::ATTACKER).bloodrageDamagePercent = 10;
	const auto rankOnly = battle()->calculateDmgRange(shot).damage;
	EXPECT_GT(rankOnly.max, noRage.max);
	EXPECT_EQ(battle()->battleGetBloodrageDamagePercent(source, target), 10);
	ASSERT_NO_FATAL_FAILURE(setAvailableHealth(target, target->getAvailableHealth() - 1));
	EXPECT_EQ(battle()->battleGetBloodrageDamagePercent(source, target), 15);
	const auto boosted = battle()->calculateDmgRange(shot).damage;
	const auto reference = persistentBloodrageReference(shot, BattleSide::ATTACKER);
	EXPECT_EQ(boosted.min, reference.min);
	EXPECT_EQ(boosted.max, reference.max);
	EXPECT_GT(boosted.max, rankOnly.max);
	ASSERT_LT(boosted.max, target->getAvailableHealth());

	BloodScentEnvironment environment(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	auto model = std::make_shared<HypotheticBattle>(&environment, callback);
	const auto projectedSource = model->getForUpdate(source->unitId());
	const auto projectedTarget = model->getForUpdate(target->unitId());
	ASSERT_NE(static_cast<const battle::Unit *>(projectedSource.get()),
		static_cast<const battle::Unit *>(source));
	ASSERT_NE(static_cast<const battle::Unit *>(projectedTarget.get()),
		static_cast<const battle::Unit *>(target));
	ASSERT_EQ(model->battleGetUnitByID(source->unitId()), projectedSource.get());
	ASSERT_EQ(model->battleGetUnitByID(target->unitId()), projectedTarget.get());
	const auto forecast = model->calculateDmgRange(BattleAttackInfo(
		model->battleGetUnitByID(source->unitId()), model->battleGetUnitByID(target->unitId()), 0, true)).damage;
	EXPECT_EQ(forecast.min, boosted.min);
	EXPECT_EQ(forecast.max, boosted.max);
	const auto targetHealth = target->getAvailableHealth();
	beginCombat();
	battle()->activeStack = source->unitId();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeShotAttack(source, target)));
	EXPECT_EQ(targetHealth - target->getAvailableHealth(), boosted.max);
	EXPECT_EQ(model->battleGetUnitByID(target->unitId())->getAvailableHealth(), targetHealth);
	EXPECT_EQ(battle()->getBloodrageDamagePercent(BattleSide::ATTACKER), 10);
}

TEST_F(NewHorizonsBloodScentTest, ElementalCollateralRetainsHistoricalDamage)
{
	startGame();
	ASSERT_NO_FATAL_FAILURE(selectBloodScent(attackerSideHero));
	startBattle();
	auto * source = addStack(BattleSide::ATTACKER, creatureByName("core:lich"), BattleHex(3, 5), 100);
	auto * target = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(12, 5), 1000);
	ASSERT_NO_FATAL_FAILURE(setAvailableHealth(target, target->getTotalHealth() / 2 - 1));
	BattleAttackInfo collateral(source, target, 0, true);
	ASSERT_FALSE(collateral.physicalDamage);
	collateral.secondaryAttack = true;
	const auto zero = battle()->calculateDmgRange(collateral).damage;
	battle()->getSide(BattleSide::ATTACKER).bloodrageDamagePercent = 10;
	const auto withRage = battle()->calculateDmgRange(collateral).damage;
	EXPECT_EQ(withRage.min, zero.min);
	EXPECT_EQ(withRage.max, zero.max);
	EXPECT_EQ(battle()->getBloodrageDamagePercent(BattleSide::ATTACKER), 10);
}

TEST_F(NewHorizonsBloodScentTest, AbsentCapturedPolicyRetainsHistoricalElementalPrimaryDamage)
{
	absentMagicRules = true;
	startGame();
	ASSERT_NO_FATAL_FAILURE(selectBloodScent(attackerSideHero));
	startBattle();
	ASSERT_TRUE(battle()->getMagicRules().isNull());
	auto * source = addStack(BattleSide::ATTACKER, creatureByName("core:lich"), BattleHex(3, 5), 100);
	auto * target = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(12, 5), 1000);
	ASSERT_NO_FATAL_FAILURE(setAvailableHealth(target, target->getTotalHealth() / 2 - 1));
	BattleAttackInfo primary(source, target, 0, true);
	ASSERT_FALSE(primary.physicalDamage);
	battle()->getSide(BattleSide::ATTACKER).bloodrageDamagePercent = 10;
	const auto historical = battle()->calculateDmgRange(primary).damage;
	battle()->getSide(BattleSide::ATTACKER).bloodrageDamagePercent = 0;
	const auto historicalZero = battle()->calculateDmgRange(primary).damage;
	EXPECT_EQ(historical.min, historicalZero.min);
	EXPECT_EQ(historical.max, historicalZero.max);
	EXPECT_EQ(battle()->getBloodrageDamagePercent(BattleSide::ATTACKER), 0);
}

TEST_F(NewHorizonsBloodScentTest, ActualActiveCreatureCastDoesNotReceiveRankOrBloodScentPremium)
{
	startGame();
	ASSERT_NO_FATAL_FAILURE(selectBloodScent(attackerSideHero));
	startBattle();
	auto * first = addStack(BattleSide::ATTACKER, creatureByName("core:magog"), BattleHex(3, 5), 10);
	auto * second = addStack(BattleSide::ATTACKER, creatureByName("core:magog"), BattleHex(3, 7), 10);
	auto * firstTarget = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(12, 5), 1000);
	auto * secondTarget = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(12, 7), 1000);
	for(auto * target : {firstTarget, secondTarget})
	{
		ASSERT_NO_FATAL_FAILURE(setAvailableHealth(target, target->getTotalHealth() / 2 - 1));
	}
	for(auto * caster : {first, second})
	{
		caster->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
			BonusType::SPELLCASTER, BonusSource::CREATURE_ABILITY, 1, BonusSourceID(),
			BonusSubtypeID(SpellID(SpellID::MAGIC_ARROW))));
		caster->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
			BonusType::CASTS, BonusSource::CREATURE_ABILITY, 1, BonusSourceID()));
		ASSERT_TRUE(caster->canCast());
	}
	beginCombat();
	const auto before = firstTarget->getAvailableHealth();
	battle()->activeStack = first->unitId();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeCreatureSpellcast(first, spells::Target{spells::Destination(firstTarget)}, SpellID::MAGIC_ARROW)));
	const auto baselineLoss = before - firstTarget->getAvailableHealth();
	ASSERT_GT(baselineLoss, 0);
	battle()->getSide(BattleSide::ATTACKER).bloodrageDamagePercent = 20;
	EXPECT_EQ(battle()->battleGetBloodrageDamagePercent(second, secondTarget), 20);
	const auto secondBefore = secondTarget->getAvailableHealth();
	battle()->activeStack = second->unitId();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeCreatureSpellcast(second, spells::Target{spells::Destination(secondTarget)}, SpellID::MAGIC_ARROW)));
	EXPECT_EQ(secondBefore - secondTarget->getAvailableHealth(), baselineLoss);
	EXPECT_EQ(battle()->getBloodrageDamagePercent(BattleSide::ATTACKER), 20);
}
