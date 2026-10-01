/*
 * NewHorizonsPiercingBoltsTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in the main folder
 *
 */
#include "StdInc.h"
#include "BattleTestFixture.h"

#include "../../../AI/BattleAI/StackWithBonuses.h"
#include "../../../lib/GameConstants.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/CSkillHandler.h"
#include "../../../lib/battle/BattleAction.h"
#include "../../../lib/battle/BattleAttackInfo.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/constants/EntityIdentifiers.h"
#include "../../../lib/constants/Enumerations.h"
#include "../../../lib/entities/hero/NewHorizonsPerkState.h"
#include "../../../lib/gameState/CGameState.h"
#include "../../../lib/json/JsonNode.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/mapping/CMap.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/filesystem/ResourcePath.h"
#include "../../../server/CGameHandler.h"
#include "../../../server/battles/BattleProcessor.h"

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace
{
constexpr auto warMachinesSkillId = "new-horizons:warMachines";
constexpr auto surgeonPerkId = "new-horizons:warMachines.surgeon";
constexpr auto piercingBoltsPerkId = "new-horizons:warMachines.piercingBolts";

bool setPerkActive(JsonNode & rules, std::string_view perkId)
{
	auto & perks = rules["skills"][warMachinesSkillId]["perks"].Vector();
	const auto found = std::find_if(perks.begin(), perks.end(), [perkId](const JsonNode & perk)
	{
		return perk["id"].String() == perkId;
	});
	if(found == perks.end())
		return false;

	(*found)["effect"]["status"].String() = "active";
	return true;
}

class PiercingBoltsEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;

public:
	explicit PiercingBoltsEnvironment(std::shared_ptr<CGameState> value)
		: state(std::move(value))
	{}

	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class NewHorizonsPiercingBoltsTest : public BattleTestFixture
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
		TinyMapGameTest::mapLoaded(loaded);

		JsonNode perkRules(JsonPath::builtin("config/newHorizonsPerks"));
		if(!setPerkActive(perkRules, surgeonPerkId) || !setPerkActive(perkRules, piercingBoltsPerkId))
			throw std::runtime_error("Missing Surgeon or Piercing Bolts from the New Horizons perk registry");
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, std::move(perkRules));

		// Use the canonical loaded capability schema as-is. This fixture must not
		// manufacture an old ruleset version just to exercise the perk.
		JsonNode capabilities(JsonPath::builtin("config/newHorizonsCapabilities"));
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_CAPABILITIES, std::move(capabilities));
	}

	SecondarySkill warMachines() const
	{
		const int decoded = SecondarySkill::decode(warMachinesSkillId);
		EXPECT_GE(decoded, 0);
		return SecondarySkill(decoded);
	}

	void acceptPerkThroughOffer(CGHeroInstance * hero, std::string_view perkId)
	{
		const auto rankLookup = [hero](const std::string & skillId)
		{
			return hero->getPerkSkillRank(skillId);
		};

		for(uint64_t seed = 0; seed < 4096; ++seed)
		{
			const auto offer = hero->getPerkState().prepareOffer(rankLookup, seed);
			const auto selected = std::find_if(offer.begin(), offer.end(), [&](const auto & candidate)
			{
				return candidate.selection.skillId == warMachinesSkillId
					&& candidate.selection.perkId == perkId;
			});
			if(selected == offer.end())
				continue;

			const auto choice = static_cast<size_t>(std::distance(offer.begin(), selected));
			gameHandler->levelUpHero(hero, offer, choice, seed, false);
			ASSERT_TRUE(hero->hasActivePerk(warMachinesSkillId, std::string(perkId)));
			return;
		}

		FAIL() << "No legal New Horizons perk offer contained " << perkId;
	}

	void prepareAdvancedWarMachines(CGHeroInstance * hero, bool selectPiercingBolts)
	{
		const auto skill = warMachines();
		ASSERT_TRUE(hero->getPerkState().canAdvanceSkillNormally(warMachinesSkillId,
			hero->getSecSkillLevel(skill)));
		gameHandler->levelUpHero(hero, skill, false);
		ASSERT_EQ(hero->getPerkSkillRank(warMachinesSkillId), MasteryLevel::BASIC);
		acceptPerkThroughOffer(hero, surgeonPerkId);

		const int currentRank = hero->getPerkSkillRank(warMachinesSkillId);
		ASSERT_EQ(currentRank, MasteryLevel::BASIC);
		ASSERT_TRUE(hero->getPerkState().canAdvanceSkillNormally(warMachinesSkillId, currentRank));
		gameHandler->levelUpHero(hero, skill, false);
		ASSERT_EQ(hero->getPerkSkillRank(warMachinesSkillId), MasteryLevel::ADVANCED);

		if(selectPiercingBolts)
			acceptPerkThroughOffer(hero, piercingBoltsPerkId);
		else
			ASSERT_FALSE(hero->hasActivePerk(warMachinesSkillId, piercingBoltsPerkId));
	}

	void prepareMirrorBallistaBattle()
	{
		startGame();
		prepareAdvancedWarMachines(attackerSideHero, true);
		prepareAdvancedWarMachines(defenderSideHero, false);
		giveArtifact(attackerSideHero, ArtifactID::BALLISTA, ArtifactPosition::MACH1);
		giveArtifact(defenderSideHero, ArtifactID::BALLISTA, ArtifactPosition::MACH1);
		startBattle();
	}

	const CStack * ballistaFor(BattleSide side) const
	{
		const auto machines = battle()->battleGetStacksIf([side](const CStack * stack)
		{
			return stack->unitSide() == side && stack->isBallista();
		});
		return machines.size() == 1 ? machines.front() : nullptr;
	}

	CStack * addHighDefenseTarget(BattleSide side, const BattleHex & position, const CStack * attacker)
	{
		auto * target = addStack(side, creatureByName("core:pikeman"), position, 1000);
		if(!target)
			return nullptr;

		const int currentDefense = target->getDefense(true);
		const int desiredDefense = std::max(currentDefense, attacker->getAttack(true) * 2 + 8);
		if(desiredDefense > currentDefense)
			target->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
				BonusType::PRIMARY_SKILL, BonusSource::OTHER, desiredDefense - currentDefense,
				BonusSourceID(), BonusSubtypeID(PrimarySkill::DEFENSE)));

		const int defense = target->getDefense(true);
		const int attack = attacker->getAttack(true);
		EXPECT_GT(defense, attack);
		EXPECT_GT(defense - defense / 2, attack)
			<< "The full and half-defense cases must both stay on the negative Attack-versus-Defense branch";
		const double perPoint = LIBRARY->engineSettings()->getDouble(EGameSettings::COMBAT_DEFENSE_POINT_DAMAGE_FACTOR);
		const double cap = LIBRARY->engineSettings()->getDouble(EGameSettings::COMBAT_DEFENSE_POINT_DAMAGE_FACTOR_CAP);
		const double unchangedReduction = std::min(cap, perPoint * (defense - attack));
		const double piercedReduction = std::min(cap, perPoint * (defense - defense / 2 - attack));
		EXPECT_LT(piercedReduction, unchangedReduction)
			<< "The fixture should leave room for Piercing Bolts to improve the final damage factor";
		return target;
	}

	void expectSameDamage(const DamageRange & actual, const DamageRange & expected)
	{
		EXPECT_EQ(actual.min, expected.min);
		EXPECT_EQ(actual.max, expected.max);
	}
};
}

