/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../../lib/nativeRules/NativeRulePilots.h"
#include "../../lib/CSkillHandler.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/CCreatureHandler.h"
#include "../../lib/combatScripts/NativeJsonParameters.h"
#include "../../lib/bonuses/BonusList.h"
#include "../../lib/bonuses/BonusParameters.h"
#include "../../lib/bonuses/CBonusSystemNode.h"
#include "../../lib/bonuses/Limiters.h"
#include "../../lib/bonuses/Propagators.h"
#include "../../lib/bonuses/Updaters.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include <array>

namespace
{
std::vector<std::byte> wire(const Bonus & bonus)
{
	CMemorySerializer serializer;
	serializer.oser & bonus;
	return serializer.extractBuffer();
}

Bonus rankBonus(int amount = 2)
{
	return Bonus(BonusDuration::PERMANENT, BonusType::MORALE,
		BonusSource::SECONDARY_SKILL, amount, BonusSourceID(SecondarySkill(SecondarySkill::LEADERSHIP)));
}

class RankSkillHandler : public CSkillHandler
{
public:
	using CSkillHandler::loadFromJson;
};
}

TEST(PreparedNativeBonusRule, ValidatesSourceTargetStageLifetimeAndVersion)
{
	const auto prototype = rankBonus();
	const nativeRules::BonusRuleContract valid;
	EXPECT_NO_THROW(nativeRules::PreparedBonusRule(valid, prototype));
	for(int invalid = 0; invalid < 5; ++invalid)
	{
		auto contract = valid;
		if(invalid == 0) contract.version = 2;
		if(invalid == 1) contract.source = nativeRules::SourceKind::HeroSpecialty;
		if(invalid == 2) contract.target = nativeRules::TargetDomain::SpellRecipient;
		if(invalid == 3) contract.stage = nativeRules::EvaluationStage::CastApplication;
		if(invalid == 4) contract.lifetime = nativeRules::LifetimePolicy::ParentStatus;
		EXPECT_THROW(nativeRules::PreparedBonusRule(contract, prototype), std::invalid_argument);
	}
}

TEST(PreparedNativeBonusRule, ImmutablePrototypeProducesIndependentApplications)
{
	auto prototype = rankBonus();
	const nativeRules::PreparedBonusRule prepared({}, prototype);
	prototype.val = 99;
	auto first = prepared.attach();
	auto sibling = prepared.attach();
	EXPECT_EQ(first->val, 2);
	EXPECT_EQ(sibling->val, 2);
	first->val = 77;
	EXPECT_EQ(sibling->val, 2);
	EXPECT_EQ(prepared.preparedBonus().val, 2);
	EXPECT_NE(first.get(), sibling.get());
}

TEST(PreparedNativeBonusRule, RankAdapterPreservesDeferredIdentifierCallbackIdentity)
{
	auto original = std::make_shared<Bonus>(rankBonus());
	const auto before = wire(*original);
	const auto prepared = nativeRules::prepareSkillRankBonus(original);
	EXPECT_EQ(prepared.get(), original.get());
	EXPECT_EQ(wire(*prepared), before);
	// An existing content-loader callback can still finish this object before
	// loader finalization; preparation has not redirected it into a stale copy.
	original->subtype = BonusSubtypeID(PrimarySkill::DEFENSE);
	EXPECT_EQ(prepared->subtype, BonusSubtypeID(PrimarySkill::DEFENSE));
	EXPECT_THROW(nativeRules::prepareSkillRankBonus(nullptr), std::invalid_argument);
}

TEST(PreparedNativeBonusRule, GenuineBonusBackedRanksKeepSourceDescriptionAndMagnitude)
{
	JsonNode definition;
	definition["name"].String() = "Prepared rank pilot";
	const std::array<std::string, 3> levels{"basic", "advanced", "expert"};
	for(int rank = 1; rank <= 3; ++rank)
	{
		auto & effect = definition[levels.at(rank - 1)]["effects"]["morale"];
		effect["type"].String() = "MORALE";
		effect["val"].Integer() = rank;
	}
	RankSkillHandler loader;
	const auto skill = loader.loadFromJson("vcmi-test", definition, "preparedRankPilot", SecondarySkill::LEADERSHIP);
	ASSERT_TRUE(skill);
	for(int rank = 1; rank <= 3; ++rank)
	{
		ASSERT_EQ(skill->at(rank).effects.size(), 1u);
		const auto & prepared = skill->at(rank).effects.front();
		EXPECT_EQ(prepared->source, BonusSource::SECONDARY_SKILL);
		EXPECT_EQ(prepared->sid, BonusSourceID(SecondarySkill(SecondarySkill::LEADERSHIP)));
		EXPECT_EQ(prepared->duration, BonusDuration::PERMANENT);
		EXPECT_EQ(prepared->val, rank);
		EXPECT_FALSE(prepared->description.empty());
	}
}

