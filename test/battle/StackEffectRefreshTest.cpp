/* Part of VCMI. Authors: see AUTHORS. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../../lib/battle/StackEffectRefresh.h"
#include "../../lib/bonuses/BonusList.h"
namespace
{
Bonus value()
{
	return Bonus(BonusDuration::N_TURNS, BonusType::STACKS_SPEED, BonusSource::SPELL_EFFECT,
		5, BonusSourceID(SpellID(SpellID::HASTE)));
}
TEST(StackEffectRefreshTest, LocalIdentityIgnoresStrengthAndTimerButKeepsEveryOriginalKey)
{
	const auto incoming = value(); Bonus candidate = incoming;
	candidate.val = 17; candidate.turnsRemain = 8;
	EXPECT_TRUE(stackEffectRefresh::matchesIdentity(incoming, &candidate));
	EXPECT_FALSE(stackEffectRefresh::matchesIdentity(incoming, nullptr));
	for(int key = 0; key < 6; ++key)
	{
		candidate = incoming;
		if(key == 0) candidate.source = BonusSource::OTHER;
		if(key == 1) candidate.sid = BonusSourceID(SpellID(SpellID::SHIELD));
		if(key == 2) candidate.type = BonusType::PRIMARY_SKILL;
		if(key == 3) candidate.subtype = BonusSubtypeID(PrimarySkill::ATTACK);
		if(key == 4) candidate.valType = BonusValueType::BASE_NUMBER;
		if(key == 5) candidate.statusIdentity = "other";
		EXPECT_FALSE(stackEffectRefresh::matchesIdentity(incoming, &candidate));
	}
	Bonus defense = incoming;
	defense.type = BonusType::PRIMARY_SKILL;
	defense.subtype = BonusSubtypeID(PrimarySkill::DEFENSE);
	EXPECT_FALSE(stackEffectRefresh::matchesIdentity(incoming, &defense));
}
TEST(StackEffectRefreshTest, LegacyCandidateRequiresSpellSourceEvenWhenIncomingIsOther)
{
	auto incoming = value(); incoming.source = BonusSource::OTHER;
	Bonus candidate = incoming;
	const auto selector = stackEffectRefresh::legacyCandidateSelector(incoming);
	EXPECT_FALSE(selector(&candidate));
	candidate.source = BonusSource::SPELL_EFFECT; candidate.val = 99;
	EXPECT_TRUE(selector(&candidate));
	candidate.statusIdentity = "different"; EXPECT_FALSE(selector(&candidate));
	candidate.statusIdentity.clear(); candidate.sid = BonusSourceID(SpellID(SpellID::SHIELD));
	EXPECT_FALSE(selector(&candidate));
}
TEST(StackEffectRefreshTest, OrdinaryAndIdentifiedPlansKeepLegacyAndSourceLocalScopesSeparate)
{
	const auto incoming = value(); BonusList empty;
	for(const bool forceAdd : {false, true})
		for(const bool hasLegacy : {false, true})
		{
			const auto ordinary = stackEffectRefresh::plan(forceAdd, incoming, empty, hasLegacy);
			EXPECT_FALSE(ordinary.identified); EXPECT_EQ(ordinary.append, forceAdd || !hasLegacy);
			auto tagged = incoming; tagged.statusIdentity = "named";
			const auto absent = stackEffectRefresh::plan(forceAdd, tagged, empty, hasLegacy);
			EXPECT_TRUE(absent.identified); EXPECT_TRUE(absent.append);
			BonusList local; local.push_back(std::make_shared<Bonus>(tagged));
			const auto present = stackEffectRefresh::plan(forceAdd, tagged, local, hasLegacy);
			EXPECT_TRUE(present.identified); EXPECT_EQ(present.append, forceAdd);
		}
}
TEST(StackEffectRefreshTest, RefreshExtendsOnlyTimerAndAppendsUniqueTagsInInputOrder)
{
	auto target = value(); target.turnsRemain = 2; target.statusTags = {BonusStatusTag::DEBUFF};
	auto incoming = target; incoming.turnsRemain = 5; incoming.val = 19;
	incoming.statusTags = {BonusStatusTag::DEBUFF, BonusStatusTag::NON_TRANSFERABLE};
	stackEffectRefresh::refreshFields(target, incoming);
	EXPECT_EQ(target.turnsRemain, 5); EXPECT_EQ(target.val, 5);
	ASSERT_EQ(target.statusTags.size(), 2u);
	EXPECT_EQ(target.statusTags[0], BonusStatusTag::DEBUFF); EXPECT_EQ(target.statusTags[1], BonusStatusTag::NON_TRANSFERABLE);
	incoming.turnsRemain = 3; stackEffectRefresh::refreshFields(target, incoming);
	EXPECT_EQ(target.turnsRemain, 5); EXPECT_EQ(target.statusTags.size(), 2u);
	EXPECT_EQ(target.type, incoming.type); EXPECT_EQ(target.source, incoming.source); EXPECT_EQ(target.sid, incoming.sid);
}
}