TEST_F(NewHorizonsPiercingBoltsTest, LegalAdvancedPerkImprovesBallistaForecastAndAcceptedShot)
{
	prepareMirrorBallistaBattle();
	const auto * ballista = ballistaFor(BattleSide::ATTACKER);
	const auto * noPerkBallista = ballistaFor(BattleSide::DEFENDER);
	ASSERT_NE(ballista, nullptr);
	ASSERT_NE(noPerkBallista, nullptr);
	ASSERT_EQ(battle()->battleGetOwnerHero(ballista), attackerSideHero);
	ASSERT_EQ(battle()->battleGetOwnerHero(noPerkBallista), defenderSideHero);
	ASSERT_TRUE(attackerSideHero->hasActivePerk(warMachinesSkillId, piercingBoltsPerkId));
	ASSERT_FALSE(defenderSideHero->hasActivePerk(warMachinesSkillId, piercingBoltsPerkId));

	auto * target = addHighDefenseTarget(BattleSide::DEFENDER, BattleHex(rightHex), ballista);
	auto * noPerkTarget = addHighDefenseTarget(BattleSide::ATTACKER, BattleHex(leftHex), noPerkBallista);
	ASSERT_NE(target, nullptr);
	ASSERT_NE(noPerkTarget, nullptr);
	ASSERT_EQ(ballista->getAttack(true), noPerkBallista->getAttack(true));
	ASSERT_EQ(target->getDefense(true), noPerkTarget->getDefense(true));

	const auto piercedForecast = battle()->calculateDmgRange(BattleAttackInfo(ballista, target, 0, true));
	const auto noPerkForecast = battle()->calculateDmgRange(BattleAttackInfo(noPerkBallista, noPerkTarget, 0, true));
	EXPECT_EQ(piercedForecast.damageBeforeDefense.min, noPerkForecast.damageBeforeDefense.min);
	EXPECT_EQ(piercedForecast.damageBeforeDefense.max, noPerkForecast.damageBeforeDefense.max);
	EXPECT_GT(piercedForecast.damage.min, noPerkForecast.damage.min);
	EXPECT_GT(piercedForecast.damage.max, noPerkForecast.damage.max);

	beginCombat();
	const auto firstRound = battle()->getRound();
	const auto maximumTurns = battle()->stacks.size() * 2;
	for(size_t turn = 0; turn < maximumTurns && battle()->battleActiveUnit() != ballista; ++turn)
	{
		ASSERT_EQ(battle()->getRound(), firstRound);
		const auto * active = battle()->battleActiveUnit();
		ASSERT_NE(active, nullptr);
		ASSERT_NE(active->unitId(), ballista->unitId());
		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0),
			battle()->sideToPlayer(active->unitSide()), BattleAction::makeDefend(active)));
	}
	ASSERT_EQ(battle()->battleActiveUnit(), ballista);
	ASSERT_TRUE(battle()->battleCanShoot(ballista, target->getPosition()));

	const auto acceptedShotPreview = battle()->calculateDmgRange(BattleAttackInfo(ballista, target, 0, true)).damage;
	const auto healthBefore = target->getAvailableHealth();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0),
		battle()->sideToPlayer(ballista->unitSide()), BattleAction::makeShotAttack(ballista, target)));
	const auto actualDamage = healthBefore - target->getAvailableHealth();
	EXPECT_GE(actualDamage, acceptedShotPreview.min);
	EXPECT_LE(actualDamage, acceptedShotPreview.max);
}

