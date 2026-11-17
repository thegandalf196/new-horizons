/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../../lib/entities/hero/NewHorizonsPerkOperation.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/modding/IdentifierStorage.h"
#include "../../lib/modding/ModScope.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <iostream>
#include <stdexcept>

namespace
{
using namespace newHorizonsHeroes;
struct Query
{
	std::string skill;
	std::string perk;
	PerkId id;
};
struct Fixture
{
	PerkState state;
	std::vector<std::pair<SecondarySkill, int>> ranks;
	std::vector<Query> queries;
};
int rank(const Fixture & fixture, SecondarySkill skill)
{
	for(const auto & [id, level] : fixture.ranks)
		if(id == skill) return level;
	return 0;
}
// Frozen pre-index algorithm: selected scan, captured row scan, canonical
// registry resolution, live bounded rank scan. No added validation or parsing.
bool reference(const Fixture & fixture, const Query & query)
{
	if(!std::ranges::any_of(fixture.state.getSelected(), [&](const auto & selected)
		{ return selected.skillId == query.skill && selected.perkId == query.perk; }))
		return false;
	for(const auto & perk : fixture.state.getRules()["skills"][query.skill]["perks"].Vector())
	{
		if(perk["id"].String() != query.perk) continue;
		if(perk["effect"]["status"].String() != "active") return false;
		const auto decoded = LIBRARY->identifiers()->getIdentifier(
			ModScope::scopeGame(), SecondarySkill::entityType(), query.skill, true);
		const auto currentRank = decoded && SecondarySkill::encode(*decoded) == query.skill
			? rank(fixture, SecondarySkill(*decoded)) : 0;
		return currentRank >= perkRequiredRank(perk["requires"].String());
	}
	return false;
}
bool compiled(const Fixture & fixture, const PerkId & id)
{
	const auto * definition = fixture.state.selectedDefinition(id);
	return definition && definition->active && rank(fixture, definition->skill) >= definition->requiredRank;
}
Fixture prepare(size_t acquiredSkills)
{
	Fixture fixture;
	fixture.state.replaceRules(JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
	const auto * catalog = fixture.state.compiledCatalogue();
	size_t learned = 0;
	for(const auto & [skill, row] : fixture.state.getRules()["skills"].Struct())
	{
		const auto resolved = catalog->skill(skill);
		if(!resolved) throw std::runtime_error("Benchmark requires canonical Skill identities");
		const auto skillIndex = learned++;
		// Current heroes have at most eight learned Skills; fresh profiles have
		// two Basic Skills without perks. Other catalogue entries remain absent.
		if(skillIndex < std::max(size_t{2}, acquiredSkills))
			fixture.ranks.emplace_back(*resolved, skillIndex < acquiredSkills ? 3 : 1);
		if(skillIndex < acquiredSkills)
			for(int tier = 1; tier <= 3; ++tier)
			{
				const auto candidates = PerkSelectionOperation(fixture.state).eligibleCandidates(skill, 3);
				if(candidates.empty()) throw std::runtime_error("Missing benchmark perk tier");
				const auto & selected = candidates.front().selection;
				fixture.state.select(selected.skillId, selected.perkId, 3);
			}
		for(const auto & perk : row["perks"].Vector())
			fixture.queries.push_back({skill, perk["id"].String(), catalog->find(skill, perk["id"].String())});
	}
	return fixture;
}
uint64_t query(const Fixture & fixture, size_t variant, size_t repetitions)
{
	uint64_t digest = 14695981039346656037ULL;
	for(size_t iteration = 0; iteration < repetitions; ++iteration)
		for(const auto & item : fixture.queries)
		{
			const bool enabled = variant == 0 ? reference(fixture, item)
				: variant == 1 ? compiled(fixture, fixture.state.compiledCatalogue()->find(item.skill, item.perk))
				: compiled(fixture, item.id);
			digest = (digest ^ static_cast<uint64_t>(enabled)) * 1099511628211ULL;
		}
	return digest;
}
}

// Opt-in experiment: no wall-clock gate, no initialization in the timed region.
// Rotate variants per sample and retain both exact digest and raw elapsed time.
TEST(CompiledPerkLookupBenchmark, DISABLED_MatchedCapturedQueries)
{
	constexpr std::array<const char *, 3> names{"reference", "compiled_string", "compiled_id"};
	constexpr size_t repetitions = 1024;
	for(const size_t acquiredSkills : {size_t{0}, size_t{1}, size_t{8}})
	{
		const auto fixture = prepare(acquiredSkills);
		for(const auto & item : fixture.queries)
		{
			ASSERT_EQ(reference(fixture, item), compiled(fixture, item.id));
			ASSERT_EQ(reference(fixture, item), compiled(fixture,
				fixture.state.compiledCatalogue()->find(item.skill, item.perk)));
		}
		const auto expected = query(fixture, 0, repetitions);
		for(size_t variant = 0; variant < names.size(); ++variant) (void)query(fixture, variant, 32);
		for(size_t sample = 0; sample < 8; ++sample)
			for(size_t order = 0; order < names.size(); ++order)
			{
				const size_t variant = (order + sample) % names.size();
				const auto begin = std::chrono::steady_clock::now();
				const auto digest = query(fixture, variant, repetitions);
				const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
					std::chrono::steady_clock::now() - begin).count();
				ASSERT_EQ(digest, expected);
				std::cout << "NH_PERF_MICRO {\"schema\":\"nh-perf-v1\",\"variant\":\"perk_" << names[variant]
					<< "\",\"acquired_skills\":" << acquiredSkills << ",\"sample\":" << sample
					<< ",\"count\":" << repetitions * fixture.queries.size() << ",\"elapsed_ns\":" << elapsed
					<< ",\"digest\":" << digest << ",\"correct\":true}\n";
			}
	}
}
