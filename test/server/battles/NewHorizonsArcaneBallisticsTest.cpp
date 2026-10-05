/*
 * NewHorizonsArcaneBallisticsTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in the main folder
 */
#include "StdInc.h"

#include "HeroCommandFixture.h"
#include "../../SpellPointTestUtils.h"
#include "../../hero/NewHorizonsHeroRulesFixture.h"

#include "../../../AI/BattleAI/StackWithBonuses.h"
#include "../../../lib/GameConstants.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/battle/BattleAction.h"
#include "../../../lib/battle/BattleAttackInfo.h"
#include "../../../lib/battle/CBattleInfoCallback.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/bonuses/BonusParameters.h"
#include "../../../lib/constants/EntityIdentifiers.h"
#include "../../../lib/entities/hero/NewHorizonsPerkState.h"
#include "../../../lib/gameState/CGameState.h"
#include "../../../lib/json/JsonNode.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/mapping/CMap.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/modding/IdentifierStorage.h"
#include "../../../lib/modding/ModScope.h"
#include "../../../lib/spells/NewHorizonsSorcery.h"
#include "../../../lib/filesystem/ResourcePath.h"
#include "../../../server/CGameHandler.h"
#include "../../../server/battles/BattleProcessor.h"

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
constexpr auto sorcerySkillId = "new-horizons:sorceryMagic";
constexpr auto sorceryBasicPerkId = "new-horizons:sorceryMagic.overcharger";
constexpr auto arcaneBallisticsPerkId = "new-horizons:sorceryMagic.countermage";
constexpr auto metamagicSkillId = "new-horizons:metamagic";
constexpr auto arcaneAcquisitionPerkId = "new-horizons:metamagic.arcaneAcquisition";

class ArcaneBallisticsEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;

