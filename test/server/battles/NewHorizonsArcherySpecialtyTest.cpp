/*
 * NewHorizonsArcherySpecialtyTest.cpp, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "HeroCommandFixture.h"

#include "../../../lib/GameConstants.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/CSkillHandler.h"
#include "../../../lib/battle/BattleAttackInfo.h"
#include "../../../lib/battle/BattleInfo.h"
#include "../../../lib/battle/NewHorizonsArchery.h"
#include "../../../lib/battle/NewHorizonsCombatSkills.h"
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/bonuses/BonusCustomTypes.h"
#include "../../../lib/entities/hero/CHero.h"
#include "../../../lib/entities/hero/NewHorizonsHeroRules.h"
#include "../../../lib/gameState/CGameState.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../server/CGameHandler.h"
#include "../../../server/battles/BattleProcessor.h"

#include <algorithm>
#include <array>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace
{
constexpr std::string_view NH_ARCHERY_ID = "new-horizons:archery";
constexpr std::string_view UNRELATED_RANGED_DAMAGE_ID = "test:unrelated-ranged-damage";
constexpr int UNRELATED_RANGED_DAMAGE_PERCENT = 50;

HeroTypeID heroType(const char * identifier)
{
	return HeroTypeID(HeroTypeID::decode(identifier));
}

CreatureID creature(const char * identifier)
{
	return CreatureID(CreatureID::decode(identifier));
}

SecondarySkill newHorizonsArcherySkill()
{
	const auto decoded = SecondarySkill::decode(std::string(NH_ARCHERY_ID));
	if(decoded < 0)
		throw std::runtime_error("New Horizons Archery is not registered");
	return SecondarySkill(decoded);
}

int growthChance(const CGHeroInstance & hero, SecondarySkill skill)
{
	const auto view = hero.getPrimaryGrowthView();
	if(!view)
		return -1;
	const auto opportunity = std::find_if(view->extraGrowth.begin(), view->extraGrowth.end(),
		[skill](const auto & entry)
		{
			return entry.skill == skill && entry.attribute == PrimarySkill::ATTACK;
		});
	return opportunity == view->extraGrowth.end() ? 0 : opportunity->chancePercent;
}

int64_t scaledDamage(int64_t rawDamage, int percent)
{
	return rawDamage * (100 + percent) / 100;
}

class NewHorizonsArcherySpecialtyTest : public HeroCommandFixture
{
protected:
	bool useOlderSupportedSpecialtyList = false;

	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the activated New Horizons preset";
	}

	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		auto rules = JsonNode(JsonPath::builtin("config/newHorizonsHeroes"));
		if(useOlderSupportedSpecialtyList)
		{
			auto & skills = rules["skillSpecialties"]["skills"].Vector();
			skills.clear();
			for(const auto skill : {"core:logistics", "core:armorer", "core:offence"})
				skills.emplace_back(std::string(skill));
		}
		// This is the exact rules snapshot that the created hero resolves and saves.
		rules.setOverrideFlag(true);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, std::move(rules));
	}

	void startSpecialtyGame()
	{
		const CreatureID token(0);
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).name("NewHorizonsArcherySpecialty")
			.playerActive(PlayerColor(0)).playerActive(PlayerColor(1))
			.hero({5, 5, 0}, heroType("core:orrin"), PlayerColor(0)).heroExperience(0)
			.heroGarrison({{token, 1}})
			.hero({7, 7, 0}, heroType("core:solmyr"), PlayerColor(1)).heroExperience(0)
			.heroGarrison({{token, 1}});
		startWithMap(std::move(builder));

		server.gameState = gameState();
		gameHandler = std::make_shared<CGameHandler>(server, gameState());
		gameHandler->randomizer->setSeed(seed);
		attackerSideHero = findHeroAt({5, 5, 0});
		defenderSideHero = findHeroAt({7, 7, 0});
		ASSERT_NE(attackerSideHero, nullptr);
		ASSERT_NE(defenderSideHero, nullptr);
	}

	void neutralize(CGHeroInstance * hero, bool preserveArcheryAlias)
	{
		const auto * type = hero->getHeroType();
		for(const auto & bonus : type->specialty)
		{
			if(preserveArcheryAlias && type->secondarySkillSpecialtyAlias
				&& type->secondarySkillSpecialtyAlias->skill == SecondarySkill(SecondarySkill::ARCHERY)
				&& std::ranges::find(type->secondarySkillSpecialtyAlias->bonuses, bonus)
					!= type->secondarySkillSpecialtyAlias->bonuses.end())
				continue;
			hero->removeBonus(bonus);
		}
		for(int index = 0; index < LIBRARY->skillh->size(); ++index)
			hero->setSecSkillLevel(SecondarySkill(index), MasteryLevel::NONE, ChangeValueMode::ABSOLUTE);
		for(const auto skill : {PrimarySkill::ATTACK, PrimarySkill::DEFENSE,
			PrimarySkill::SPELL_POWER, PrimarySkill::KNOWLEDGE})
			hero->setPrimarySkill(skill, 0, ChangeValueMode::ABSOLUTE);
	}

	CStack * addShooter(BattleSide side, BattleHex position)
	{
		auto * shooter = addStack(side, creature("core:angel"), position, 2);
		shooter->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
			BonusType::SHOOTER, BonusSource::OTHER, 1, BonusSourceID()));
		shooter->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
			BonusType::SHOTS, BonusSource::OTHER, 1, BonusSourceID()));
		BattleTestFixture::forceMaximumDamage(shooter);
		return shooter;
	}

	void addUnrelatedRangedBoost(CStack * shooter)
	{
		shooter->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
			BonusType::PERCENTAGE_DAMAGE_BOOST, BonusSource::OTHER, UNRELATED_RANGED_DAMAGE_PERCENT,
			BonusSourceID(), BonusCustomSubtype::damageTypeRanged));
	}

	void activate(const CStack * unit)
	{
		BattleSetActiveStack pack;
		pack.battleID = BattleID(0);
		pack.stack = unit->unitId();
		pack.reason = BattleUnitTurnReason::TURN_QUEUE;
		gameHandler->sendAndApply(pack);
	}
};
}

TEST_F(NewHorizonsArcherySpecialtyTest, OrrinScalesOnlyCoreArcheryAndAcceptedShot)
{
	startSpecialtyGame();
	auto * orrin = attackerSideHero;
	auto * ordinaryHero = defenderSideHero;
	ASSERT_EQ(orrin->getHeroType()->getJsonKey(), "core:orrin");
	ASSERT_TRUE(orrin->getHeroType()->secondarySkillSpecialtyAlias);
	ASSERT_EQ(orrin->getHeroType()->secondarySkillSpecialtyAlias->skill,
		SecondarySkill(SecondarySkill::ARCHERY));
	ASSERT_FALSE(ordinaryHero->getHeroType()->secondarySkillSpecialtyAlias
		&& ordinaryHero->getHeroType()->secondarySkillSpecialtyAlias->skill == SecondarySkill(SecondarySkill::ARCHERY));
	neutralize(orrin, true);
	neutralize(ordinaryHero, false);
	ASSERT_EQ(orrin->getSkillSpecialtyCoreBonusPercent(SecondarySkill(SecondarySkill::ARCHERY)), 20);
	EXPECT_EQ(ordinaryHero->getSkillSpecialtyCoreBonusPercent(SecondarySkill(SecondarySkill::ARCHERY)), 0);

	startBattle();
	auto * specialistShooter = addShooter(BattleSide::ATTACKER, BattleHex(leftHex));
	auto * specialistTarget = addStack(BattleSide::DEFENDER, creature("core:angel"), BattleHex(rightHex + 4), 100);
	auto * ordinaryShooter = addShooter(BattleSide::DEFENDER, BattleHex(3 * GameConstants::BFIELD_WIDTH + 10));
	auto * ordinaryTarget = addStack(BattleSide::ATTACKER, creature("core:angel"),
		BattleHex(3 * GameConstants::BFIELD_WIDTH + 5), 100);
	ASSERT_NE(specialistShooter, nullptr);
	ASSERT_NE(specialistTarget, nullptr);
	ASSERT_NE(ordinaryShooter, nullptr);
	ASSERT_NE(ordinaryTarget, nullptr);
	beginCombat();
	ASSERT_TRUE(battle()->battleCanShoot(specialistShooter, specialistTarget->getPosition()));
	ASSERT_TRUE(battle()->battleCanShoot(ordinaryShooter, ordinaryTarget->getPosition()));
	EXPECT_EQ(battle()->calculateDmgRange(BattleAttackInfo(specialistShooter, specialistTarget, 0, true)).damage.max, 100);
	EXPECT_EQ(battle()->calculateDmgRange(BattleAttackInfo(ordinaryShooter, ordinaryTarget, 0, true)).damage.max, 100);
	const auto specialistMeleeBase = battle()->calculateDmgRange(
		BattleAttackInfo(specialistShooter, specialistTarget, 0, false)).damage.max;
	const auto ordinaryMeleeBase = battle()->calculateDmgRange(
		BattleAttackInfo(ordinaryShooter, ordinaryTarget, 0, false)).damage.max;
	addUnrelatedRangedBoost(specialistShooter);
	addUnrelatedRangedBoost(ordinaryShooter);
	EXPECT_EQ(battle()->calculateDmgRange(BattleAttackInfo(specialistShooter, specialistTarget, 0, true)).damage.max,
		scaledDamage(100, UNRELATED_RANGED_DAMAGE_PERCENT));
	EXPECT_EQ(battle()->calculateDmgRange(BattleAttackInfo(ordinaryShooter, ordinaryTarget, 0, true)).damage.max,
		scaledDamage(100, UNRELATED_RANGED_DAMAGE_PERCENT));

	const auto archery = newHorizonsArcherySkill();
	orrin->setSecSkillLevel(archery, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	ordinaryHero->setSecSkillLevel(archery, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	orrin->applyPerkSelection({std::string(newHorizonsArchery::SKILL),
		std::string(newHorizonsArchery::TARGET_CALLER)});
	ASSERT_TRUE(newHorizonsArchery::hasTargetCaller(orrin));
	activate(specialistShooter);
	const auto * active = battle()->battleActiveUnit();
	ASSERT_NE(active, nullptr);
	ASSERT_EQ(active->unitId(), specialistShooter->unitId());
	ASSERT_EQ(battle()->battleGetActionController(active), PlayerColor(0));
	ASSERT_TRUE(battle()->battleCanBeginHeroCommand(BattleSide::ATTACKER, HeroCommand::FOCUS_FIRE));
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeTargetedHeroCommand(BattleSide::ATTACKER, HeroCommand::FOCUS_FIRE,
			specialistTarget->unitId())));
	EXPECT_EQ(battle()->battleTargetedRangedCommandPercent(specialistShooter, specialistTarget, true), 5);

	static constexpr std::array<MasteryLevel::Type, 3> ranks = {
		MasteryLevel::BASIC, MasteryLevel::ADVANCED, MasteryLevel::EXPERT};
	static constexpr std::array<int, 3> ordinaryPercent = {10, 20, 30};
	static constexpr std::array<int, 3> specialistPercent = {12, 24, 36};
	int64_t expertForecast = 0;
	for(size_t index = 0; index < ranks.size(); ++index)
	{
		orrin->setSecSkillLevel(archery, ranks[index], ChangeValueMode::ABSOLUTE);
		ordinaryHero->setSecSkillLevel(archery, ranks[index], ChangeValueMode::ABSOLUTE);
		EXPECT_EQ(newHorizonsCombatSkills::archeryDamagePercent(orrin), specialistPercent[index]);
		EXPECT_EQ(newHorizonsCombatSkills::archeryDamagePercent(ordinaryHero), ordinaryPercent[index]);
		EXPECT_EQ(growthChance(*orrin, archery), specialistPercent[index]);
		EXPECT_EQ(growthChance(*ordinaryHero, archery), ordinaryPercent[index]);
		const auto specialistForecast = battle()->calculateDmgRange(
			BattleAttackInfo(specialistShooter, specialistTarget, 0, true)).damage.max;
		const auto ordinaryForecast = battle()->calculateDmgRange(
			BattleAttackInfo(ordinaryShooter, ordinaryTarget, 0, true)).damage.max;
		EXPECT_EQ(specialistForecast, scaledDamage(100, UNRELATED_RANGED_DAMAGE_PERCENT
			+ 5 /* Focus Fire */ + newHorizonsArchery::TARGET_CALLER_DAMAGE_PERCENT + specialistPercent[index]));
		EXPECT_EQ(ordinaryForecast, scaledDamage(100, UNRELATED_RANGED_DAMAGE_PERCENT + ordinaryPercent[index]));
		EXPECT_EQ(battle()->calculateDmgRange(
			BattleAttackInfo(specialistShooter, specialistTarget, 0, false)).damage.max, specialistMeleeBase)
			<< "Archery does not alter the melee path";
		EXPECT_EQ(battle()->calculateDmgRange(
			BattleAttackInfo(ordinaryShooter, ordinaryTarget, 0, false)).damage.max, ordinaryMeleeBase);
		if(index + 1 == ranks.size())
			expertForecast = specialistForecast;
	}

	activate(specialistShooter);
	active = battle()->battleActiveUnit();
	ASSERT_NE(active, nullptr);
	ASSERT_EQ(active->unitId(), specialistShooter->unitId());
	ASSERT_EQ(battle()->battleGetActionController(active), PlayerColor(0));
	const auto acceptedForecast = battle()->calculateDmgRange(
		BattleAttackInfo(specialistShooter, specialistTarget, 0, true)).damage.max;
	EXPECT_EQ(acceptedForecast, expertForecast);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeShotAttack(specialistShooter, specialistTarget)));
	const auto attack = std::ranges::find_if(server.attacks, [specialistShooter](const BattleAttack & value)
	{
		return value.stackAttacking == specialistShooter->unitId() && value.shot() && !value.counter();
	});
	ASSERT_NE(attack, server.attacks.end());
	const auto hit = std::ranges::find(attack->bsa, specialistTarget->unitId(), &BattleStackAttacked::stackAttacked);
	ASSERT_NE(hit, attack->bsa.end());
	EXPECT_EQ(hit->damageAmount, acceptedForecast);

	const auto heroId = orrin->id;
	const auto saved = gameState()->saveToMemory();
	CGameState restored;
	restored.preInit(LIBRARY);
	restored.loadFromMemory(saved);
	auto * loaded = restored.getHero(heroId);
	ASSERT_NE(loaded, nullptr);
	EXPECT_EQ(loaded->getSkillSpecialtyCoreBonusPercent(SecondarySkill(SecondarySkill::ARCHERY)), 20);
	EXPECT_EQ(newHorizonsCombatSkills::archeryDamagePercent(loaded), 36);
	orrin->setSecSkillLevel(archery, MasteryLevel::NONE, ChangeValueMode::ABSOLUTE);
	EXPECT_EQ(newHorizonsCombatSkills::archeryDamagePercent(orrin), 0);
	EXPECT_EQ(growthChance(*orrin, archery), 0);
	EXPECT_EQ(orrin->getSkillSpecialtyCoreBonusPercent(SecondarySkill(SecondarySkill::ARCHERY)), 20);
}

