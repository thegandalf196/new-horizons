/*
 * NewHorizonsArmorerSpecialtyTest.cpp, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "HeroCommandFixture.h"

#include "../../../lib/GameConstants.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/CSkillHandler.h"
#include "../../../lib/battle/NewHorizonsCombatSkills.h"
#include "../../../lib/bonuses/Bonus.h"
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

#include <algorithm>
#include <array>
#include <memory>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>

namespace
{
constexpr std::string_view SPECIALTY_MARKER_PREFIX = "new-horizons:skill-specialty:";

HeroTypeID heroType(const char * identifier)
{
	return HeroTypeID(HeroTypeID::decode(identifier));
}

CreatureID creature(const char * identifier)
{
	return CreatureID(CreatureID::decode(identifier));
}

SecondarySkill nhArmorer()
{
	return SecondarySkill(SecondarySkill::decode("new-horizons:armorer"));
}

int growthChance(const CGHeroInstance & hero, SecondarySkill skill)
{
	const auto view = hero.getPrimaryGrowthView();
	if(!view)
		return -1;
	const auto opportunity = std::find_if(view->extraGrowth.begin(), view->extraGrowth.end(),
		[skill](const auto & entry)
		{
			return entry.skill == skill && entry.attribute == PrimarySkill::DEFENSE;
		});
	return opportunity == view->extraGrowth.end() ? 0 : opportunity->chancePercent;
}

size_t specialtyMarkerCount(const CGHeroInstance & hero)
{
	const auto bonuses = hero.getExportedBonusList();
	return std::count_if(bonuses.begin(), bonuses.end(), [](const auto & bonus)
	{
		return bonus && bonus->stacking.starts_with(SPECIALTY_MARKER_PREFIX);
	});
}

class CapturingGrowthRandomizer final : public IGameRandomizer
{
	GameRandomizer delegate;

public:
	explicit CapturingGrowthRandomizer(const IGameInfoCallback & gameInfo)
		: delegate(gameInfo)
	{}

	int capturedCalls = 0;
	int capturedSpecialtyPercent = -1;
	int capturedDefenseChance = -1;

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
		capturedSpecialtyPercent = hero->getSkillSpecialtyCoreBonusPercent(SecondarySkill(SecondarySkill::ARMORER));
		capturedDefenseChance = growthChance(*hero, nhArmorer());
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

class NewHorizonsArmorerSpecialtyTest : public HeroCommandFixture
{
protected:
	bool omitSkillSpecialtyRules = false;

	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		const auto & activeMods = LIBRARY->modh->getActiveMods();
		if(std::find(activeMods.begin(), activeMods.end(), GameConstants::NEW_HORIZONS_MOD_SCOPE) == activeMods.end())
			GTEST_SKIP() << "Requires the activated New Horizons preset";
	}

	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		JsonNode rules(JsonPath::builtin("config/newHorizonsHeroes"));
		if(omitSkillSpecialtyRules)
		{
			rules.Struct().erase("skillSpecialties");
			// Map overrides merge over installed defaults; this models an older
			// hero-development snapshot with no specialty conversion declaration.
			rules.setOverrideFlag(true);
		}
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, rules);
	}

	void startSpecialtyMap(const char * specialtyId)
	{
		const auto pikeman = creature("core:pikeman");
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false)
			.playerActive(PlayerColor(0)).playerActive(PlayerColor(1))
			.hero({5, 5, 0}, heroType("core:solmyr"), PlayerColor(0)).heroExperience(0)
			.heroGarrison({{pikeman, 1}})
			.hero({7, 7, 0}, heroType(specialtyId), PlayerColor(1)).heroExperience(0)
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

	void makeNeutral(CGHeroInstance * hero, bool keepSkillAliasSuppressed)
	{
		const auto * heroType = hero->getHeroType();
		for(const auto & bonus : heroType->specialty)
		{
			if(keepSkillAliasSuppressed && heroType->secondarySkillSpecialtyAlias
				&& std::ranges::find(heroType->secondarySkillSpecialtyAlias->bonuses, bonus)
					!= heroType->secondarySkillSpecialtyAlias->bonuses.end())
				continue;
			hero->removeBonus(bonus);
		}
		for(int index = 0; index < LIBRARY->skillh->size(); ++index)
			hero->setSecSkillLevel(SecondarySkill(index), MasteryLevel::NONE, ChangeValueMode::ABSOLUTE);
		for(const auto skill : {PrimarySkill::ATTACK, PrimarySkill::DEFENSE,
			PrimarySkill::SPELL_POWER, PrimarySkill::KNOWLEDGE})
			hero->setPrimarySkill(skill, 0, ChangeValueMode::ABSOLUTE);
	}

	void runAliasRankChecks(const char * specialtyId, bool acceptedAttack)
	{
		startSpecialtyMap(specialtyId);
		auto * specialist = defenderSideHero;
		auto * control = attackerSideHero;
		ASSERT_EQ(specialist->getHeroType()->getJsonKey(), specialtyId);
		ASSERT_TRUE(specialist->getHeroType()->secondarySkillSpecialtyAlias);
		ASSERT_EQ(specialist->getHeroType()->secondarySkillSpecialtyAlias->skill,
			SecondarySkill(SecondarySkill::ARMORER));
		ASSERT_FALSE(control->getHeroType()->secondarySkillSpecialtyAlias
			&& control->getHeroType()->secondarySkillSpecialtyAlias->skill == SecondarySkill(SecondarySkill::ARMORER));

		const auto aliasBonuses = specialist->getHeroType()->secondarySkillSpecialtyAlias->bonuses;
		ASSERT_FALSE(aliasBonuses.empty()) << "This must exercise the real legacy core Armorer producer";
		std::vector<std::tuple<const Bonus *, int, BonusType, std::string>> prototypeSnapshot;
		for(const auto & bonus : specialist->getHeroType()->specialty)
			prototypeSnapshot.emplace_back(bonus.get(), bonus->val, bonus->type, bonus->stacking);

		makeNeutral(specialist, true);
		makeNeutral(control, false);
		const auto unrelatedDefense = [](CGHeroInstance * hero)
		{
			hero->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
				BonusType::PRIMARY_SKILL, BonusSource::OTHER, 2, BonusSourceID(),
				BonusSubtypeID(PrimarySkill::DEFENSE)));
		};
		unrelatedDefense(specialist);
		unrelatedDefense(control);

		ASSERT_EQ(specialist->getSkillSpecialtyCoreBonusPercent(SecondarySkill(SecondarySkill::ARMORER)), 20);
		EXPECT_EQ(control->getSkillSpecialtyCoreBonusPercent(SecondarySkill(SecondarySkill::ARMORER)), 0);
		EXPECT_EQ(specialtyMarkerCount(*specialist), 1u);
		EXPECT_EQ(specialtyMarkerCount(*control), 0u);
		for(const auto & aliasBonus : aliasBonuses)
		{
			const auto exported = specialist->getExportedBonusList();
			EXPECT_EQ(std::ranges::count(exported, aliasBonus), 0)
				<< "Only the exact alias-generated prototype pointer is replaced on the converted hero";
		}
		EXPECT_EQ(specialist->getPrimSkillLevel(PrimarySkill::DEFENSE), 2)
			<< "An unrelated Defense modifier remains intact";

		static constexpr std::array<MasteryLevel::Type, 3> ranks = {
			MasteryLevel::BASIC, MasteryLevel::ADVANCED, MasteryLevel::EXPERT};
		static constexpr std::array<int, 3> baseReduction = {5, 10, 15};
		static constexpr std::array<int, 3> convertedReduction = {6, 12, 18};
		static constexpr std::array<int, 3> baseGrowthChance = {10, 20, 30};
		static constexpr std::array<int, 3> convertedGrowthChance = {12, 24, 36};

		const auto resolvedRules = newHorizonsHeroes::skillSpecialtyRules(specialist->getPrimaryGrowthRules());
		ASSERT_TRUE(resolvedRules);
		EXPECT_EQ(resolvedRules->version, 1);
		EXPECT_EQ(resolvedRules->coreBonusPercent, 20);
		EXPECT_TRUE(std::ranges::find(resolvedRules->skills, SecondarySkill(SecondarySkill::ARMORER))
			!= resolvedRules->skills.end());

		CMemorySerializer memory;
		memory.oser.version = ESerializationVersion::CURRENT;
		memory.iser.version = ESerializationVersion::CURRENT;
		memory.oser & *gameState();
		CGameState restored;
		memory.iser.cb = &restored;
		memory.iser.loadingGamestate = true;
		memory.iser & restored;
		auto * loadedSpecialist = restored.getHero(specialist->id);
		ASSERT_NE(loadedSpecialist, nullptr);
		EXPECT_EQ(loadedSpecialist->getSkillSpecialtyCoreBonusPercent(SecondarySkill(SecondarySkill::ARMORER)), 20);
		EXPECT_EQ(specialtyMarkerCount(*loadedSpecialist), 1u)
			<< "The exact local conversion marker and saved supported-skill rule survive save/load";
		loadedSpecialist->setSecSkillLevel(nhArmorer(), MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
		EXPECT_EQ(newHorizonsCombatSkills::armorerReductionPercent(loadedSpecialist), 18);

		startBattle();
		auto * attackingStack = addStack(BattleSide::ATTACKER, creature("core:angel"), BattleHex(leftHex), 100);
		auto * defendingStack = addStack(BattleSide::DEFENDER, creature("core:angel"), BattleHex(rightHex), 100);
		ASSERT_NE(attackingStack, nullptr);
		ASSERT_NE(defendingStack, nullptr);
		forceMaximumDamage(attackingStack);
		forceMaximumDamage(defendingStack);
		beginCombat();
		BattleAttackInfo forecast(attackingStack, defendingStack, 0, false);
		const int64_t baselineDamage = battle()->calculateDmgRange(forecast).damage.max;
		ASSERT_GT(baselineDamage, 0);

		for(size_t index = 0; index < ranks.size(); ++index)
		{
			specialist->setSecSkillLevel(nhArmorer(), ranks[index], ChangeValueMode::ABSOLUTE);
			control->setSecSkillLevel(nhArmorer(), ranks[index], ChangeValueMode::ABSOLUTE);
			EXPECT_EQ(newHorizonsCombatSkills::armorerReductionPercent(specialist), convertedReduction[index]);
			EXPECT_EQ(newHorizonsCombatSkills::armorerReductionPercent(control), baseReduction[index]);
			EXPECT_EQ(growthChance(*specialist, nhArmorer()), convertedGrowthChance[index]);
			EXPECT_EQ(growthChance(*control, nhArmorer()), baseGrowthChance[index]);
			EXPECT_EQ(specialist->getPrimSkillLevel(PrimarySkill::DEFENSE), 2)
				<< "Skill conversion must not mutate Primary Attributes or unrelated Defense bonuses";
			forecast = BattleAttackInfo(attackingStack, defendingStack, 0, false);
			EXPECT_EQ(battle()->calculateDmgRange(forecast).damage.max,
				baselineDamage * (100 - convertedReduction[index]) / 100)
				<< "The shared physical-attack forecast applies the converted reduction at every rank";

			if(ranks[index] == MasteryLevel::BASIC)
			{
				// The canonical perk registry requires one Basic-tier selection
				// before an Advanced-tier Armorer perk can be accepted.
				specialist->applyPerkSelection({"new-horizons:armorer", "new-horizons:armorer.pavise"});
				control->applyPerkSelection({"new-horizons:armorer", "new-horizons:armorer.pavise"});
				ASSERT_TRUE(specialist->hasActivePerk("new-horizons:armorer", "new-horizons:armorer.pavise"));
				ASSERT_TRUE(control->hasActivePerk("new-horizons:armorer", "new-horizons:armorer.pavise"));
				EXPECT_EQ(newHorizonsCombatSkills::paviseReductionPercent(specialist),
					newHorizonsCombatSkills::PAVISE_REDUCTION_PERCENT);
				EXPECT_EQ(newHorizonsCombatSkills::paviseReductionPercent(control),
					newHorizonsCombatSkills::PAVISE_REDUCTION_PERCENT)
					<< "The specialty does not amplify Basic Pavise";
			}
			if(ranks[index] == MasteryLevel::ADVANCED)
			{
				specialist->applyPerkSelection({"new-horizons:armorer", "new-horizons:armorer.formationFighting"});
				control->applyPerkSelection({"new-horizons:armorer", "new-horizons:armorer.formationFighting"});
				EXPECT_EQ(newHorizonsCombatSkills::formationFightingReductionPercent(specialist),
					newHorizonsCombatSkills::FORMATION_FIGHTING_REDUCTION_PERCENT);
				EXPECT_EQ(newHorizonsCombatSkills::formationFightingReductionPercent(control),
					newHorizonsCombatSkills::FORMATION_FIGHTING_REDUCTION_PERCENT)
					<< "Skill specialty never amplifies a perk's independent reduction";
			}
		}

		if(acceptedAttack)
		{
			forecast = BattleAttackInfo(attackingStack, defendingStack, 0, false);
			const int64_t expectedDamage = battle()->calculateDmgRange(forecast).damage.max;
			ASSERT_GT(expectedDamage, 0);
			ASSERT_TRUE(attack(attackingStack, defendingStack->getPosition()));
			ASSERT_FALSE(server.attacks.empty());
			const auto attackResult = std::find_if(server.attacks.begin(), server.attacks.end(),
				[&](const auto & result)
				{
					return std::ranges::any_of(result.bsa, [defendingStack](const auto & hit)
					{
						return hit.stackAttacked == defendingStack->unitId();
					});
				});
			ASSERT_NE(attackResult, server.attacks.end());
			const auto hit = std::ranges::find(attackResult->bsa, defendingStack->unitId(),
				&BattleStackAttacked::stackAttacked);
			ASSERT_NE(hit, attackResult->bsa.end());
			EXPECT_EQ(hit->damageAmount, expectedDamage)
				<< "The authoritative accepted physical attack matches the shared damage forecast";
		}

		// Removing the active rank removes both rank-driven core effects, while the
		// hero-scoped marker remains stable and can support a later rank gain.
		specialist->setSecSkillLevel(nhArmorer(), MasteryLevel::NONE, ChangeValueMode::ABSOLUTE);
		EXPECT_EQ(newHorizonsCombatSkills::armorerReductionPercent(specialist), 0);
		EXPECT_EQ(growthChance(*specialist, nhArmorer()), 0);
		EXPECT_EQ(specialist->getSkillSpecialtyCoreBonusPercent(SecondarySkill(SecondarySkill::ARMORER)), 20);
		EXPECT_EQ(specialtyMarkerCount(*specialist), 1u);

		ASSERT_EQ(specialist->getHeroType()->specialty.size(), prototypeSnapshot.size());
		for(size_t index = 0; index < prototypeSnapshot.size(); ++index)
		{
			EXPECT_EQ(specialist->getHeroType()->specialty[index].get(), std::get<0>(prototypeSnapshot[index]));
			EXPECT_EQ(specialist->getHeroType()->specialty[index]->val, std::get<1>(prototypeSnapshot[index]));
			EXPECT_EQ(specialist->getHeroType()->specialty[index]->type, std::get<2>(prototypeSnapshot[index]));
			EXPECT_EQ(specialist->getHeroType()->specialty[index]->stacking, std::get<3>(prototypeSnapshot[index]));
		}

	}
};

TEST_F(NewHorizonsArmorerSpecialtyTest, MephalaUsesTheCanonicalSpecialtyAtEveryRank)
{
	runAliasRankChecks("core:mephala", true);
}

TEST_F(NewHorizonsArmorerSpecialtyTest, TazarUsesTheCanonicalSpecialtyAtEveryRank)
{
	runAliasRankChecks("core:tazar", false);
}

TEST_F(NewHorizonsArmorerSpecialtyTest, NeelaUsesTheCanonicalSpecialtyAtEveryRank)
{
	runAliasRankChecks("core:neela", false);
}

TEST_F(NewHorizonsArmorerSpecialtyTest, InitialExperienceRollSeesTheSavedSpecialtyBeforeItSamplesDefense)
{
	startSpecialtyMap("core:mephala");
	auto * alias = defenderSideHero->getHeroType();
	ASSERT_TRUE(alias->secondarySkillSpecialtyAlias);

	CGHeroInstance fresh(gameState().get());
	fresh.setOwner(PlayerColor(1));
	fresh.secSkills = {{SecondarySkill(SecondarySkill::ARMORER), MasteryLevel::BASIC}};
	fresh.exp = LIBRARY->heroh->reqExp(2);
	CapturingGrowthRandomizer randomizer(*gameState());
	fresh.initHero(randomizer, alias->getId(), false);

	EXPECT_EQ(fresh.level, 2u);
	ASSERT_EQ(randomizer.capturedCalls, 1);
	EXPECT_EQ(randomizer.capturedSpecialtyPercent, 20);
	EXPECT_EQ(randomizer.capturedDefenseChance, 12)
		<< "The initial automatic level-up sampler must observe the already-installed marker";
	EXPECT_EQ(growthChance(fresh, nhArmorer()),
		std::min(100, static_cast<int>(fresh.getSecSkillLevel(nhArmorer())) * 12));
}

TEST_F(NewHorizonsArmorerSpecialtyTest, MissingSavedSkillSpecialtyRulePreservesTheLegacyAliasAndDoesNotConvert)
{
	omitSkillSpecialtyRules = true;
	startSpecialtyMap("core:tazar");
	auto * hero = defenderSideHero;
	ASSERT_TRUE(hero->getHeroType()->secondarySkillSpecialtyAlias);
	const auto aliasBonuses = hero->getHeroType()->secondarySkillSpecialtyAlias->bonuses;
	ASSERT_FALSE(aliasBonuses.empty());
	EXPECT_FALSE(newHorizonsHeroes::skillSpecialtyRules(hero->getPrimaryGrowthRules()));
	EXPECT_EQ(hero->getSkillSpecialtyCoreBonusPercent(SecondarySkill(SecondarySkill::ARMORER)), 0);
	EXPECT_EQ(specialtyMarkerCount(*hero), 0u);

	const auto exported = hero->getExportedBonusList();
	for(const auto & aliasBonus : aliasBonuses)
		EXPECT_NE(std::ranges::find(exported, aliasBonus), exported.end())
			<< "A saved snapshot without the conversion rule keeps its exact legacy alias producer";

	const auto skill = nhArmorer();
	hero->setSecSkillLevel(skill, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	EXPECT_EQ(newHorizonsCombatSkills::armorerReductionPercent(hero), 15);
	EXPECT_EQ(growthChance(*hero, skill), 30);
}