public:
	explicit ArcaneBallisticsEnvironment(std::shared_ptr<CGameState> value)
		: state(std::move(value))
	{}

	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class NewHorizonsArcaneBallisticsTest : public HeroCommandFixture
{
protected:
	SpellID focusMagic;
	SpellID arcaneBreach;
	ScriptID arcaneBreachTrigger;
	CStack * markShooter = nullptr;
	CStack * ordinaryShooter = nullptr;
	CStack * markedTarget = nullptr;

	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";

		focusMagic = SpellID(SpellID::decode(newHorizonsSorcery::FOCUS_MAGIC_SPELL));
		arcaneBreach = SpellID(SpellID::decode(newHorizonsSorcery::ARCANE_BREACH_EFFECT));
		ASSERT_NE(focusMagic, SpellID::NONE);
		ASSERT_NE(arcaneBreach, SpellID::NONE);
		const auto breachScript = LIBRARY->identifiers()->getIdentifier(ModScope::scopeGame(), "script",
			std::string(newHorizonsSorcery::ARCANE_BREACH_TRIGGER));
		ASSERT_TRUE(breachScript.has_value());
		arcaneBreachTrigger = ScriptID(*breachScript);
	}

	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, testHeroRules());
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
	}

	void acceptPerkThroughOffer(CGHeroInstance * hero, std::string_view skillId,
		std::string_view perkId, int expectedRank)
	{
		const auto rankLookup = [hero](const std::string & requestedSkill)
		{
			return hero->getPerkSkillRank(requestedSkill);
		};
		for(uint64_t seed = 0; seed < 4096; ++seed)
		{
			const auto offers = hero->getPerkState().prepareOffer(rankLookup, seed);
			const auto candidate = std::find_if(offers.begin(), offers.end(), [&](const auto & offer)
			{
				return offer.selection.skillId == skillId && offer.selection.perkId == perkId;
			});
			if(candidate == offers.end())
				continue;

			ASSERT_EQ(candidate->requiredRank, expectedRank);
			const auto index = static_cast<size_t>(std::distance(offers.begin(), candidate));
			gameHandler->levelUpHero(hero, offers, index, seed, false);
			ASSERT_TRUE(hero->hasActivePerk(std::string(skillId), std::string(perkId)));
			return;
		}
		FAIL() << "No legal New Horizons perk offer contained " << perkId;
	}

	void acquireBallistics(CGHeroInstance * hero)
	{
		const int decoded = SecondarySkill::decode(sorcerySkillId);
		ASSERT_GE(decoded, 0);
		const SecondarySkill skill(decoded);
		hero->setSecSkillLevel(skill, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		acceptPerkThroughOffer(hero, sorcerySkillId, sorceryBasicPerkId,
			static_cast<int>(MasteryLevel::BASIC));
		gameHandler->levelUpHero(hero, skill, false);
		ASSERT_EQ(hero->getPerkSkillRank(sorcerySkillId), MasteryLevel::ADVANCED);
		acceptPerkThroughOffer(hero, sorcerySkillId, arcaneBallisticsPerkId,
			static_cast<int>(MasteryLevel::ADVANCED));
	}

	void acquireArcaneAcquisition(CGHeroInstance * hero)
	{
		const int decoded = SecondarySkill::decode(metamagicSkillId);
		ASSERT_GE(decoded, 0);
		hero->setSecSkillLevel(SecondarySkill(decoded), MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		acceptPerkThroughOffer(hero, metamagicSkillId, arcaneAcquisitionPerkId,
			static_cast<int>(MasteryLevel::BASIC));
	}

	void activate(const CStack * stack)
	{
		BattleSetActiveStack active;
		active.battleID = BattleID(0);
		active.stack = stack->unitId();
		active.reason = BattleUnitTurnReason::TURN_QUEUE;
		gameHandler->sendAndApply(active);
	}

	bool castFocusMagic(const CStack * shooter, bool followup)
	{
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = BattleSide::ATTACKER;
		action.spell = focusMagic;
		action.metamagicFollowup = followup;
		action.aimToUnit(shooter);
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action);
	}

	void prepare(bool withBallistics, bool withArcaneAcquisition = false)
	{
		startGame();
		if(withBallistics)
			acquireBallistics(attackerSideHero);
		if(withArcaneAcquisition)
			acquireArcaneAcquisition(attackerSideHero);

		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 20, ChangeValueMode::ABSOLUTE);
		attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		attackerSideHero->addSpellToSpellbook(focusMagic);
		setTestSpellPointTotal(attackerSideHero, 1000);
		// The machine is a negative control for the ordinary physical-creature gate.
		giveArtifact(attackerSideHero, ArtifactID::BALLISTA, ArtifactPosition::MACH1);

		startBattle();
		markShooter = addStack(BattleSide::ATTACKER, creatureByName("core:grandElf"),
			BattleHex(leftHex), 10);
		ordinaryShooter = addStack(BattleSide::ATTACKER, creatureByName("core:archer"),
			BattleHex(leftHex + 2), 1000);
		markedTarget = addStack(BattleSide::DEFENDER, creatureByName("core:angel"),
			BattleHex(12, 5), 1000);
		ASSERT_NE(markShooter, nullptr);
		ASSERT_NE(ordinaryShooter, nullptr);
		ASSERT_NE(markedTarget, nullptr);
		EXPECT_EQ(battle()->battleGetOwnerHero(markShooter), attackerSideHero);
		EXPECT_EQ(battle()->battleGetOwnerHero(ordinaryShooter), attackerSideHero);
		beginCombat();
	}

	bool focusMagicFollowupAndShotCreateThreeMarks()
	{
		activate(markShooter);
		if(!castFocusMagic(markShooter, false) || !castFocusMagic(markShooter, true))
			return false;
		activate(markShooter);
		if(!battle()->battleCanShoot(markShooter, markedTarget->getPosition()))
			return false;
		if(!gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
			BattleAction::makeShotAttack(markShooter, markedTarget)))
			return false;

		int validMarks = 0;
		for(const auto & bonus : markedTarget->getExportedBonusList())
		{
			if(bonus->type != BonusType::COMBAT_EVENT_TRIGGER
				|| bonus->subtype != BonusSubtypeID(arcaneBreachTrigger)
				|| bonus->source != BonusSource::SPELL_EFFECT
				|| bonus->sid != BonusSourceID(arcaneBreach))
				continue;
			if(!bonus->parameters)
			{
				ADD_FAILURE() << "Arcane Breach marks must preserve their beneficiary side";
				return false;
			}
			EXPECT_EQ(bonus->duration, BonusDuration::N_TURNS);
			EXPECT_GT(bonus->turnsRemain, 0);
			EXPECT_EQ(bonus->parameters->toCustom<JsonNode>()["beneficiarySide"].Integer(),
				static_cast<int32_t>(BattleSide::ATTACKER));
			++validMarks;
		}
		EXPECT_EQ(validMarks, 3);
		return validMarks == 3;
	}

	void addArcaneMark(CStack * target, BattleSide beneficiary, int32_t turnsRemain = 2) const
	{
		auto mark = std::make_shared<Bonus>(BonusDuration::N_TURNS, BonusType::COMBAT_EVENT_TRIGGER,
			BonusSource::SPELL_EFFECT, newHorizonsSorcery::ARCANE_BREACH_CAP_BASIS_POINTS,
			BonusSourceID(arcaneBreach), BonusSubtypeID(arcaneBreachTrigger));
		mark->turnsRemain = turnsRemain;
		JsonNode parameters;
		parameters["beneficiarySide"].Integer() = static_cast<int32_t>(beneficiary);
		mark->parameters = std::make_shared<BonusParameters>(parameters);
		target->addNewBonus(mark);
	}

	RangedAttackPenetration penetration(const battle::Unit * attacker, const battle::Unit * defender,
		bool shooting = true, bool physical = true) const
	{
		BattleAttackInfo attack(attacker, defender, 0, shooting);
		attack.physicalDamage = physical;
		return battle()->battleGetRangedAttackPenetration(attack);
	}

	void addPdr(CStack * target, int32_t basisPoints, SpellID source) const
	{
		target->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
			BonusType::PHYSICAL_DAMAGE_REDUCTION_BASIS_POINTS, BonusSource::OTHER,
			basisPoints, BonusSourceID(source)));
	}
};
}

