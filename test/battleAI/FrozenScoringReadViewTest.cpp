/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "ForecastNativeAdapterFixture.h"
#include "../../AI/BattleAI/ForecastNativeReadView.h"
#include "../../AI/BattleAI/StackWithBonuses.h"
#include "../../lib/bonuses/OwnedFrozenScoringSource.h"
#include "../../lib/bonuses/ProjectedBonusView.h"
#include "../../lib/bonuses/BonusList.h"
#include "../../lib/bonuses/Updaters.h"
#include "../../lib/battle/OwnedUnitCreatureResolver.h"
#include "../../lib/IGameSettings.h"
namespace
{
struct FrozenNativeCase
{
	std::vector<forecast::NativeReadOccurrence> occurrences;
	std::shared_ptr<forecast::NativeReadEnvironment> environment;
	std::vector<std::shared_ptr<const forecast::NativeReadView>> views;
};
FrozenNativeCase captureFrozen(HypotheticBattle & source)
{
	FrozenNativeCase result;
	const auto roster = source.battleGetAllUnits(false);
	std::map<CreatureID, battle::OwnedUnitCreatureResolver::Creature> prototypes;
	for(const auto * unit : roster)
	{
		const auto * type = unit->unitType();
		prototypes.try_emplace(type->getId(), battle::OwnedUnitCreatureResolver::Creature{
			type->getId(), type->getFactionID(), type->getLevel(), type->isDoubleWide(), type->getIconIndex(),
			type->warMachine, type->getFullRecruitCost(), type->getJsonKey(), type->getNamePluralTextID(),
			type->getNameSingularTextID(), type->getBaseHitPoints(), type->getAIValue()});
	}
	std::vector<battle::OwnedUnitCreatureResolver::Creature> descriptors;
	for(auto & [id, value] : prototypes) descriptors.push_back(std::move(value));
	auto creatures = std::make_shared<const battle::OwnedUnitCreatureResolver>(std::move(descriptors));
	result.environment = std::make_shared<forecast::NativeReadEnvironment>();
	auto & env = *result.environment;
	const auto sides = battle::ProjectedUnitEnvironmentState::capture(*source.getBattle());
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
	{
		env.inputs.at(side) = sides.inputs(side);
		env.states.at(side) = sides.state(side);
	}
	env.magic = source.getCapturedMagicRules();
	env.skeletonUpgrades = CreatureID(CreatureID::SKELETON).toCreature()->upgrades;
	const auto skill = SecondarySkill::decode("new-horizons:necromancy");
	if(skill >= 0) env.necromancyLuckSource = SecondarySkill(skill);
	const auto & settings = *LIBRARY->engineSettings();
	env.legacyMoraleLimits = std::pair{-static_cast<int32_t>(settings.getVector(EGameSettings::COMBAT_BAD_MORALE_CHANCE).size()),
		static_cast<int32_t>(settings.getVector(EGameSettings::COMBAT_GOOD_MORALE_CHANCE).size())};
	env.legacyLuckLimits = std::pair{-static_cast<int32_t>(settings.getVector(EGameSettings::COMBAT_BAD_LUCK_CHANCE).size()),
		static_cast<int32_t>(settings.getVector(EGameSettings::COMBAT_GOOD_LUCK_CHANCE).size())};
	for(const auto * observed : roster)
	{
		const auto unit = source.getForUpdate(observed->unitId());
		unit->freezeDamageScoringBonusSnapshot();
		auto values = std::make_shared<const battle::UnitStateValueSnapshot>(battle::UnitStateValueSnapshot::capture(*unit));
		auto descriptor = std::make_shared<const forecast::NativeReadDescriptor>(forecast::NativeReadDescriptor{
			values->identity(), {unit->getBattleStartMaximumAggregateHP(), unit->getRebirthOriginalAggregateHP()}, creatures});
		bonusDependencies::QueryFrameBinding binding{unit->unitId(), unit->creatureId(), unit->getCount(),
			unit->unitOwner(), unit->unitSide(), unit->unitSlot(), unit->isGhost(), unit->summoned, unit->natureSummoned, creatures};
		auto scoring = unit->captureFrozenScoringSource(binding);
		if(!scoring) throw std::logic_error("Native test source has no owned frozen scoring snapshot");
		result.occurrences.push_back({std::move(descriptor), std::move(values), {}, {}, std::move(scoring)});
		env.warfare[unit->unitId()] = source.unitWarfareStatBonus(unit.get());
	}
	result.views = forecast::NativeReadView::fromOwned(result.occurrences, result.environment);
	return result;
}
struct NativeObservation
{
	JsonNode state;
	std::array<int64_t, 38> values;
	bool operator==(const NativeObservation &) const = default;
};
NativeObservation observe(battle::CUnitState & unit)
{
	// Values come from the REAL native APIs, never reimplemented arithmetic.
	const std::array<int64_t, 38> values{unit.getCount(), unit.getAvailableHealth(), unit.getFirstHPleft(),
		unit.alive(), unit.isGhost(), unit.moved(), unit.waited(), unit.defended(), unit.canMove(),
		unit.shots.available(), unit.shots.total(), unit.shots.isLimited(),
		unit.counterAttacks.available(), unit.counterAttacks.total(), unit.counterAttacks.isLimited(),
		unit.casts.available(), unit.getAttack(false), unit.getDefense(false), unit.getMinDamage(false),
		unit.getMaxDamage(false), unit.getMovementRange(), unit.getInitiative(), unit.getMaxHealth(), unit.battleQueuePhase(0),
		unit.getAttack(true), unit.getDefense(true), unit.getMinDamage(true), unit.getMaxDamage(true),
		unit.getTotalAttacks(false), unit.getTotalAttacks(true), unit.isShooter(), unit.canShoot(), unit.canCast(),
		unit.magicResistance(), unit.moraleVal(), unit.luckVal(),
		unit.getBattleStartMaximumAggregateHP(), unit.getRebirthOriginalAggregateHP()};
	// Native retaliation totals have a monotonic cache: observe it BEFORE the
	// native save, so repeated observations compare the same frozen state.
	return {unit.save(), values};
}
}
TEST_F(ForecastNativeAdapterTest, FrozenScoringOwnNativeLoanKeepsAnswersAcrossCountAndSourceWorldRetirement)
{
	ASSERT_NO_FATAL_FAILURE(prepare(false,false,false,"core:archer",true));
	const auto id=parent->battleGetAllUnits(false).front()->unitId();
	auto original=parent->getForUpdate(id);
	Bonus frozen(BonusDuration::PERMANENT,BonusType::PHYSICAL_DEFENSE,BonusSource::OTHER,7,BonusSourceID());
	frozen.updater=std::make_shared<TimesStackSizeUpdater>(0,100,9,1);
	frozen.limiter=std::make_shared<CreatureLevelLimiter>(99,100); // Materialized local metadata is not rerun.
	original->addUnitBonus({frozen});
	original->freezeDamageScoringBonusSnapshot();
	const auto before=observe(*original);
	auto data=captureFrozen(*parent);
	auto acquisition=forecast::NativeReadView::acquireFromOwned(data.occurrences,data.environment,id);
	ASSERT_TRUE(acquisition.unit); EXPECT_EQ(acquisition.owner->work().materialized,0u);
	EXPECT_TRUE(acquisition.owner->scoringSource()); EXPECT_FALSE(acquisition.owner->queryFrame());
	EXPECT_EQ(acquisition.owner->work().materialized,0u);
	EXPECT_EQ(observe(*acquisition.unit),before);
	const auto proof=forecast::NativeReadView::querySource(*acquisition.unit); ASSERT_TRUE(proof);
	EXPECT_TRUE(proof->scoring); EXPECT_FALSE(proof->modifiers); EXPECT_FALSE(proof->queries);
	int64_t amount=original->getFirstHPleft(); ASSERT_GT(amount,0);
	int64_t actualAmount=amount; original->damage(amount); acquisition.unit->damage(actualAmount);
	EXPECT_EQ(actualAmount,amount); EXPECT_LT(acquisition.unit->getCount(),before.values[0]);
	EXPECT_EQ(observe(*acquisition.unit),observe(*original));
	EXPECT_EQ(acquisition.unit->valOfBonuses(BonusType::PHYSICAL_DEFENSE),7);
	const auto after=observe(*acquisition.unit); auto nested=acquisition.unit->acquireState();
	EXPECT_EQ(observe(*nested),after); EXPECT_EQ(acquisition.owner->work().queryRebindings,0u);
	const auto sourceQueries=forecast::NativeReadView::querySource(*nested); ASSERT_TRUE(sourceQueries);
	EXPECT_TRUE(sourceQueries->scoring); EXPECT_FALSE(sourceQueries->queries);
	const auto siblings=forecast::NativeReadView::forkViews(data.views);
	EXPECT_EQ(siblings.front()->unit().getCount(),before.values[0]);
	const std::weak_ptr<CGameState> world=gameState();
	original.reset(); parent.reset(); visible.reset(); handler.reset(); server.gameState.reset(); environment.reset();
	TinyMapGameTest::TearDown(); EXPECT_TRUE(world.expired());
	data.views.clear(); data.occurrences.clear(); acquisition.unit.reset(); acquisition.owner.reset();
	EXPECT_EQ(observe(*nested),after); EXPECT_EQ(nested->valOfBonuses(BonusType::PHYSICAL_DEFENSE),7);
}
TEST_F(ForecastNativeAdapterTest, FrozenScoringNativePostprocessorUsesCurrentOwnedVeteranFlagAndNoRawProvenance)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto id=parent->battleGetAllUnits(false).front()->unitId();
	const auto native=parent->getForUpdate(id); native->veteranCohesionEarned=true;
	auto data=captureFrozen(*parent);
	auto acquisition=forecast::NativeReadView::acquireFromOwned(data.occurrences,data.environment,id);
	ASSERT_TRUE(acquisition.unit);
	for(bool earned:{true,false,true})
	{
		native->veteranCohesionEarned=earned; acquisition.unit->veteranCohesionEarned=earned;
		for(bool unstacked:{false,true})
		{
			const auto actual=acquisition.unit->getBonusesBeforeCreatureAbilitySuppression(Selector::all,{},unstacked);
			const auto expected=native->getBonusesBeforeCreatureAbilitySuppression(Selector::all,{},unstacked);
			EXPECT_EQ(actual->size(),expected->size());
			EXPECT_EQ(actual->valOfBonuses(Selector::type()(BonusType::MORALE)),expected->valOfBonuses(Selector::type()(BonusType::MORALE)));
		}
	}
	const auto * provider=acquisition.unit->getBonusBearer();
	const auto * projected=dynamic_cast<const ProjectedBonusView *>(provider); ASSERT_NE(projected,nullptr);
	EXPECT_THROW(projected->sourceLocalSpellEffects(),std::logic_error);
	auto bad=data.occurrences; bad.front().scoring.reset();
	EXPECT_THROW(forecast::NativeReadView::fromOwned(bad,data.environment),std::invalid_argument);
	bad=data.occurrences; bad.front().queries=std::make_shared<const bonusDependencies::OwnedBonusQueryFrame>();
	EXPECT_THROW(forecast::NativeReadView::fromOwned(bad,data.environment),std::invalid_argument);
}
