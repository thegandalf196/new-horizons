/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../../lib/nativeRules/CapturedRuleExecutionAudit.h"
#include "../../lib/nativeRules/CapturedRuleRef.h"
#include "../../lib/filesystem/ResourcePath.h"
#include <stdexcept>
#include <thread>
using namespace nativeRules;
using namespace nativeRules::executionAudit;
TEST(CompiledRuleExecutionAudit, GrowthMasteryLeadershipProbesDoNotValidatePoliciesOrResolveIdentifiers)
{
	if(!available()) GTEST_SKIP() << "Requires opt-in NH_PERF_EXPERIMENTS work counters";
	const JsonNode development(JsonPath::builtin("config/newHorizonsHeroes"));
	const JsonNode capability(JsonPath::builtin("config/newHorizonsCapabilities"));
	const JsonNode mastery(JsonPath::builtin("config/newHorizonsMasteries"));
	const HeroClassID knight(HeroClassID::decode("core:knight"));
	const auto rawDevelopment = newHorizonsHeroes::resolveHeroRules(development, knight);
	const auto rawCapability = newHorizonsHeroes::resolveCapabilityRules(capability, knight);
	const SecondarySkill offense(SecondarySkill::decode("new-horizons:offense"));
	CapturedRulePool pool;
	Counters actual;
	Scope scope(actual);
	const auto developmentId = pool.intern(CapturedRuleKind::ResolvedDevelopment, rawDevelopment);
	const auto masteryId = pool.intern(CapturedRuleKind::Mastery, mastery);
	const auto capabilityId = pool.intern(CapturedRuleKind::ResolvedCapability, rawCapability);
	EXPECT_GT(actual.count(Event::DevelopmentAdmission), 0u);
	EXPECT_GT(actual.count(Event::MasteryAdmission), 0u);
	EXPECT_GT(actual.count(Event::CapabilityAdmission), 0u);
	EXPECT_GT(actual.count(Event::IdentifierResolution), 0u);
	const auto afterAdmission = actual;
	const auto d = pool.document(developmentId, CapturedRuleKind::ResolvedDevelopment)->development();
	const auto m = pool.document(masteryId, CapturedRuleKind::Mastery)->mastery();
	const auto c = pool.document(capabilityId, CapturedRuleKind::ResolvedCapability)->capabilities();
	for(int probe = 0; probe < 100; ++probe)
	{
		EXPECT_TRUE(d->profile());
		(void)d->profile()->baseAtLevel(1 + probe);
		(void)d->skillGrowthChances([probe](SecondarySkill) { return probe % 4; });
		(void)d->skillOfferWeight(offense);
		(void)d->isExcludedSkill(offense);
		(void)d->factionSkill(FactionID::CASTLE);
		(void)m->options(SecondarySkill::ARTILLERY);
		(void)m->options(SecondarySkill::LOGISTICS);
		(void)c->leadership()->rating(1 + probe);
		(void)c->leadership()->requirement(CreatureID::SKELETON);
		(void)c->leadership()->authorityRankBonus(probe % 4);
		(void)c->leadership()->authorityPerkBonus("massedRanks");
	}
	EXPECT_EQ(actual, afterAdmission);
	EXPECT_EQ(pool.intern(CapturedRuleKind::ResolvedDevelopment, rawDevelopment), developmentId);
	EXPECT_EQ(pool.intern(CapturedRuleKind::Mastery, mastery), masteryId);
	EXPECT_EQ(pool.intern(CapturedRuleKind::ResolvedCapability, rawCapability), capabilityId);
	EXPECT_EQ(actual, afterAdmission);
	auto changed = rawDevelopment;
	changed["powerDivisor"].Integer() += 1;
	EXPECT_NE(pool.intern(CapturedRuleKind::ResolvedDevelopment, changed), developmentId);
	EXPECT_GT(actual.count(Event::DevelopmentAdmission), afterAdmission.count(Event::DevelopmentAdmission));
}
TEST(CompiledRuleExecutionAudit, NestedAndExceptionalScopesRestoreTheCurrentThreadObserver)
{
	if(!available()) GTEST_SKIP() << "Requires opt-in NH_PERF_EXPERIMENTS work counters";
	Counters outer, inner;
	{
		Scope scope(outer);
		record(Event::IdentifierResolution);
		std::thread unrelated([] { record(Event::IdentifierResolution); });
		unrelated.join();
		try
		{
			Scope nested(inner);
			record(Event::MasteryAdmission);
			throw std::runtime_error("scope unwind");
		}
		catch(const std::runtime_error &) {}
		record(Event::IdentifierResolution);
	}
	const auto before = outer;
	record(Event::IdentifierResolution);
	EXPECT_EQ(outer, before);
	EXPECT_EQ(outer.count(Event::IdentifierResolution), 2u);
	EXPECT_EQ(outer.count(Event::MasteryAdmission), 0u);
	EXPECT_EQ(inner.count(Event::MasteryAdmission), 1u);
}