TEST_F(NewHorizonsArcaneBallisticsTest, LegalPerkUsesThreeRealBreachMarksForAcceptedRangedShot)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true, true));
	ASSERT_TRUE(attackerSideHero->hasActivePerk(sorcerySkillId, arcaneBallisticsPerkId));
	ASSERT_TRUE(attackerSideHero->hasActivePerk(metamagicSkillId, arcaneAcquisitionPerkId));
	ASSERT_TRUE(focusMagicFollowupAndShotCreateThreeMarks());
	ASSERT_TRUE(ordinaryShooter->isShooter());
	ASSERT_EQ(penetration(ordinaryShooter, markedTarget).physicalDamageReductionIgnorePercent,
		newHorizonsSorcery::ARCANE_BALLISTICS_PDR_IGNORE_PERCENT);

	forceMaximumDamage(ordinaryShooter);
	const BattleAttackInfo attack(ordinaryShooter, markedTarget, 0, true);
	const auto withoutPdr = battle()->calculateDmgRange(attack);
	ASSERT_GT(withoutPdr.damage.min, 0);
	EXPECT_EQ(withoutPdr.damage.min, withoutPdr.damage.max);

	// 20% and 30% remain independent sources: 1 - (0.8 * 0.7) = 44%
	// reduction. Ballistics ignores one quarter of that capped reduction, leaving
	// 67% damage on this stage.
	addPdr(markedTarget, 2000, SpellID(SpellID::SHIELD));
	addPdr(markedTarget, 3000, SpellID(SpellID::AIR_SHIELD));
	const auto penetrated = battle()->calculateDmgRange(attack);
	EXPECT_EQ(penetrated.damageBeforeDefense.min, withoutPdr.damageBeforeDefense.min);
	EXPECT_EQ(penetrated.damageBeforeDefense.max, withoutPdr.damageBeforeDefense.max);
	EXPECT_NEAR(100.0 * penetrated.damage.min / withoutPdr.damage.min, 67.0, 0.1);
	EXPECT_NEAR(100.0 * penetrated.damage.max / withoutPdr.damage.max, 67.0, 0.1);

	const auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	ArcaneBallisticsEnvironment environment(gameState());
	HypotheticBattle projected(&environment, callback);
	const auto projectedAttacker = projected.getForUpdate(ordinaryShooter->unitId());
	const auto projectedDefender = projected.getForUpdate(markedTarget->unitId());
	ASSERT_NE(projectedAttacker, nullptr);
	ASSERT_NE(projectedDefender, nullptr);
	const auto detached = projected.calculateDmgRange(
		BattleAttackInfo(projectedAttacker.get(), projectedDefender.get(), 0, true));
	EXPECT_EQ(detached.damage.min, penetrated.damage.min);
	EXPECT_EQ(detached.damage.max, penetrated.damage.max);
	EXPECT_EQ(projected.battleGetRangedAttackPenetration(
		BattleAttackInfo(projectedAttacker.get(), projectedDefender.get(), 0, true)).physicalDamageReductionIgnorePercent,
		newHorizonsSorcery::ARCANE_BALLISTICS_PDR_IGNORE_PERCENT);

	const auto healthBefore = markedTarget->getAvailableHealth();
	ASSERT_TRUE(battle()->battleCanShoot(ordinaryShooter, markedTarget->getPosition()));
	activate(ordinaryShooter);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeShotAttack(ordinaryShooter, markedTarget)));
	EXPECT_EQ(healthBefore - markedTarget->getAvailableHealth(), penetrated.damage.max);

	// Adding another independent source exceeds the saved 80% cap. Ignoring 25%
	// of the capped reduction leaves 40% damage, not an uncapped source product.
	addPdr(markedTarget, 8000, SpellID(SpellID::STONE_SKIN));
	EXPECT_EQ(penetration(ordinaryShooter, markedTarget).physicalDamageReductionIgnorePercent,
		newHorizonsSorcery::ARCANE_BALLISTICS_PDR_IGNORE_PERCENT);
	const auto cappedAfterShot = battle()->calculateDmgRange(attack);
	EXPECT_NEAR(100.0 * cappedAfterShot.damage.max / withoutPdr.damage.max, 40.0, 0.1);
}

