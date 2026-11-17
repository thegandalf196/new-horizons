/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../mock/TinyMapGameTest.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/entities/hero/CompiledPerkCatalogue.h"
#include "../../lib/entities/hero/NewHorizonsPerkOperation.h"
#include "../../lib/nativeRules/CapturedPerkRules.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include <algorithm>
#include <set>
#include <stdexcept>
#include <type_traits>

namespace
{
using namespace newHorizonsHeroes;
constexpr auto SKILL = "new-horizons:offense";
JsonNode canonicalRules() { return JsonNode(JsonPath::builtin("config/newHorizonsPerks")); }
PerkState fresh()
{
	PerkState state;
	state.replaceRules(canonicalRules());
	return state;
}
PerkSelection acquire(PerkState & state, int tier)
{
	const auto offer = PerkSelectionOperation(state).eligibleCandidates(SKILL, tier);
	if(offer.empty()) throw std::runtime_error("Missing compiled-perk fixture tier");
	const auto choice = offer.front().selection;
	state.select(choice.skillId, choice.perkId, tier);
	return choice;
}
bool active(const PerkState & state, const PerkId & perk, int rank)
{
	const auto * definition = state.selectedDefinition(perk);
	return definition && definition->active && rank >= definition->requiredRank;
}
void expectProjection(const PerkState & state)
{
	const auto * catalog = state.compiledCatalogue();
	ASSERT_NE(catalog, nullptr);
	for(const auto & [skill, row] : state.getRules()["skills"].Struct())
		for(const auto & perk : row["perks"].Vector())
		{
			const auto id = catalog->find(skill, perk["id"].String());
			const bool expected = std::ranges::any_of(state.getSelected(), [&](const auto & selected)
				{ return selected.skillId == skill && selected.perkId == perk["id"].String(); });
			EXPECT_EQ(state.hasSelection(id), expected);
			EXPECT_EQ(state.hasSelection(skill, perk["id"].String()), expected);
		}
}
}

static_assert(std::is_same_v<decltype(std::declval<const PerkState &>().getRules()), const JsonNode &>);
static_assert(std::is_same_v<decltype(std::declval<PerkState &>().getSelected()), const std::vector<PerkSelection> &>);

TEST(CompiledPerkCatalogue, EveryDefinitionMatchesCapturedAdmissionAndHasUniqueDenseIndex)
{
	const auto rules = canonicalRules();
	const ValidatedPerkRules admitted(rules);
	const CompiledPerkCatalogue catalog(admitted);
	std::set<uint32_t> indices;
	size_t count = 0;
	for(const auto & [skill, row] : rules["skills"].Struct())
		for(const auto & perk : row["perks"].Vector())
		{
			SCOPED_TRACE(perk["id"].String());
			const auto id = catalog.find(skill, perk["id"].String());
			const auto * compiled = catalog.definition(id);
			ASSERT_NE(compiled, nullptr);
			const auto parsed = admitted.definition(skill, perk["id"].String());
			ASSERT_TRUE(parsed);
			EXPECT_EQ(compiled->requiredRank, perkRequiredRank(parsed->requiredRank));
			EXPECT_EQ(compiled->active, parsed->effect["status"].String() == "active");
			EXPECT_EQ(compiled->skill, catalog.skill(skill));
			EXPECT_EQ(SecondarySkill::encode(compiled->skill.getNum()), skill);
			EXPECT_TRUE(indices.insert(compiled->denseIndex).second);
			++count;
		}
	EXPECT_EQ(catalog.size(), count);
	ASSERT_FALSE(indices.empty());
	EXPECT_EQ(*indices.begin(), 0u);
	EXPECT_EQ(*indices.rbegin(), count - 1);
	EXPECT_FALSE(catalog.find("offense", "new-horizons:offense.executioner"));
	EXPECT_FALSE(catalog.find(SKILL, "executioner"));
	EXPECT_FALSE(catalog.skill("offense"));
	EXPECT_EQ(catalog.definition(PerkId{}), nullptr);
}

