/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "../../Global.h"
#include "../../AI/BattleAI/MovementQueryCache.h"
#include "../../test/battle/MovementLegacyReference.h"
#include "MovementBenchmarkReference.h"
#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace
{
constexpr std::size_t actorCount = 14;
constexpr std::size_t candidateCount = 8;
constexpr std::size_t queriesPerDecision = actorCount * candidateCount;
constexpr std::uint64_t initialDigest = 1469598103934665603ull;
using Costs = std::array<std::int32_t, GameConstants::BFIELD_SIZE>;

struct Input
{
	AccessibilityInfo accessibility;
	ReachabilityInfo::Parameters params;
	BattleHexArray stoppers;
	EGateState gateState = EGateState::CLOSED;
	Costs movementCosts{};
};

struct Workload
{
	std::string name;
	std::string description;
	std::vector<Input> inputs;
	bool unique;
	bool bypass;
};

struct Sample
{
	double microseconds = 0;
	std::uint64_t digest = initialDigest;
	std::size_t queries = 0;
	std::size_t hits = 0;
	std::size_t solves = 0;
	std::size_t evictions = 0;
	std::size_t peakEntries = 0;
};

void mix(std::uint64_t & digest, std::uint64_t value)
{
	digest ^= value;
	digest *= 1099511628211ull;
}

battle::MovementQuery compile(const Input & input)
{
	return input.params.flying
		? battle::compileFlyingMovementQuery(input.accessibility, input.params)
		: battle::compileWalkingMovementQuery(input.accessibility, input.params,
			input.stoppers, input.gateState, input.movementCosts);
}

battle::MovementPaths legacy(const Input & input)
{
	return movementLegacy::solve(input.accessibility, input.params, input.stoppers,
		input.gateState, input.movementCosts);
}

battle::MovementPaths direct(const Input & input)
{
	return input.params.flying
		? battle::solveFlyingMovementDirect(input.accessibility, input.params)
		: battle::solveMovementDirect(input.accessibility, input.params, input.stoppers,
			input.gateState, input.movementCosts);
}

BattleHex actorPosition(std::size_t actor)
{
	return BattleHex(actor < 7 ? 3 : 13, static_cast<si16>(1 + actor % 7));
}

Workload makeWorkload(std::string name, std::string description, bool flying,
	bool weighted, bool small, bool unique)
{
	Workload result{std::move(name), std::move(description), {}, unique, flying || small};
	result.inputs.reserve(queriesPerDecision);
	std::mt19937 random(0x56434d49u);
	std::array<bool, GameConstants::BFIELD_SIZE> scattered{};
	for(std::size_t hex = 0; hex < scattered.size(); ++hex)
		scattered[hex] = random() % 13 == 0;
	for(std::size_t candidate = 0; candidate < candidateCount; ++candidate)
		for(std::size_t actor = 0; actor < actorCount; ++actor)
		{
			Input input;
			input.params.startPosition = actorPosition(actor);
			input.params.side = actor < 7 ? BattleSide::ATTACKER : BattleSide::DEFENDER;
			input.params.doubleWide = actor % 3 == 0;
			input.params.flying = flying;
			input.params.knownAccessible = &battle::Unit::getHexes(input.params.startPosition,
				input.params.doubleWide, input.params.side);
			input.params.perspective = BattleSide::ATTACKER;
			input.accessibility.fill(EAccessibility::ACCESSIBLE);
			input.accessibility.demonicGateReservationBase.fill(EAccessibility::ACCESSIBLE);
			for(int hex = 0; hex < GameConstants::BFIELD_SIZE; ++hex)
			{
				const BattleHex position(hex);
				if(position.getX() == 0 || position.getX() == GameConstants::BFIELD_WIDTH - 1)
					input.accessibility[hex] = EAccessibility::SIDE_COLUMN;
				else if(small || (weighted && scattered[hex]))
					input.accessibility[hex] = EAccessibility::OBSTACLE;
				if(weighted && position.getX() == 8)
					input.movementCosts[hex] = 2;
			}
			if(small)
				for(const auto hex : input.params.startPosition.getNeighbouringTiles())
					input.accessibility[hex.toInt()] = EAccessibility::ACCESSIBLE;
			else
				for(std::size_t other = 0; other < actorCount; ++other)
					for(const auto hex : battle::Unit::getHexes(actorPosition(other), other % 3 == 0,
						other < 7 ? BattleSide::ATTACKER : BattleSide::DEFENDER))
						input.accessibility[hex.toInt()] = EAccessibility::ALIVE_STACK;
			for(const auto hex : *input.params.knownAccessible)
				input.accessibility[hex.toInt()] = EAccessibility::ACCESSIBLE;
			if(weighted)
			{
				input.stoppers.insert(BattleHex(7, 2));
				input.stoppers.insert(BattleHex(10, 8));
			}
			if(unique)
			{
				// Each candidate has a different visible layout; walking queries also
				// carry a distinct terrain cost. Actor starts distinguish its 14 queries.
				input.accessibility[BattleHex(static_cast<si16>(5 + candidate), 9).toInt()] = EAccessibility::OBSTACLE;
				input.movementCosts[BattleHex(9, 5).toInt()] = static_cast<std::int32_t>(candidate + 1);
			}
			if(!flying)
				input.accessibility.destructibleEnemyTurns =
					std::make_shared<const TBattlefieldTurnsArray>(input.params.destructibleEnemyTurns);
			result.inputs.push_back(std::move(input));
		}
	return result;
}

std::uint64_t verify(const Workload & workload)
{
	MovementQueryCache memo;
	std::uint64_t digest = initialDigest;
	for(const auto & input : workload.inputs)
	{
		const auto expected = legacy(input);
		const auto query = compile(input);
		const auto compiled = battle::solveMovementQuery(query);
		const auto native = direct(input);
		const auto external = movementBenchmark::legacyExternal(input.accessibility, input.params,
			input.stoppers, input.gateState, input.movementCosts);
		const auto cached = input.params.flying ? native
			: memo.getWalking(input.accessibility, input.params, input.stoppers,
				input.gateState, input.movementCosts);
		if(expected != compiled || expected != cached || expected != native || expected != external)
			throw std::runtime_error("Full distance/predecessor parity failed: " + workload.name);
		for(std::size_t hex = 0; hex < expected.distances.size(); ++hex)
		{
			mix(digest, expected.distances[hex]);
			mix(digest, static_cast<std::uint16_t>(expected.predecessors[hex]));
		}
	}
	const auto counts = memo.statistics();
	const auto cachedQueries = workload.inputs.front().params.flying ? 0 : queriesPerDecision;
	const auto expectedSolves = cachedQueries == 0 ? 0
		: workload.unique || workload.bypass ? queriesPerDecision : actorCount;
	if(counts.solves != expectedSolves || counts.hits != cachedQueries - expectedSolves)
		throw std::runtime_error("Workload did not establish its declared reuse: " + workload.name);
	return digest;
}

void retainResult(Sample & sample, const battle::MovementPaths & paths, std::size_t query)
{
	// Complete arrays were compared before timing. Four output cells keep the
	// timed digest cheap; both variants perform exactly the same digest work.
	for(std::size_t offset : {0u, 47u, 94u, 140u})
	{
		const auto hex = (query * 17 + offset) % GameConstants::BFIELD_SIZE;
		mix(sample.digest, paths.distances[hex]);
		mix(sample.digest, static_cast<std::uint16_t>(paths.predecessors[hex]));
	}
}

Sample measure(const Workload & workload, std::size_t variant, std::size_t decisions)
{
	Sample sample;
	const auto start = std::chrono::steady_clock::now();
	for(std::size_t decision = 0; decision < decisions; ++decision)
	{
		if(variant == 1)
		{
			// Cache construction, every query compilation, hashing/lookup, result
			// copy and cache destruction all lie inside the measured interval.
			MovementQueryCache memo;
			for(std::size_t index = 0; index < workload.inputs.size(); ++index)
			{
				const auto & input = workload.inputs[index];
				// Match CBattleInfoCallback: flying bypasses the cache before entry.
				if(input.params.flying)
				{
					retainResult(sample, direct(input), index);
					++sample.queries;
					++sample.solves;
				}
				else retainResult(sample, memo.getWalking(input.accessibility, input.params, input.stoppers,
					input.gateState, input.movementCosts), index);
			}
			const auto counts = memo.statistics();
			sample.queries += counts.queries;
			sample.hits += counts.hits;
			sample.solves += counts.solves;
			sample.evictions += counts.evictions;
			sample.peakEntries = std::max(sample.peakEntries, counts.entries);
		}
		else
		{
			for(std::size_t index = 0; index < workload.inputs.size(); ++index)
			{
				const auto & input = workload.inputs[index];
				retainResult(sample, variant == 0 ? legacy(input) : variant == 2
					? battle::solveMovementQuery(compile(input))
					: variant == 3 ? direct(input)
					: movementBenchmark::legacyExternal(input.accessibility, input.params, input.stoppers,
						input.gateState, input.movementCosts), index);
			}
			sample.queries += workload.inputs.size();
			sample.solves += workload.inputs.size();
		}
	}
	sample.microseconds = std::chrono::duration<double, std::micro>(
		std::chrono::steady_clock::now() - start).count();
	return sample;
}

std::size_t argument(std::string_view value, std::size_t maximum)
{
	std::size_t result = 0;
	const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
	if(parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() || result == 0 || result > maximum)
		throw std::runtime_error("Argument must be an integer in 1.." + std::to_string(maximum));
	return result;
}

void print(const Workload & workload, std::size_t sample, std::size_t order, std::string_view variant,
	std::size_t decisions, const Sample & value)
{
	std::cout << workload.name << ',' << sample << ',' << order << ',' << variant << ',' << decisions
		<< ',' << value.queries << ',' << value.hits << ',' << value.solves << ',' << value.evictions
		<< ',' << value.peakEntries << ',' << std::fixed << std::setprecision(3) << value.microseconds
		<< ',' << value.microseconds / decisions << ',' << value.digest << '\n';
}
}