TEST_F(NewHorizonsArcaneBallisticsTest, RequiresThreeCurrentMarksForTheAttackersSideAndPhysicalShot)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true));
	ASSERT_TRUE(attackerSideHero->hasActivePerk(sorcerySkillId, arcaneBallisticsPerkId));
	ASSERT_NE(ordinaryShooter, nullptr);

	auto * twoMarks = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(2, 8), 100);
	auto * wrongSide = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(5, 8), 100);
	auto * expired = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(8, 8), 100);
	auto * valid = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(11, 8), 100);
	ASSERT_NE(twoMarks, nullptr);
	ASSERT_NE(wrongSide, nullptr);
	ASSERT_NE(expired, nullptr);
	ASSERT_NE(valid, nullptr);

	addArcaneMark(twoMarks, BattleSide::ATTACKER);
	addArcaneMark(twoMarks, BattleSide::ATTACKER);
	for(int i = 0; i < 3; ++i)
		addArcaneMark(wrongSide, BattleSide::DEFENDER);
	addArcaneMark(expired, BattleSide::ATTACKER, 0);
	addArcaneMark(expired, BattleSide::ATTACKER);
	addArcaneMark(expired, BattleSide::ATTACKER);
	for(int i = 0; i < 3; ++i)
		addArcaneMark(valid, BattleSide::ATTACKER);

	EXPECT_EQ(penetration(ordinaryShooter, twoMarks).physicalDamageReductionIgnorePercent, 0);
	EXPECT_EQ(penetration(ordinaryShooter, wrongSide).physicalDamageReductionIgnorePercent, 0);
	EXPECT_EQ(penetration(ordinaryShooter, expired).physicalDamageReductionIgnorePercent, 0);
	EXPECT_EQ(penetration(ordinaryShooter, valid).physicalDamageReductionIgnorePercent,
		newHorizonsSorcery::ARCANE_BALLISTICS_PDR_IGNORE_PERCENT);

	EXPECT_EQ(penetration(ordinaryShooter, valid, false).physicalDamageReductionIgnorePercent, 0);
	EXPECT_EQ(penetration(ordinaryShooter, valid, true, false).physicalDamageReductionIgnorePercent, 0);
	const auto machines = battle()->battleGetStacksIf([](const CStack * stack)
	{
		return stack->unitSide() == BattleSide::ATTACKER && stack->isBallista();
	});
	ASSERT_EQ(machines.size(), 1u);
	EXPECT_EQ(penetration(machines.front(), valid).physicalDamageReductionIgnorePercent, 0);
}

TEST_F(NewHorizonsArcaneBallisticsTest, ThreeMarksDoNotGrantPenetrationWithoutBallistics)
{
	ASSERT_NO_FATAL_FAILURE(prepare(false));
	for(int i = 0; i < 3; ++i)
		addArcaneMark(markedTarget, BattleSide::ATTACKER);
	EXPECT_FALSE(attackerSideHero->hasActivePerk(sorcerySkillId, arcaneBallisticsPerkId));
	EXPECT_EQ(penetration(ordinaryShooter, markedTarget).physicalDamageReductionIgnorePercent, 0);
}