TEST(CompiledPerkCatalogue, ForeignAndExpiredCatalogueHandlesNeverAliasEqualDenseIndices)
{
	const auto rules = canonicalRules();
	const ValidatedPerkRules admitted(rules);
	PerkId expired;
	{
		const CompiledPerkCatalogue first(admitted);
		const CompiledPerkCatalogue second(admitted);
		expired = first.find(SKILL, "new-horizons:offense.executioner");
		ASSERT_TRUE(expired);
		EXPECT_EQ(second.definition(expired), nullptr);
	}
	const CompiledPerkCatalogue replacement(admitted);
	EXPECT_EQ(replacement.definition(expired), nullptr);
}

TEST(CompiledPerkCatalogue, EmptyHistoricalCapturesStayEmpty)
{
	const JsonNode rules;
	const ValidatedPerkRules admitted(rules);
	const CompiledPerkCatalogue catalog(admitted);
	EXPECT_EQ(catalog.size(), 0u);
	EXPECT_FALSE(catalog.find(SKILL, "new-horizons:offense.executioner"));
	PerkState state;
	EXPECT_FALSE(state.hasSelection(SKILL, "new-horizons:offense.executioner"));
	EXPECT_THROW(state.replaceSelected({{SKILL, "new-horizons:offense.executioner"}}), std::runtime_error);
	EXPECT_TRUE(state.getSelected().empty());
}

TEST(CompiledPerkCatalogue, SelectionCommitReplacementCopyAndRollbackKeepProjectionConsistent)
{
	auto state = fresh();
	expectProjection(state);
	const auto basic = acquire(state, 1);
	const auto basicId = state.compiledCatalogue()->find(basic.skillId, basic.perkId);
	expectProjection(state);
	auto snapshot = state;
	const auto advanced = acquire(state, 2);
	const auto advancedId = state.compiledCatalogue()->find(advanced.skillId, advanced.perkId);
	EXPECT_FALSE(snapshot.hasSelection(advancedId));
	EXPECT_TRUE(snapshot.hasSelection(basicId));
	expectProjection(snapshot);
	expectProjection(state);
	state = snapshot;
	EXPECT_FALSE(state.hasSelection(advancedId));
	EXPECT_TRUE(state.hasSelection(basicId));
	state.replaceSelected({});
	EXPECT_FALSE(state.hasSelection(basicId));
	expectProjection(state);
	state.replaceSelected(snapshot.getSelected());
	expectProjection(state);
}

TEST(CompiledPerkCatalogue, FailedRulesAndSelectionReplacementAreAtomic)
{
	auto state = fresh();
	const auto basic = acquire(state, 1);
	const auto id = state.compiledCatalogue()->find(basic.skillId, basic.perkId);
	const auto saved = state.toJson();
	auto badRules = state.getRules();
	badRules["skills"].Struct().rbegin()->second["perks"].Vector().back()["requires"].String() = "unknown";
	EXPECT_THROW(state.replaceRules(std::move(badRules)), std::runtime_error);
	EXPECT_EQ(state.toJson(), saved);
	EXPECT_TRUE(state.hasSelection(id));
	EXPECT_THROW(state.replaceSelected({basic, basic}), std::runtime_error);
	EXPECT_EQ(state.toJson(), saved);
	EXPECT_TRUE(state.hasSelection(id));
	EXPECT_THROW(state.replaceRules(JsonNode()), std::runtime_error);
	EXPECT_EQ(state.toJson(), saved);
	expectProjection(state);
}

TEST(CompiledPerkCatalogue, LiveRankLossRestorationAndExternalGrantDoNotChangeAcquisition)
{
	auto state = fresh();
	const auto basic = acquire(state, 1);
	const auto id = state.compiledCatalogue()->find(basic.skillId, basic.perkId);
	const auto selected = state.getSelected();
	EXPECT_TRUE(active(state, id, 1));
	EXPECT_FALSE(active(state, id, 0));
	EXPECT_TRUE(active(state, id, 3));
	EXPECT_EQ(state.getSelected(), selected);
	const auto advanced = PerkSelectionOperation(state).eligibleCandidates(SKILL, 3);
	ASSERT_FALSE(advanced.empty());
	const auto unacquired = state.compiledCatalogue()->find(SKILL, advanced.front().selection.perkId);
	EXPECT_FALSE(active(state, unacquired, 3));
	EXPECT_FALSE(state.hasSelection(unacquired));
}