TEST_F(NewHorizonsPiercingBoltsTest, DetachedForecastUsesTheCurrentBallistaController)
{
	prepareMirrorBallistaBattle();
	const auto * ballista = ballistaFor(BattleSide::ATTACKER);
	ASSERT_NE(ballista, nullptr);
	ASSERT_EQ(battle()->battleGetOwner(ballista), battle()->sideToPlayer(ballista->unitSide()));
	ASSERT_EQ(battle()->battleGetOwnerHero(ballista), attackerSideHero);

	auto * target = addHighDefenseTarget(BattleSide::DEFENDER, BattleHex(rightHex), ballista);
	ASSERT_NE(target, nullptr);
	const auto liveInfo = BattleAttackInfo(ballista, target, 0, true);
	const auto live = battle()->calculateDmgRange(liveInfo);

	const auto controller = battle()->sideToPlayer(ballista->unitSide());
	const auto callback = std::make_shared<CPlayerBattleCallback>(battle(), controller);
	PiercingBoltsEnvironment environment(gameState());
	HypotheticBattle projected(&environment, callback);
	const auto projectedBallista = projected.getForUpdate(ballista->unitId());
	const auto projectedTarget = projected.getForUpdate(target->unitId());
	ASSERT_NE(projectedBallista, nullptr);
	ASSERT_NE(projectedTarget, nullptr);
	EXPECT_EQ(projected.battleGetOwnerHero(projectedBallista.get()), attackerSideHero);

	const auto projectedInfo = BattleAttackInfo(projectedBallista.get(), projectedTarget.get(), 0, true);
	const auto detached = projected.calculateDmgRange(projectedInfo);
	expectSameDamage(detached.damage, live.damage);
	expectSameDamage(detached.damageBeforeDefense, live.damageBeforeDefense);
}