TEST_F(NewHorizonsArcherySpecialtyTest, OlderSupportedSpecialtyListKeepsLegacyOrrinAlias)
{
	useOlderSupportedSpecialtyList = true;
	startSpecialtyGame();
	auto * orrin = attackerSideHero;
	ASSERT_TRUE(orrin->getHeroType()->secondarySkillSpecialtyAlias);
	ASSERT_EQ(orrin->getHeroType()->secondarySkillSpecialtyAlias->skill,
		SecondarySkill(SecondarySkill::ARCHERY));
	const auto rules = newHorizonsHeroes::skillSpecialtyRules(orrin->getPrimaryGrowthRules());
	ASSERT_TRUE(rules);
	EXPECT_EQ(rules->skills.size(), 3u);
	EXPECT_EQ(orrin->getSkillSpecialtyCoreBonusPercent(SecondarySkill(SecondarySkill::ARCHERY)), 0);
	const auto & alias = orrin->getHeroType()->secondarySkillSpecialtyAlias->bonuses;
	ASSERT_FALSE(alias.empty());
	const auto local = orrin->getExportedBonusList();
	EXPECT_TRUE(std::ranges::any_of(alias, [&local](const auto & prototype)
	{
		return std::ranges::any_of(local, [&prototype](const auto & bonus)
		{
			return bonus->type == prototype->type && bonus->source == prototype->source
				&& bonus->subtype == prototype->subtype && bonus->valType == prototype->valType
				&& bonus->val == prototype->val && bonus->sid == prototype->sid;
		});
	})) << "The older saved allowlist leaves Orrin's generated legacy alias installed";
	const auto archery = newHorizonsArcherySkill();
	orrin->setSecSkillLevel(archery, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	EXPECT_EQ(newHorizonsCombatSkills::archeryDamagePercent(orrin), 10);
	EXPECT_EQ(growthChance(*orrin, archery), 10);
}