TEST(CompiledPerkCatalogue, JsonBinaryLoadAndChangedInstalledInputRebuildIndependentProjections)
{
	auto state = fresh();
	acquire(state, 1);
	acquire(state, 2);
	acquire(state, 3);
	const auto saved = state.toJson();
	auto json = PerkState::fromJson(saved);
	CMemorySerializer memory;
	memory.oser & state;
	PerkState binary;
	memory.iser & binary;
	EXPECT_EQ(json.toJson(), saved);
	EXPECT_EQ(binary.toJson(), saved);
	expectProjection(json);
	expectProjection(binary);
	auto installed = canonicalRules();
	installed["skills"][SKILL]["perks"].Vector().front()["effect"]["status"].String() = "planned";
	PerkState different;
	different.replaceRules(std::move(installed));
	const auto selected = state.getSelected().front();
	const auto oldId = state.compiledCatalogue()->find(selected.skillId, selected.perkId);
	EXPECT_FALSE(different.hasSelection(oldId));
	EXPECT_EQ(binary.getRules(), state.getRules());
	EXPECT_EQ(json.getRules(), state.getRules());
}

TEST(CompiledPerkCatalogue, BuiltinCombatKeysResolveActualCapturedStatusAndTier)
{
	auto rules = canonicalRules();
	for(auto & perk : rules["skills"][SKILL]["perks"].Vector())
		if(perk["id"].String() == "new-horizons:offense.executioner")
			perk["effect"]["status"].String() = "planned";
	const ValidatedPerkRules admitted(rules);
	const CompiledPerkCatalogue catalog(admitted);
	const auto & executioner = catalog.find(BuiltinCombatPerk::Executioner);
	EXPECT_EQ(executioner, catalog.find(SKILL, "new-horizons:offense.executioner"));
	const auto * definition = catalog.definition(executioner);
	ASSERT_NE(definition, nullptr);
	EXPECT_FALSE(definition->active);
	EXPECT_EQ(definition->requiredRank, perkRequiredRank(
		admitted.definition(SKILL, "new-horizons:offense.executioner")->requiredRank));
	EXPECT_EQ(catalog.find(BuiltinCombatPerk::ArmorPiercer), catalog.find(SKILL, "new-horizons:offense.armorPiercer"));
	EXPECT_EQ(catalog.find(BuiltinCombatPerk::Breakthrough), catalog.find(SKILL, "new-horizons:offense.breakthrough"));
	EXPECT_EQ(catalog.find(BuiltinCombatPerk::ShockAssault), catalog.find(SKILL, "new-horizons:offense.shockAssault"));
	EXPECT_EQ(catalog.find(BuiltinCombatPerk::LuckyAim), catalog.find("new-horizons:luck", "new-horizons:luck.luckyAim"));
	EXPECT_EQ(catalog.find(BuiltinCombatPerk::ElvenPrecision), catalog.find("new-horizons:sylvanLuck", "new-horizons:sylvanLuck.elvenPrecision"));
	EXPECT_FALSE(catalog.find(BuiltinCombatPerk::Count));
	const JsonNode empty;
	const ValidatedPerkRules legacy(empty);
	const CompiledPerkCatalogue emptyCatalog(legacy);
	EXPECT_EQ(emptyCatalog.skill(SKILL), catalog.skill(SKILL));
	EXPECT_FALSE(emptyCatalog.find(BuiltinCombatPerk::Executioner));
}

namespace
{
class CompiledPerkHeroTest : public TinyMapGameTest
{
protected:
	Services * gameServices() override { return LIBRARY; }
};
}