TEST(PreparedNativeBonusRule, ParentStatusMatchesExactExistingSourceAndNotAnUnrelatedSanctified)
{
	const SpellID source(SpellID::MAGIC_ARROW);
	const auto bonus = nativeRules::sanctuaryKeeperMorale(source);
	ASSERT_TRUE(bonus.limiter);
	CBonusSystemNode node(BonusNodeType::HERO);
	BonusList accepted, undecided;
	const auto decision = [&]
	{
		return bonus.limiter->limit(BonusLimitationContext(bonus, node, accepted, undecided));
	};
	EXPECT_EQ(decision(), ILimiter::EDecision::DISCARD);
	auto parent = std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::SANCTIFIED,
		BonusSource::SPELL_EFFECT, 1, BonusSourceID(SpellID(SpellID::SLOW)));
	accepted.push_back(parent);
	EXPECT_EQ(decision(), ILimiter::EDecision::DISCARD);
	parent->sid = BonusSourceID(source);
	EXPECT_EQ(decision(), ILimiter::EDecision::ACCEPT);
	accepted.clear();
	undecided.push_back(parent);
	EXPECT_EQ(decision(), ILimiter::EDecision::NOT_SURE);
}

TEST(PreparedNativeBonusRule, SanctuaryTypedOutputMatchesTheExistingDescriptorExactly)
{
	const SpellID source(SpellID::MAGIC_ARROW);
	JsonNode descriptor;
	descriptor["type"].String() = "MORALE";
	descriptor["val"].Integer() = 2;
	descriptor["duration"].String() = "ONE_BATTLE";
	descriptor["sourceType"].String() = "SPELL_EFFECT";
	descriptor["sourceID"].String() = "core:magicArrow";
	JsonNode parent;
	parent["type"].String() = "HAS_ANOTHER_BONUS_LIMITER";
	parent["bonusType"].String() = "SANCTIFIED";
	parent["bonusSourceType"].String() = "SPELL_EFFECT";
	parent["bonusSourceID"].String() = "core:magicArrow";
	descriptor["limiters"].Vector().push_back(parent);
	const auto expected = nativeRules::bonusFromDescriptor(descriptor);
	EXPECT_EQ(wire(nativeRules::sanctuaryKeeperMorale(source)), wire(expected));
}

TEST(PreparedNativeBonusRule, CategoricalSuppressionPreservesBothLevelsDurationAndMetadata)
{
	for(const bool enhanced : {false, true})
	{
		const auto value = nativeRules::creatureSuppression(SpellID::FORGETFULNESS, "core:forgetfulness", 3, enhanced);
		EXPECT_EQ(value.type, BonusType::CREATURE_ABILITY_SUPPRESSION);
		EXPECT_EQ(value.val, enhanced ? 2 : 1);
		EXPECT_EQ(value.valType, BonusValueType::INDEPENDENT_MAX);
		EXPECT_EQ(value.duration, BonusDuration::N_TURNS);
		EXPECT_EQ(value.turnsRemain, 3);
		EXPECT_EQ(value.statusTags, std::vector<BonusStatusTag>{BonusStatusTag::DEBUFF});
		EXPECT_EQ(value.statusIdentity, "core:forgetfulness");
	}
	EXPECT_THROW(nativeRules::creatureSuppression(SpellID::FORGETFULNESS, "core:forgetfulness", 0, true), std::invalid_argument);
}

