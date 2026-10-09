/*
 * TargetConditionTransferTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"

#include "../../lib/spells/TargetCondition.h"
#include "../../lib/spells/CSpell.h"
#include "../../lib/spells/CSpellHandler.h"
#include "../../lib/serializer/JsonDeserializer.h"
#include "../../lib/bonuses/BonusParameters.h"
#include "mock/mock_spells_Spell.h"
#include "mock/mock_spells_Mechanics.h"
#include "mock/mock_battle_Unit.h"
#include "mock/mock_BonusBearer.h"

namespace test
{
using namespace spells;
using namespace testing;

class TargetConditionTransferTest : public Test
{
protected:
	TargetCondition subject;
	NiceMock<SpellMock> source;
	NiceMock<UnitMock> recipient;
	BonusBearerMock bonuses;
	const TargetConditionItemFactory * factory = TargetConditionItemFactory::getDefault();
	SpellID sourceID = SpellID(SpellID::SLOW);

	void SetUp() override
	{
		ON_CALL(source, getId()).WillByDefault(Invoke([&]() { return sourceID; }));
		ON_CALL(source, isMagical()).WillByDefault(Return(true));
		ON_CALL(source, isPositive()).WillByDefault(Return(false));
		ON_CALL(source, forEachSchool(_)).WillByDefault(Invoke([](const Spell::SchoolCallback & callback)
		{
			bool stop = false;
			callback(SpellSchool::AIR, stop);
		}));
		ON_CALL(recipient, getAllBonuses(_, _)).WillByDefault(Invoke(&bonuses, &BonusBearerMock::getAllBonuses));
		ON_CALL(recipient, getTreeVersion()).WillByDefault(Invoke(&bonuses, &BonusBearerMock::getTreeVersion));
		ON_CALL(recipient, magicResistance()).WillByDefault(Return(0));
		load(JsonNode());
	}

	void load(const JsonNode & configuration)
	{
		JsonDeserializer reader(nullptr, configuration);
		subject.serializeJson(reader, factory);
	}

	RecipientConditionContext context(std::optional<bool> opposing = false,
		std::optional<int64_t> ceiling = {}, bool v3 = true, SpellID family = SpellID(SpellID::SLOW)) const
	{
		return {&source, family, 2, v3, opposing, ceiling};
	}

	void add(BonusType type, BonusSubtypeID subtype = BonusSubtypeID(), bool absolute = false)
	{
		auto bonus = std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, type, BonusSource::OTHER, 4, BonusSourceID(), subtype);
		if(absolute)
			bonus->parameters = std::make_shared<BonusParameters>(1);
		bonuses.addNewBonus(bonus);
	}

	void forbid(BonusType type, const std::string & name)
	{
		auto item = factory->createConfigurable("game", "bonus", name);
		ASSERT_NE(item, nullptr);
		item->setExclusive(true);
		item->setInverted(true);
		subject.normal.push_back(item);
		add(type);
	}
};

TEST_F(TargetConditionTransferTest, BasicTraitInversionAndOrdinaryRegression)
{
	forbid(BonusType::NON_LIVING, "NON_LIVING");
	EXPECT_EQ(subject.checkRecipient(context(), &recipient), RecipientConditionResult::ILLEGAL);
	StrictMock<MechanicsMock> ordinary;
	EXPECT_CALL(ordinary, getSpellId()).WillRepeatedly(Return(sourceID));
	EXPECT_CALL(ordinary, getSpellLevel()).WillRepeatedly(Return(2));
	EXPECT_CALL(ordinary, isMagicalEffect()).WillRepeatedly(Return(true));
	EXPECT_CALL(ordinary, isPositiveSpell()).WillRepeatedly(Return(false));
	EXPECT_CALL(ordinary, getSpell()).WillRepeatedly(Return(&source));
	EXPECT_CALL(ordinary, usesNewHorizonsMagicV3()).WillRepeatedly(Return(true));
	EXPECT_CALL(ordinary, battle()).WillRepeatedly(Return(nullptr));
	EXPECT_FALSE(subject.isReceptive(&ordinary, &recipient));
}

TEST_F(TargetConditionTransferTest, ResistanceGateAndNegationPrecedence)
{
	ON_CALL(recipient, magicResistance()).WillByDefault(Return(100));
	EXPECT_EQ(subject.checkRecipient(context(), &recipient), RecipientConditionResult::ILLEGAL);
	add(BonusType::NEGATE_ALL_NATURAL_IMMUNITIES, BonusCustomSubtype::immunityEnemyHero);
	EXPECT_EQ(subject.checkRecipient(context(), &recipient), RecipientConditionResult::LEGAL);
}

TEST_F(TargetConditionTransferTest, AbsoluteFamilyImmunityPrecedesNegation)
{
	add(BonusType::SPELL_IMMUNITY, BonusSubtypeID(SpellID(SpellID::HASTE)), true);
	add(BonusType::NEGATE_ALL_NATURAL_IMMUNITIES, BonusCustomSubtype::immunityEnemyHero);
	EXPECT_EQ(subject.checkRecipient(context(false, {}, true, SpellID(SpellID::HASTE)), &recipient), RecipientConditionResult::ILLEGAL);
	EXPECT_EQ(subject.checkRecipient(context(), &recipient), RecipientConditionResult::LEGAL);
}

TEST_F(TargetConditionTransferTest, NormalFamilyImmunityAndReceptivePositiveException)
{
	add(BonusType::SPELL_IMMUNITY, BonusSubtypeID(SpellID(SpellID::HASTE)));
	EXPECT_EQ(subject.checkRecipient(context(false, {}, true, SpellID(SpellID::HASTE)), &recipient), RecipientConditionResult::ILLEGAL);
	add(BonusType::RECEPTIVE);
	ON_CALL(source, isPositive()).WillByDefault(Return(true));
	EXPECT_EQ(subject.checkRecipient(context(false, {}, true, SpellID(SpellID::HASTE)), &recipient), RecipientConditionResult::LEGAL);
}

TEST_F(TargetConditionTransferTest, AbsoluteLevelImmunityUsesSavedLevel)
{
	add(BonusType::LEVEL_SPELL_IMMUNITY, BonusSubtypeID(), true);
	add(BonusType::NEGATE_ALL_NATURAL_IMMUNITIES, BonusCustomSubtype::immunityEnemyHero);
	EXPECT_EQ(subject.checkRecipient(context(), &recipient), RecipientConditionResult::ILLEGAL);
	const RecipientConditionContext higher{&source, sourceID, 5, true, false, {}};
	EXPECT_EQ(subject.checkRecipient(higher, &recipient), RecipientConditionResult::LEGAL);
}

TEST_F(TargetConditionTransferTest, CapturedHealthCeilingIsExactWithoutRecalculation)
{
	auto health = factory->createConfigurable("", "healthValueSpecial", "");
	ASSERT_NE(health, nullptr);
	health->setExclusive(true);
	subject.normal.push_back(health);
	EXPECT_CALL(recipient, getAvailableHealth()).WillRepeatedly(Return(101));
	EXPECT_EQ(subject.checkRecipient(context(false, 101), &recipient), RecipientConditionResult::LEGAL);
	EXPECT_EQ(subject.checkRecipient(context(false, 100), &recipient), RecipientConditionResult::ILLEGAL);
}

TEST_F(TargetConditionTransferTest, MissingHealthCaptureCannotPassInvertedCondition)
{
	auto health = factory->createConfigurable("", "healthValueSpecial", "");
	ASSERT_NE(health, nullptr);
	health->setExclusive(true);
	health->setInverted(true);
	subject.normal.push_back(health);
	EXPECT_CALL(recipient, getAvailableHealth()).Times(0);
	EXPECT_EQ(subject.checkRecipient(context(), &recipient), RecipientConditionResult::MISSING_HEALTH_CAPTURE);
}

TEST_F(TargetConditionTransferTest, KnownNegationBypassesNormalCaptureWithoutRecalculation)
{
	auto health = factory->createConfigurable("", "healthValueSpecial", "");
	ASSERT_NE(health, nullptr);
	health->setExclusive(true);
	subject.normal.push_back(health);
	add(BonusType::NEGATE_ALL_NATURAL_IMMUNITIES, BonusCustomSubtype::immunityEnemyHero);
	EXPECT_CALL(recipient, getAvailableHealth()).Times(0);
	EXPECT_EQ(subject.checkRecipient(context(), &recipient), RecipientConditionResult::LEGAL);
}

TEST_F(TargetConditionTransferTest, OriginalCasterProvenanceIsRequiredOnlyForBattleWideNegation)
{
	EXPECT_EQ(subject.checkRecipient(context({}), &recipient), RecipientConditionResult::LEGAL);
	add(BonusType::SPELL_IMMUNITY, BonusSubtypeID(sourceID));
	add(BonusType::NEGATE_ALL_NATURAL_IMMUNITIES, BonusCustomSubtype::immunityBattleWide);
	EXPECT_EQ(subject.checkRecipient(context({}), &recipient), RecipientConditionResult::MISSING_CASTER_PROVENANCE);
	EXPECT_EQ(subject.checkRecipient(context(false), &recipient), RecipientConditionResult::ILLEGAL);
	EXPECT_EQ(subject.checkRecipient(context(true), &recipient), RecipientConditionResult::LEGAL);
	add(BonusType::RECEPTIVE);
	ON_CALL(source, isPositive()).WillByDefault(Return(true));
	subject.negation = {factory->createImmunityNegation(), factory->createReceptiveFeature()};
	EXPECT_EQ(subject.checkRecipient(context({}), &recipient), RecipientConditionResult::LEGAL);
}

TEST_F(TargetConditionTransferTest, ForgetfulnessV3UsesSavedRuleAndSourceNotFamily)
{
	auto shooter = factory->createConfigurable("game", "bonus", "SHOOTER");
	ASSERT_NE(shooter, nullptr);
	shooter->setExclusive(true);
	subject.normal.push_back(shooter);
	sourceID = SpellID(SpellID::FORGETFULNESS);
	EXPECT_EQ(subject.checkRecipient(context(false, {}, true, sourceID), &recipient), RecipientConditionResult::LEGAL);
	EXPECT_EQ(subject.checkRecipient(context(false, {}, false, sourceID), &recipient), RecipientConditionResult::ILLEGAL);
	sourceID = SpellID(SpellID::SLOW);
	EXPECT_EQ(subject.checkRecipient(context(false, {}, true, SpellID(SpellID::FORGETFULNESS)), &recipient), RecipientConditionResult::ILLEGAL);
}

TEST_F(TargetConditionTransferTest, SchoolImmunityAndAlternativeRequirements)
{
	auto creature = factory->createConfigurable("game", "healthValueSpecial", "");
	ASSERT_NE(creature, nullptr);
	// anyOf remains OR, even when one supported requirement does not match.
	ON_CALL(recipient, getAvailableHealth()).WillByDefault(Return(10));
	subject.normal.push_back(creature);
	subject.normal.push_back(factory->createResistance());
	EXPECT_EQ(subject.checkRecipient(context(false, 0), &recipient), RecipientConditionResult::LEGAL);
	add(BonusType::SPELL_SCHOOL_IMMUNITY, BonusSubtypeID(SpellSchool::AIR));
	EXPECT_EQ(subject.checkRecipient(context(false, 0), &recipient), RecipientConditionResult::ILLEGAL);
}

class UnsupportedTransferItem : public TargetConditionItem
{
public:
	bool isReceptive(const Mechanics *, const battle::Unit *) const override { return true; }
	void setInverted(bool) override {}
	void setExclusive(bool) override {}
	bool isExclusive() const override { return true; }
	bool isForgetfulnessShooterRequirement() const override { return true; }
};

TEST_F(TargetConditionTransferTest, UnknownCustomConditionCannotSilentlyPassOrBeNegated)
{
	subject.normal.push_back(std::make_shared<UnsupportedTransferItem>());
	add(BonusType::NEGATE_ALL_NATURAL_IMMUNITIES, BonusCustomSubtype::immunityEnemyHero);
	EXPECT_EQ(subject.checkRecipient(context(), &recipient), RecipientConditionResult::UNSUPPORTED_CONDITION);
	sourceID = SpellID(SpellID::FORGETFULNESS);
	EXPECT_EQ(subject.checkRecipient(context(false, {}, true, sourceID), &recipient), RecipientConditionResult::UNSUPPORTED_CONDITION);
	JsonNode config;
	config["noneOf"]["custom.missing"].String() = "normal";
	load(config);
	EXPECT_EQ(subject.checkRecipient(context(), &recipient), RecipientConditionResult::UNSUPPORTED_CONDITION);
}

TEST_F(TargetConditionTransferTest, CachedFactoryAndSpellForwardingRejectWrongSource)
{
	CSpellHandler handler;
	auto ownedSource = std::make_shared<CSpell>();
	handler.objects.push_back(ownedSource);
	CSpell & realSource = *ownedSource;
	realSource.id = sourceID;
	realSource.targetCondition["noneOf"]["bonus.UNDEAD"].String() = "normal";
	realSource.targetCondition.setModScope("game");
	handler.afterLoadFinalization();
	const RecipientConditionContext captured{&realSource, sourceID, 2, true, false, {}};
	EXPECT_EQ(realSource.checkRecipient(captured, &recipient), RecipientConditionResult::LEGAL);
	add(BonusType::UNDEAD);
	realSource.targetCondition = JsonNode(); // The factory must use its already-parsed restriction.
	EXPECT_EQ(realSource.checkRecipient(captured, &recipient), RecipientConditionResult::ILLEGAL);
	EXPECT_EQ(realSource.checkRecipient(context(), &recipient), RecipientConditionResult::INVALID_CONTEXT);
	const RecipientConditionContext invalid{nullptr, sourceID, 2, true, false, {}};
	EXPECT_EQ(subject.checkRecipient(invalid, &recipient), RecipientConditionResult::INVALID_CONTEXT);
}

}
