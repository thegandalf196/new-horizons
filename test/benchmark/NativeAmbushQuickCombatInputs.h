/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#pragma once
#include "NativeEncounterBenchmarkInputs.h"
#include <string>
#include <tuple>
#include <vector>

namespace nativeBenchmark
{
/// Quick-combat shaped Sylvan Tactics workloads: Jenova's Rampart army from the
/// real start columns, with obstacles, against split neutral stacks or an AI hero.
struct AmbushOpponent
{
	using ArmyFactory = Army (*)();
	const char * name;
	ArmyFactory army;
	/// Null: a neutral map monster split into the listed stacks.
	const char * hero;
};
/// One Sylvan Tactics build; the policy 4 list names the same tier slots of the
/// historical roster (Quick Redeployment, Kindred Flame, Ancient Accord).
struct SylvanBuild
{
	const char * name;
	int rank;
	std::vector<const char *> perks5;
	std::vector<const char *> perks4;
};
inline Army splitStacks(const char * creature, uint16_t count, size_t stacks)
{
	return Army(stacks, {troop(creature), count});
}
inline std::vector<AmbushOpponent> ambushOpponents()
{
	return {
		{"NeutralMelee", +[] { return splitStacks("core:crusader", 8, 5); }, nullptr},
		{"NeutralShooters", +[] { return splitStacks("core:marksman", 10, 5); }, nullptr},
		{"NeutralFliers", +[] { return splitStacks("core:royalGriffin", 14, 5); }, nullptr},
		{"NeutralBigStack", +[] { return splitStacks("core:ancientBehemoth", 6, 1); }, nullptr},
		{"HeroCastle", mirrorCastleArmy, "core:rion"},
		{"HeroNecropolis", mirrorNecropolisArmy, "core:galthran"},
	};
}
inline std::vector<SylvanBuild> sylvanBuilds()
{
	return {
		{"Basic", 1, {}, {}},
		{"AdvancedHunterCover", 2, {"serendipity", "forestCover"}, {"serendipity", "natureSProvidence"}},
		{"ExpertHunterStartleApex", 3, {"serendipity", "startle", "apexPredator"},
			{"serendipity", "wildChance", "cascadingFortune"}},
		{"ExpertPackCoverApex", 3, {"elvenPrecision", "forestCover", "apexPredator"},
			{"elvenPrecision", "natureSProvidence", "cascadingFortune"}},
	};
}
/// Opponent index, build index, Sylvan Tactics policy (4 or 5).
using AmbushQuickCombatCase = std::tuple<size_t, size_t, int>;
inline std::vector<AmbushQuickCombatCase> ambushQuickCombatCases()
{
	std::vector<AmbushQuickCombatCase> result;
	for(size_t opponent = 0; opponent < 6; ++opponent)
		for(size_t build = 0; build < 4; ++build)
			for(const int policy : {4, 5})
				result.emplace_back(opponent, build, policy);
	return result;
}
}