TEST(PreparedNativeBonusRule, BundlesPreserveOperationOrderAndRejectMixedControllers)
{
	const auto first = nativeRules::creatureSuppressionTemplate(false);
	const auto second = nativeRules::creatureSuppressionTemplate(true);
	const nativeRules::PreparedBonusBundle bundle({first, second});
	const auto values = bundle.bindSpell(SpellID::FORGETFULNESS, 3);
	ASSERT_EQ(values.size(), 2u);
	EXPECT_EQ(values[0].val, 1);
	EXPECT_EQ(values[1].val, 2);
	EXPECT_THROW(nativeRules::PreparedBonusBundle(std::vector<nativeRules::PreparedBonusRule>{}), std::invalid_argument);
	EXPECT_THROW(nativeRules::PreparedBonusBundle(std::vector<nativeRules::PreparedBonusRule>(9, first)), std::invalid_argument);
	EXPECT_THROW(nativeRules::PreparedBonusBundle({first, nativeRules::sanctuaryKeeperTemplate()}), std::invalid_argument);
	EXPECT_THROW(first.attach(), std::invalid_argument);
	EXPECT_THROW(nativeRules::PreparedBonusRule({}, rankBonus()).bindSpell(SpellID::MAGIC_ARROW), std::invalid_argument);
}

TEST(PreparedNativeBonusRule, FamilyFilteredPassiveKeepsItsExistingImmutableLimiterAndSource)
{
	Bonus prototype(BonusDuration::PERMANENT, BonusType::STACKS_SPEED, BonusSource::HERO_SPECIAL,
		5, BonusSourceID(HeroTypeID(0)));
	auto family = std::make_shared<CCreatureTypeLimiter>();
	family->setCreature(CreatureID(0));
	family->includeUpgrades = true;
	prototype.limiter = family;
	const auto prepared = nativeRules::prepareFamilySpecialtyBonus(prototype);
	EXPECT_EQ(wire(*prepared), wire(prototype));
	EXPECT_EQ(prepared->val, 5);
	EXPECT_EQ(prepared->limiter.get(), prototype.limiter.get());
	prototype.limiter.reset();
	EXPECT_THROW(nativeRules::prepareFamilySpecialtyBonus(prototype), std::invalid_argument);
}

TEST(PreparedNativeBonusRule, ExistingDirectMagicAuraRetainsOriginalArmyOrdinaryRecipientAndStrongestRules)
{
	Bonus expected(BonusDuration::PERMANENT, BonusType::SPELL_DAMAGE_REDUCTION,
		BonusSource::HERO_SPECIAL, 15, BonusSourceID(HeroTypeID(0)));
	expected.stacking = "native-aura-pilot";
	expected.subtype = BonusSubtypeID(SpellSchool::ANY);
	auto adjacent = std::make_shared<UnitAdjacentLimiter>();
	adjacent->targetUnit = CreatureID(0);
	adjacent->filter.includeUpgrades = true;
	adjacent->filter.relationship = AdjacentUnitFilter::Relationship::SAME_ARMY;
	auto all = std::make_shared<AllOfLimiter>();
	all->add(adjacent);
	all->add(std::make_shared<CreatureLevelLimiter>(1));
	expected.limiter = all;

	Bonus actual(BonusDuration::PERMANENT, BonusType::SPELL_DAMAGE_REDUCTION,
		BonusSource::HERO_SPECIAL, 15, BonusSourceID(HeroTypeID(0)));
	actual.stacking = "native-aura-pilot";
	nativeRules::configureAdjacentSpecialtyBonus(actual, CreatureID(0), nativeRules::AuraPolicy::DirectMagicProtection);
	EXPECT_EQ(wire(*nativeRules::prepareAdjacentSpecialtyBonus(actual)), wire(expected));
	EXPECT_THROW(nativeRules::configureAdjacentSpecialtyBonus(actual, CreatureID(0),
		static_cast<nativeRules::AuraPolicy>(255)), std::invalid_argument);
}

TEST(PreparedNativeBonusRule, CurrentDisciplineUsesItsActualPreparedMoraleRankTemplates)
{
	const int id = SecondarySkill::decode("new-horizons:discipline");
	ASSERT_GE(id, 0);
	const auto & skill = LIBRARY->skillh->objects.at(id);
	ASSERT_TRUE(skill);
	for(int rank = 1; rank <= 3; ++rank)
	{
		const auto & effects = skill->at(rank).effects;
		const auto found = std::find_if(effects.begin(), effects.end(), [](const auto & bonus)
		{ return bonus->type == BonusType::MORALE; });
		ASSERT_NE(found, effects.end());
		EXPECT_EQ((*found)->val, rank);
		EXPECT_EQ((*found)->source, BonusSource::SECONDARY_SKILL);
		EXPECT_EQ((*found)->sid, BonusSourceID(SecondarySkill(id)));
		EXPECT_EQ((*found)->duration, BonusDuration::PERMANENT);
	}
}