int main(int argc, char ** argv)
{
	try
	{
		std::size_t samples = 8;
		std::size_t decisions = 12;
		for(int index = 1; index < argc; index += 2)
		{
			if(index + 1 >= argc)
				throw std::runtime_error("Usage: MovementQueryBenchmark [--samples 1..32] [--decisions 1..1024]");
			const std::string_view option(argv[index]);
			if(option == "--samples") samples = argument(argv[index + 1], 32);
			else if(option == "--decisions") decisions = argument(argv[index + 1], 1024);
			else throw std::runtime_error("Unknown option: " + std::string(option));
		}
		const std::vector<Workload> workloads{
			makeWorkload("walking_shared", "14 actor geometries repeated across 8 candidates; occupied stacks; no terrain cost", false, false, false, false),
			makeWorkload("weighted_shared", "same 14 geometries repeated; deterministic scattered blockers and weighted column plus stopping tiles", false, true, false, false),
			makeWorkload("walking_unique", "112 distinct actor/layout/cost queries; deliberately zero reuse", false, false, false, true),
			makeWorkload("flying_shared", "14 flying actor geometries repeated across 8 candidates", true, false, false, false),
			makeWorkload("small_shared", "14 actors confined to small isolated reachable components repeated across 8 candidates", false, false, true, false),
			makeWorkload("flying_unique", "112 distinct flying actor/layout queries; deliberately zero reuse", true, false, false, true)};
		std::cout << "# Component benchmark: 14 actors x 8 candidate evaluations = 112 queries per decision.\n"
			<< "# Prepared visible-world boundary: excludes gathering game objects, AI search, simulation, rendering and result metadata assembly.\n"
			<< "# Timings include query compilation, hash lookup, per-decision cache construction/destruction and equal sampled-output digest work.\n"
			<< "# legacy retains the inline frozen reference; legacy_external adds a separate translation-unit control for call-boundary/code-layout cost.\n"
			<< "# Production flying uses the same small inline fill as the callback; these prepared-world timings exclude callback output/metadata initialization.\n"
			<< "# Fresh decision caches; deterministic seed 0x56434d49; alternating variant order; warm process/OS caches; no whole-game claim.\n";
		for(const auto & workload : workloads)
			std::cout << "# " << workload.name << ": " << workload.description
				<< "; full_array_parity_checksum=" << verify(workload) << '\n';
		std::cout << "workload,sample,run_order,variant,decisions,queries,hits,solves,evictions,peak_entries,elapsed_us,us_per_decision,checksum\n";
		for(const auto & workload : workloads)
			for(std::size_t sample = 0; sample < samples; ++sample)
			{
				const std::array<std::string_view, 5> names{"legacy", "memo", "compiled", "direct", "legacy_external"};
				std::uint64_t expectedDigest = 0;
				for(std::size_t order = 0; order < names.size(); ++order)
				{
					const auto variant = (sample + order) % names.size();
					const auto measured = measure(workload, variant, decisions);
					if(order != 0 && measured.digest != expectedDigest)
						throw std::runtime_error("Timed checksum differs: " + workload.name);
					expectedDigest = measured.digest;
					print(workload, sample, order, names[variant], decisions, measured);
				}
			}
		return 0;
	}
	catch(const std::exception & error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