TEST_F(NewHorizonsPiercingBoltsTest, OrdinaryPhysicalRangedCreaturesDoNotReceiveBallistaPenetration)
{
	prepareMirrorBallistaBattle();
	auto * attackerTitan = addStack(BattleSide::ATTACKER,
		creatureByName("core:titan"), BattleHex(leftHex), 100);
	auto * defenderTitan = addStack(BattleSide::DEFENDER,
		creatureByName("core:titan"), BattleHex(rightHex), 100);
	ASSERT_NE(attackerTitan, nullptr);
	ASSERT_NE(defenderTitan, nullptr);
	auto * defenderTarget = addHighDefenseTarget(BattleSide::DEFENDER, BattleHex(rightHex + 2), attackerTitan);
	auto * attackerTarget = addHighDefenseTarget(BattleSide::ATTACKER, BattleHex(leftHex - 2), defenderTitan);
	ASSERT_NE(defenderTarget, nullptr);
	ASSERT_NE(attackerTarget, nullptr);
	ASSERT_FALSE(attackerTitan->isBallista());
	ASSERT_FALSE(defenderTitan->isBallista());

	const auto withPiercing = battle()->calculateDmgRange(BattleAttackInfo(attackerTitan, defenderTarget, 0, true));
	const auto withoutPiercing = battle()->calculateDmgRange(BattleAttackInfo(defenderTitan, attackerTarget, 0, true));
	expectSameDamage(withPiercing.damage, withoutPiercing.damage);
	expectSameDamage(withPiercing.damageBeforeDefense, withoutPiercing.damageBeforeDefense);
}

TEST_F(NewHorizonsPiercingBoltsTest, BallistaPerkDoesNotAffectNonPhysicalOrMeleeForecasts)
{
	prepareMirrorBallistaBattle();
	const auto * ballista = ballistaFor(BattleSide::ATTACKER);
	const auto * noPerkBallista = ballistaFor(BattleSide::DEFENDER);
	ASSERT_NE(ballista, nullptr);
	ASSERT_NE(noPerkBallista, nullptr);
	auto * target = addHighDefenseTarget(BattleSide::DEFENDER, BattleHex(rightHex), ballista);
	auto * noPerkTarget = addHighDefenseTarget(BattleSide::ATTACKER, BattleHex(leftHex), noPerkBallista);
	ASSERT_NE(target, nullptr);
	ASSERT_NE(noPerkTarget, nullptr);
	ASSERT_EQ(target->getDefense(true), noPerkTarget->getDefense(true));
	ASSERT_EQ(ballista->getAttack(true), noPerkBallista->getAttack(true));

	auto nonPhysicalRanged = BattleAttackInfo(ballista, target, 0, true);
	nonPhysicalRanged.physicalDamage = false;
	auto noPerkNonPhysicalRanged = BattleAttackInfo(noPerkBallista, noPerkTarget, 0, true);
	noPerkNonPhysicalRanged.physicalDamage = false;
	const auto activeNonPhysicalRange = battle()->calculateDmgRange(nonPhysicalRanged);
	const auto noPerkNonPhysicalRange = battle()->calculateDmgRange(noPerkNonPhysicalRanged);
	expectSameDamage(activeNonPhysicalRange.damage, noPerkNonPhysicalRange.damage);
	expectSameDamage(activeNonPhysicalRange.damageBeforeDefense, noPerkNonPhysicalRange.damageBeforeDefense);

	const auto activeMeleeRange = battle()->calculateDmgRange(BattleAttackInfo(ballista, target, 0, false));
	const auto noPerkMeleeRange = battle()->calculateDmgRange(BattleAttackInfo(noPerkBallista, noPerkTarget, 0, false));
	expectSameDamage(activeMeleeRange.damage, noPerkMeleeRange.damage);
	expectSameDamage(activeMeleeRange.damageBeforeDefense, noPerkMeleeRange.damageBeforeDefense);
}