TEST_F(CompiledPerkHeroTest, ActualHeroRankGrantRemovalRestorationAndWorldLoadKeepAcquisitionExact)
{
	TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
	builder.size(36).playerActive(PlayerColor(0)).hero({5, 5, 0}, HeroTypeID(0), PlayerColor(0)).heroExperience(0);
	startWithMap(std::move(builder));
	auto * hero = findHeroByOwner(PlayerColor(0));
	ASSERT_NE(hero, nullptr);
	ASSERT_EQ(hero->secSkills.size(), 2u);
	EXPECT_TRUE(hero->getPerkState().getSelected().empty());
	const auto skill = hero->secSkills.front().first;
	const auto identity = SecondarySkill::encode(skill.getNum());
	const auto pool = hero->getPerkState().candidatesForSkillTier(identity, 1);
	ASSERT_FALSE(pool.empty());
	const auto choice = pool.front().selection;
	const auto id = hero->getPerkId(choice.skillId, choice.perkId);
	ASSERT_TRUE(id);
	hero->setSecSkillLevel(skill, 3, ChangeValueMode::ABSOLUTE);
	EXPECT_TRUE(hero->getPerkState().getSelected().empty());
	EXPECT_FALSE(hero->hasActivePerk(id));
	hero->applyPerkSelection(choice);
	EXPECT_TRUE(hero->hasActivePerk(id));
	const auto acquired = hero->getPerkState().getSelected();
	hero->setSecSkillLevel(skill, 0, ChangeValueMode::ABSOLUTE);
	EXPECT_FALSE(hero->hasActivePerk(id));
	EXPECT_EQ(hero->getPerkState().getSelected(), acquired);
	hero->setSecSkillLevel(skill, 1, ChangeValueMode::ABSOLUTE);
	EXPECT_TRUE(hero->hasActivePerk(id));
	EXPECT_TRUE(hero->hasActivePerk(choice.skillId, choice.perkId));
	EXPECT_EQ(hero->getPerkState().getSelected(), acquired);
	CGameState restored;
	restored.preInit(LIBRARY);
	restored.loadFromMemory(gameState()->saveToMemory());
	const auto * loaded = restored.getHero(hero->id);
	ASSERT_NE(loaded, nullptr);
	EXPECT_EQ(loaded->getPerkState().getSelected(), acquired);
	EXPECT_TRUE(loaded->hasActivePerk(loaded->getPerkId(choice.skillId, choice.perkId)));
	EXPECT_FALSE(loaded->hasActivePerk(id));
	EXPECT_EQ(loaded->getPerkSkillRank(identity), 1);
}

TEST(CompiledPerkCatalogue, OutstandingOperationRetainsItsCaptureAndRejectsReplacement)
{
	auto state = fresh();
	PerkSelectionOperation operation(state);
	const auto choice = operation.candidates(SKILL, 1).front().selection;
	operation.select(choice.skillId, choice.perkId, 1);
	state.replaceRules(canonicalRules());
	// The old view remains valid, but its choices cannot mutate the replacement.
	EXPECT_TRUE(operation.definitions().definition(choice.skillId, choice.perkId));
	EXPECT_THROW(operation.commit(state), std::logic_error);
	EXPECT_THROW(operation.candidates(SKILL, 1), std::logic_error);
	EXPECT_TRUE(state.getSelected().empty());
	expectProjection(state);
}

TEST(CompiledPerkCatalogue, OutstandingOperationCannotOverwriteAnotherCommittedSelection)
{
	auto state = fresh();
	PerkSelectionOperation operation(state);
	const auto candidates = operation.candidates(SKILL, 1);
	ASSERT_GE(candidates.size(), 2u);
	operation.select(SKILL, candidates.front().selection.perkId, 1);
	const auto other = candidates.back().selection;
	state.select(other.skillId, other.perkId, 1);
	EXPECT_THROW(operation.commit(state), std::logic_error);
	EXPECT_EQ(state.getSelected(), (std::vector<PerkSelection>{other}));
	expectProjection(state);
}
