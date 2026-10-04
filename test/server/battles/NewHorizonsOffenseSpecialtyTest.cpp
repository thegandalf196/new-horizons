/*
 * NewHorizonsOffenseSpecialtyTest.cpp, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "HeroCommandFixture.h"

#include "../../../AI/BattleAI/StackWithBonuses.h"
#include "../../../lib/GameConstants.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/CSkillHandler.h"
#include "../../../lib/battle/BattleInfo.h"
#include "../../../lib/battle/CBattleInfoCallback.h"
#include "../../../lib/battle/NewHorizonsCombatSkills.h"
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/bonuses/BonusCustomTypes.h"
#include "../../../lib/bonuses/BonusSelector.h"
#include "../../../lib/callback/GameRandomizer.h"
#include "../../../lib/callback/IGameInfoCallback.h"
#include "../../../lib/callback/IGameRandomizer.h"
#include "../../../lib/entities/hero/CHero.h"
#include "../../../lib/entities/hero/CHeroHandler.h"
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
#include <numeric>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace
{
constexpr std::string_view SPECIALTY_MARKER_PREFIX = "new-horizons:skill-specialty:";
constexpr std::string_view OFFENSE_SKILL_ID = "new-horizons:offense";
constexpr std::string_view EXECUTIONER_PERK_ID = "new-horizons:offense.executioner";
constexpr int UNRELATED_MELEE_BONUS = 7;
constexpr int EXECUTIONER_BONUS = 20;

HeroTypeID heroType(const char * identifier)
{
	return HeroTypeID(HeroTypeID::decode(identifier));
}

CreatureID creature(const char * identifier)
{
	return CreatureID(CreatureID::decode(identifier));
}

SecondarySkill nhOffenseSkill()
{
	const auto decoded = SecondarySkill::decode(std::string(OFFENSE_SKILL_ID));
	if(decoded < 0)
		throw std::runtime_error("New Horizons Offense is not registered");
	return SecondarySkill(decoded);
}

BonusSubtypeID meleeDamageSubtype()
{
	return BonusSubtypeID(BonusCustomSubtype::damageTypeMelee);
}

CSelector coreOffenseDamageSelector()
{
	return Selector::typeSubtypeValueType(BonusType::PERCENTAGE_DAMAGE_BOOST,
		meleeDamageSubtype(), BonusValueType::BASE_NUMBER)
		.And(Selector::source(BonusSource::SECONDARY_SKILL, BonusSourceID(nhOffenseSkill())));
}

int coreOffenseDamage(const battle::Unit & unit)
{
	const auto bonuses = unit.getBonuses(coreOffenseDamageSelector());
	if(!bonuses)
		return 0;
	return std::accumulate(bonuses->begin(), bonuses->end(), 0,
		[](int total, const auto & bonus) { return total + bonus->val; });
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

int specialtyMarkerCount(const CGHeroInstance & hero)
{
	const auto expectedMarker = std::string(SPECIALTY_MARKER_PREFIX)
		+ std::to_string(hero.getHeroTypeID().getNum()) + ":"
		+ std::to_string(SecondarySkill(SecondarySkill::OFFENCE).getNum());
	const auto exported = hero.getExportedBonusList();
	return static_cast<int>(std::count_if(exported.begin(), exported.end(), [&expectedMarker](const auto & bonus)
	{
		return bonus && bonus->type == BonusType::NONE
			&& bonus->source == BonusSource::HERO_SPECIAL
			&& bonus->stacking == expectedMarker;
	}));
}

void setAvailableHealth(CGameHandler & handler, CStack * stack, int64_t desiredHealth)
{
	ASSERT_NE(stack, nullptr);
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
	handler.sendAndApply(update);
}

int64_t expectedDamage(int64_t rawDamage, int percent)
{
	return rawDamage * (100 + percent) / 100;
}

using PrototypeSnapshot = std::tuple<const Bonus *, int, BonusType, BonusSource, BonusSubtypeID,
	BonusValueType, BonusSourceID, std::string>;

std::vector<PrototypeSnapshot> snapshotAliasBonuses(const CHero & type)
{
	std::vector<PrototypeSnapshot> result;
	if(!type.secondarySkillSpecialtyAlias)
		return result;
	for(const auto & bonus : type.secondarySkillSpecialtyAlias->bonuses)
		result.emplace_back(bonus.get(), bonus->val, bonus->type, bonus->source, bonus->subtype,
			bonus->valType, bonus->sid, bonus->stacking);
	return result;
}

void expectAliasBonusesUnchanged(const CHero & type, const std::vector<PrototypeSnapshot> & original)
{
	ASSERT_TRUE(type.secondarySkillSpecialtyAlias);
	const auto & aliases = type.secondarySkillSpecialtyAlias->bonuses;
	ASSERT_EQ(aliases.size(), original.size());
	for(size_t index = 0; index < aliases.size(); ++index)
	{
		EXPECT_EQ(aliases[index].get(), std::get<0>(original[index]));
		EXPECT_EQ(aliases[index]->val, std::get<1>(original[index]));
		EXPECT_EQ(aliases[index]->type, std::get<2>(original[index]));
		EXPECT_EQ(aliases[index]->source, std::get<3>(original[index]));
		EXPECT_EQ(aliases[index]->subtype, std::get<4>(original[index]));
		EXPECT_EQ(aliases[index]->valType, std::get<5>(original[index]));
		EXPECT_EQ(aliases[index]->sid, std::get<6>(original[index]));
		EXPECT_EQ(aliases[index]->stacking, std::get<7>(original[index]));
	}
}

class OffenseProjectionEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;

public:
	explicit OffenseProjectionEnvironment(std::shared_ptr<CGameState> state_)
		: state(std::move(state_))
	{}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class CapturingGrowthRandomizer final : public IGameRandomizer
{
	GameRandomizer delegate;

public:
	explicit CapturingGrowthRandomizer(const IGameInfoCallback & gameInfo)
		: delegate(gameInfo)
	{}
	int capturedCalls = 0;
	int capturedSpecialtyPercent = -1;
	int capturedAttackChance = -1;

	ArtifactID rollArtifact() override { return delegate.rollArtifact(); }
	ArtifactID rollArtifact(EArtifactClass type) override { return delegate.rollArtifact(type); }
	ArtifactID rollArtifact(std::set<ArtifactID> filtered) override { return delegate.rollArtifact(std::move(filtered)); }
	std::vector<ArtifactID> rollMarketArtifactSet() override { return delegate.rollMarketArtifactSet(); }
	CreatureID rollCreature() override { return delegate.rollCreature(); }
	CreatureID rollCreature(int tier) override { return delegate.rollCreature(tier); }
	PrimarySkill rollPrimarySkillForLevelup(const CGHeroInstance * hero) override
	{
		return delegate.rollPrimarySkillForLevelup(hero);
	}
	std::array<int, GameConstants::PRIMARY_SKILLS> rollPrimarySkillsForLevelup(const CGHeroInstance * hero) override
	{
		++capturedCalls;
		capturedSpecialtyPercent = hero->getSkillSpecialtyCoreBonusPercent(SecondarySkill(SecondarySkill::OFFENCE));
		capturedAttackChance = growthChance(*hero, nhOffenseSkill());
		return delegate.rollPrimarySkillsForLevelup(hero);
	}
	SecondarySkill rollSecondarySkillForLevelup(const CGHeroInstance * hero,
		const std::set<SecondarySkill> & candidates) override
	{
		return delegate.rollSecondarySkillForLevelup(hero, candidates);
	}
	std::vector<SecondarySkill> rollSecondarySkills(const CGHeroInstance * hero) override
	{
		return delegate.rollSecondarySkills(hero);
	}
	vstd::RNG & getDefault() override { return delegate.getDefault(); }
};
}

class NewHorizonsOffenseSpecialtyTest : public HeroCommandFixture
{
protected:
	bool omitSkillSpecialtyRules = false;

	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		const auto & activeMods = LIBRARY->modh->getActiveMods();
		if(!vstd::contains(activeMods, GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the activated New Horizons preset";
	}

	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		auto rules = JsonNode(JsonPath::builtin("config/newHorizonsHeroes"));
		if(omitSkillSpecialtyRules)
		{
			rules.Struct().erase("skillSpecialties");
			// Preserve an exact older snapshot instead of merging the new rule back in.
			rules.setOverrideFlag(true);
		}
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, std::move(rules));
	}

	void startSpecialtyMap(const char * specialistId)
	{
		const auto pikeman = creature("core:pikeman");
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).name("NewHorizonsOffenseSpecialty")
			.playerActive(PlayerColor(0)).playerActive(PlayerColor(1))
			.hero({5, 5, 0}, heroType(specialistId), PlayerColor(0)).heroExperience(0)
			.heroGarrison({{pikeman, 1}})
			.hero({7, 7, 0}, heroType("core:solmyr"), PlayerColor(1)).heroExperience(0)
			.heroGarrison({{pikeman, 1}});
		startWithMap(std::move(builder));

		server.gameState = gameState();
		gameHandler = std::make_shared<CGameHandler>(server, gameState());
		gameHandler->randomizer->setSeed(seed);
		attackerSideHero = findHeroAt({5, 5, 0});
		defenderSideHero = findHeroAt({7, 7, 0});
		ASSERT_NE(attackerSideHero, nullptr);
		ASSERT_NE(defenderSideHero, nullptr);
	}

	void neutralize(CGHeroInstance * hero, bool keepOffenseAlias)
	{
		const auto * type = hero->getHeroType();
		for(const auto & bonus : type->specialty)
		{
			if(keepOffenseAlias && type->secondarySkillSpecialtyAlias
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

	static int64_t meleeForecast(const BattleInfo & battle, const battle::Unit * attacker, const battle::Unit * defender)
	{
		return battle.calculateDmgRange(BattleAttackInfo(attacker, defender, 0, false)).damage.max;
	}

	void activateForAttack(CStack * desired)
	{
		for(int attempt = 0; attempt < 64; ++attempt)
		{
			const auto * active = battle()->battleActiveUnit();
			ASSERT_NE(active, nullptr);
			if(active->unitId() == desired->unitId())
				return;
			const auto controller = battle()->battleGetActionController(active);
			BattleAction defend = BattleAction::makeDefend(active);
			ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), controller, defend));
		}
		FAIL() << "The specialist melee stack did not receive an activation";
	}

	void runAliasRankChecks(const char * specialistId)
	{
		startSpecialtyMap(specialistId);
		auto * specialist = attackerSideHero;
		auto * control = defenderSideHero;
		ASSERT_EQ(specialist->getHeroType()->getJsonKey(), specialistId);
		ASSERT_TRUE(specialist->getHeroType()->secondarySkillSpecialtyAlias);
		ASSERT_EQ(specialist->getHeroType()->secondarySkillSpecialtyAlias->skill,
			SecondarySkill(SecondarySkill::OFFENCE));
		ASSERT_FALSE(control->getHeroType()->secondarySkillSpecialtyAlias
			&& control->getHeroType()->secondarySkillSpecialtyAlias->skill == SecondarySkill(SecondarySkill::OFFENCE));
		const auto aliasSnapshot = snapshotAliasBonuses(*specialist->getHeroType());
		ASSERT_FALSE(aliasSnapshot.empty()) << "Use the real legacy Offence alias producer";

		neutralize(specialist, true);
		neutralize(control, false);
		ASSERT_EQ(specialist->getSkillSpecialtyCoreBonusPercent(SecondarySkill(SecondarySkill::OFFENCE)), 20);
		EXPECT_EQ(control->getSkillSpecialtyCoreBonusPercent(SecondarySkill(SecondarySkill::OFFENCE)), 0);
		EXPECT_EQ(specialtyMarkerCount(*specialist), 1);
		EXPECT_EQ(specialtyMarkerCount(*control), 0);

		startBattle();
		// Pikeman maximum damage is fractional per creature here. Doubling the
		// attacker stacks makes the unmodified melee base integral, so the oracle
		// can compare engine-side percentage composition before its final floor.
		auto * specialistAttack = addStack(BattleSide::ATTACKER, creature("core:pikeman"), BattleHex(leftHex), 200);
		auto * specialistTarget = addStack(BattleSide::DEFENDER, creature("core:pikeman"), BattleHex(rightHex), 1000);
		auto * controlAttack = addStack(BattleSide::DEFENDER, creature("core:pikeman"), BattleHex(rightHex + 17), 200);
		auto * controlTarget = addStack(BattleSide::ATTACKER, creature("core:pikeman"), BattleHex(leftHex - 17), 1000);
		auto * rangedAttacker = addStack(BattleSide::ATTACKER, creature("core:archer"), BattleHex(leftHex - 34), 100);
		ASSERT_NE(specialistAttack, nullptr);
		ASSERT_NE(specialistTarget, nullptr);
		ASSERT_NE(controlAttack, nullptr);
		ASSERT_NE(controlTarget, nullptr);
		ASSERT_NE(rangedAttacker, nullptr);
		for(auto * attacker : {specialistAttack, controlAttack, rangedAttacker})
			forceMaximumDamage(attacker);
		BattleTestFixture::blockRetaliation(specialistTarget);
		beginCombat();
		const int64_t rawDamage = meleeForecast(*battle(), specialistAttack, specialistTarget);
		ASSERT_GT(rawDamage, 0);
		const auto rangedBase = battle()->calculateDmgRange(
			BattleAttackInfo(rangedAttacker, specialistTarget, 0, true)).damage.max;

		const auto addUnrelatedMeleeBoost = [](CStack * stack)
		{
			auto bonus = std::make_shared<Bonus>(BonusDuration::PERMANENT,
				BonusType::PERCENTAGE_DAMAGE_BOOST, BonusSource::OTHER, UNRELATED_MELEE_BONUS,
				BonusSourceID(), meleeDamageSubtype());
			bonus->valType = BonusValueType::BASE_NUMBER;
			stack->addNewBonus(bonus);
		};
		addUnrelatedMeleeBoost(specialistAttack);
		addUnrelatedMeleeBoost(controlAttack);

		const auto offense = nhOffenseSkill();
		specialist->setSecSkillLevel(offense, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		control->setSecSkillLevel(offense, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		specialist->applyPerkSelection({std::string(OFFENSE_SKILL_ID), std::string(EXECUTIONER_PERK_ID)});
		control->applyPerkSelection({std::string(OFFENSE_SKILL_ID), std::string(EXECUTIONER_PERK_ID)});
		ASSERT_TRUE(specialist->hasActivePerk(std::string(OFFENSE_SKILL_ID), std::string(EXECUTIONER_PERK_ID)));
		ASSERT_TRUE(control->hasActivePerk(std::string(OFFENSE_SKILL_ID), std::string(EXECUTIONER_PERK_ID)));

		static constexpr std::array<MasteryLevel::Type, 3> ranks = {
			MasteryLevel::BASIC, MasteryLevel::ADVANCED, MasteryLevel::EXPERT};
		static constexpr std::array<int, 3> baseBonuses = {10, 20, 30};
		static constexpr std::array<int, 3> specialtyBonuses = {12, 24, 36};
		static constexpr std::array<int, 3> baseGrowth = {10, 20, 30};
		static constexpr std::array<int, 3> specialtyGrowth = {12, 24, 36};
		int64_t expertForecast = 0;

		for(size_t index = 0; index < ranks.size(); ++index)
		{
			specialist->setSecSkillLevel(offense, ranks[index], ChangeValueMode::ABSOLUTE);
			control->setSecSkillLevel(offense, ranks[index], ChangeValueMode::ABSOLUTE);
			EXPECT_EQ(specialist->getSkillSpecialtyCoreBonusPercent(SecondarySkill(SecondarySkill::OFFENCE)), 20);
			EXPECT_EQ(control->getSkillSpecialtyCoreBonusPercent(SecondarySkill(SecondarySkill::OFFENCE)), 0);
			EXPECT_EQ(coreOffenseDamage(*specialistAttack), specialtyBonuses[index]);
			EXPECT_EQ(coreOffenseDamage(*controlAttack), baseBonuses[index]);
			EXPECT_EQ(growthChance(*specialist, offense), specialtyGrowth[index]);
			EXPECT_EQ(growthChance(*control, offense), baseGrowth[index]);

			const int64_t specialistDamage = meleeForecast(*battle(), specialistAttack, specialistTarget);
			const int64_t controlDamage = meleeForecast(*battle(), controlAttack, controlTarget);
			EXPECT_EQ(specialistDamage,
				expectedDamage(rawDamage, specialtyBonuses[index] + UNRELATED_MELEE_BONUS));
			EXPECT_EQ(controlDamage,
				expectedDamage(rawDamage, baseBonuses[index] + UNRELATED_MELEE_BONUS));
			EXPECT_EQ(specialistAttack->valOfBonuses(BonusType::PERCENTAGE_DAMAGE_BOOST, meleeDamageSubtype()),
				specialtyBonuses[index] + UNRELATED_MELEE_BONUS)
				<< "Only the source-stamped NH core bonus receives the specialty; the OTHER bonus remains +7";
			EXPECT_EQ(battle()->calculateDmgRange(BattleAttackInfo(rangedAttacker, specialistTarget, 0, true)).damage.max,
				rangedBase) << "Core Offense remains melee-only";

			if(index + 1 == ranks.size())
				expertForecast = specialistDamage;
		}

		// A wounded target activates the independent Executioner perk. Its +20%
		// remains present when a projected attacker loses only the exact NH core source.
		setAvailableHealth(*gameHandler, specialistTarget, specialistTarget->getTotalHealth() * 30 / 100);
		setAvailableHealth(*gameHandler, controlTarget, controlTarget->getTotalHealth() * 30 / 100);
		const int64_t executionerForecast = meleeForecast(*battle(), specialistAttack, specialistTarget);
		EXPECT_EQ(executionerForecast,
			expectedDamage(rawDamage, specialtyBonuses.back() + UNRELATED_MELEE_BONUS + EXECUTIONER_BONUS));
		EXPECT_GT(executionerForecast, expertForecast);

		OffenseProjectionEnvironment environment(gameState());
		std::shared_ptr<CBattleInfoCallback> callback(gameState(), battle());
		HypotheticBattle projected(&environment, callback);
		auto projectedAttacker = projected.getForUpdate(specialistAttack->unitId());
		ASSERT_NE(projectedAttacker, nullptr);
		const auto selector = coreOffenseDamageSelector();
		const auto projectedCore = projectedAttacker->getBonuses(selector);
		ASSERT_NE(projectedCore, nullptr);
		ASSERT_EQ(projectedCore->size(), 1u);
		EXPECT_EQ(projectedCore->front()->val, specialtyBonuses.back());
		projectedAttacker->removeUnitBonus(selector);
		const auto removedCore = projectedAttacker->getBonuses(selector);
		EXPECT_TRUE(!removedCore || removedCore->empty());
		EXPECT_EQ(projectedAttacker->valOfBonuses(BonusType::PERCENTAGE_DAMAGE_BOOST, meleeDamageSubtype()),
			UNRELATED_MELEE_BONUS)
			<< "The projected removal preserves an unrelated PERCENTAGE_DAMAGE_BOOST source";
		const int64_t withoutCore = meleeForecast(*battle(), projectedAttacker.get(), specialistTarget);
		EXPECT_EQ(withoutCore,
			expectedDamage(rawDamage, UNRELATED_MELEE_BONUS + EXECUTIONER_BONUS))
			<< "Hero rank does not restore the removed source-stamped core bonus in a projection";

		activateForAttack(specialistAttack);
		const auto * active = battle()->battleActiveUnit();
		ASSERT_NE(active, nullptr);
		ASSERT_EQ(active->unitId(), specialistAttack->unitId());
		ASSERT_EQ(battle()->battleGetActionController(active), PlayerColor(0));
		const int64_t acceptedForecast = meleeForecast(*battle(), specialistAttack, specialistTarget);
		const BattleAction action = BattleAction::makeMeleeAttack(
			specialistAttack, specialistTarget->getPosition(), specialistAttack->getPosition());
		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
		const auto attackResult = std::find_if(server.attacks.begin(), server.attacks.end(),
			[specialistTarget](const auto & result)
			{
				return std::ranges::any_of(result.bsa, [specialistTarget](const auto & hit)
					{ return hit.stackAttacked == specialistTarget->unitId(); });
			});
		ASSERT_NE(attackResult, server.attacks.end());
		const auto hit = std::ranges::find(attackResult->bsa, specialistTarget->unitId(),
			&BattleStackAttacked::stackAttacked);
		ASSERT_NE(hit, attackResult->bsa.end());
		EXPECT_EQ(hit->damageAmount, acceptedForecast)
			<< "An accepted melee attack resolves the fresh shared forecast after intervening activations";

		const auto heroId = specialist->id;
		const auto saved = gameState()->saveToMemory();
		CGameState restored;
		restored.preInit(LIBRARY);
		restored.loadFromMemory(saved);
		auto * loaded = restored.getHero(heroId);
		ASSERT_NE(loaded, nullptr);
		EXPECT_EQ(loaded->getSkillSpecialtyCoreBonusPercent(SecondarySkill(SecondarySkill::OFFENCE)), 20);
		EXPECT_EQ(specialtyMarkerCount(*loaded), 1);
		EXPECT_EQ(growthChance(*loaded, offense), specialtyGrowth.back());

		specialist->setSecSkillLevel(offense, MasteryLevel::NONE, ChangeValueMode::ABSOLUTE);
		EXPECT_EQ(coreOffenseDamage(*specialistAttack), 0);
		EXPECT_EQ(growthChance(*specialist, offense), 0);
		EXPECT_EQ(specialist->getSkillSpecialtyCoreBonusPercent(SecondarySkill(SecondarySkill::OFFENCE)), 20);
		EXPECT_EQ(specialtyMarkerCount(*specialist), 1);
		expectAliasBonusesUnchanged(*specialist->getHeroType(), aliasSnapshot);
	}

};

TEST_F(NewHorizonsOffenseSpecialtyTest, CragHackScalesOnlyCoreOffenseAtEveryRank)
{
	runAliasRankChecks("core:cragHack");
}

TEST_F(NewHorizonsOffenseSpecialtyTest, GundulaScalesOnlyCoreOffenseAtEveryRank)
{
	runAliasRankChecks("core:gundula");
}

TEST_F(NewHorizonsOffenseSpecialtyTest, InitialExperienceSamplerSeesTheSavedOffenseSpecialty)
{
	startSpecialtyMap("core:cragHack");
	const auto * type = attackerSideHero->getHeroType();
	ASSERT_TRUE(type->secondarySkillSpecialtyAlias);

	CGHeroInstance fresh(gameState().get());
	fresh.setOwner(PlayerColor(0));
	fresh.secSkills = {{SecondarySkill(SecondarySkill::OFFENCE), MasteryLevel::BASIC}};
	fresh.exp = LIBRARY->heroh->reqExp(2);
	CapturingGrowthRandomizer randomizer(*gameState());
	fresh.initHero(randomizer, type->getId(), false);

	ASSERT_EQ(fresh.level, 2u);
	ASSERT_EQ(randomizer.capturedCalls, 1);
	EXPECT_EQ(randomizer.capturedSpecialtyPercent, 20);
	EXPECT_EQ(randomizer.capturedAttackChance, 12)
		<< "The initial automatic level-up sampler sees the specialty marker before Attack growth is sampled";
}

TEST_F(NewHorizonsOffenseSpecialtyTest, MissingSavedRulePreservesLegacyAliasWithoutConversion)
{
	omitSkillSpecialtyRules = true;
	startSpecialtyMap("core:gundula");
	auto * hero = attackerSideHero;
	ASSERT_TRUE(hero->getHeroType()->secondarySkillSpecialtyAlias);
	const auto & legacyAlias = hero->getHeroType()->secondarySkillSpecialtyAlias->bonuses;
	ASSERT_FALSE(legacyAlias.empty());
	EXPECT_FALSE(newHorizonsHeroes::skillSpecialtyRules(hero->getPrimaryGrowthRules()));
	EXPECT_EQ(hero->getSkillSpecialtyCoreBonusPercent(SecondarySkill(SecondarySkill::OFFENCE)), 0);
	EXPECT_EQ(specialtyMarkerCount(*hero), 0);
	const auto localBonuses = hero->getExportedBonusList();
	for(const auto & prototypeBonus : legacyAlias)
	{
		EXPECT_TRUE(std::ranges::any_of(localBonuses, [&prototypeBonus](const auto & local)
		{
			return local->type == prototypeBonus->type && local->source == prototypeBonus->source
				&& local->subtype == prototypeBonus->subtype && local->valType == prototypeBonus->valType
				&& local->val == prototypeBonus->val && local->sid == prototypeBonus->sid
				&& local->updater == prototypeBonus->updater;
		})) << "Older snapshots keep the original per-hero legacy alias producer";
	}

	const auto heroId = hero->id;
	const auto saved = gameState()->saveToMemory();
	CGameState restored;
	restored.preInit(LIBRARY);
	restored.loadFromMemory(saved);
	auto * loaded = restored.getHero(heroId);
	ASSERT_NE(loaded, nullptr);
	EXPECT_EQ(loaded->getSkillSpecialtyCoreBonusPercent(SecondarySkill(SecondarySkill::OFFENCE)), 0);
	EXPECT_EQ(specialtyMarkerCount(*loaded), 0);
}

TEST(NewHorizonsOffenseSpecialtyRulesTest, CurrentAndOlderSupportedListsRemainValidAndInvalidListsReject)
{
	auto rules = JsonNode(JsonPath::builtin("config/newHorizonsHeroes"));
	ASSERT_NO_THROW(newHorizonsHeroes::validateHeroRules(rules, true));
	const auto classId = HeroClassID(HeroClassID::decode("core:barbarian"));
	ASSERT_TRUE(classId.hasValue());

	const auto setSpecialtyList = [](JsonNode & target, std::initializer_list<const char *> values)
	{
		auto & list = target["skillSpecialties"]["skills"].Vector();
		list.clear();
		for(const auto * value : values)
			list.emplace_back(std::string(value));
	};
	for(const auto values : {std::initializer_list<const char *>{"core:logistics"},
		std::initializer_list<const char *>{"core:logistics", "core:armorer"},
		std::initializer_list<const char *>{"core:logistics", "core:armorer", "core:offence"}})
	{
		auto supported = rules;
		setSpecialtyList(supported, values);
		EXPECT_NO_THROW(newHorizonsHeroes::validateHeroRules(supported, true));
		const auto snapshot = newHorizonsHeroes::resolveHeroRules(supported, classId);
		EXPECT_NO_THROW(newHorizonsHeroes::validateResolvedHeroRules(snapshot));
	}

	auto duplicate = rules;
	setSpecialtyList(duplicate, {"core:offence", "core:offence"});
	EXPECT_THROW(newHorizonsHeroes::validateHeroRules(duplicate, true), std::runtime_error);
	auto unsupported = rules;
	setSpecialtyList(unsupported, {"core:archery"});
	EXPECT_THROW(newHorizonsHeroes::validateHeroRules(unsupported, true), std::runtime_error);
}
