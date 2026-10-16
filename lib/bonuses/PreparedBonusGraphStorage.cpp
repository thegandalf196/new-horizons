/*
 * PreparedBonusGraphStorage.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "PreparedBonusGraphStorage.h"

#include <algorithm>
#include <bit>
#include <stdexcept>
#include <utility>

PreparedBonusGraphStorage::Values PreparedBonusGraphStorage::read(const CBonusSystemNode & node)
{
	return {node.bonuses, node.exportedBonuses, node.parentsToInherit,
		node.parentsToPropagate, node.children};
}

bool PreparedBonusGraphStorage::sameBonuses(const BonusList & first, const BonusList & second) noexcept
{
	return first.size() == second.size() && std::equal(first.begin(), first.end(), second.begin());
}

void PreparedBonusGraphStorage::requireBuilding() const
{
	if(phase != Phase::BUILDING)
		throw std::logic_error("Prepared graph storage is no longer editable");
}

size_t PreparedBonusGraphStorage::capture(CBonusSystemNode & node)
{
	requireBuilding();
	for(size_t index = 0; index < entries.size(); ++index)
		if(entries[index].node == &node)
			return index;
	auto original = read(node);
	Entry entry{&node, node.nodeChanged.load(), node.cachedLast, original, original};
	entries.push_back(std::move(entry));
	return entries.size() - 1;
}

const PreparedBonusGraphStorage::Values & PreparedBonusGraphStorage::projected(size_t index) const
{
	if(phase == Phase::COMMITTED)
		throw std::logic_error("Prepared graph storage has been consumed");
	return entries.at(index).replacement;
}

void PreparedBonusGraphStorage::replace(size_t index, Values replacement)
{
	requireBuilding();
	entries.at(index).replacement = std::move(replacement);
}

bool PreparedBonusGraphStorage::captured(const CBonusSystemNode * node) const noexcept
{
	return std::any_of(entries.begin(), entries.end(), [node](const Entry & entry)
	{
		return entry.node == node;
	});
}

void PreparedBonusGraphStorage::addInvalidation(bool advanceGeneration, std::vector<CBonusSystemNode *> targets)
{
	requireBuilding();
	for(size_t index = 0; index < targets.size(); ++index)
	{
		if(!targets[index] || !captured(targets[index]))
			throw std::invalid_argument("Prepared invalidation target was not captured");
		if(std::find(targets.begin(), targets.begin() + index, targets[index]) != targets.begin() + index)
			throw std::invalid_argument("Prepared invalidation repeats a target within one generation");
	}
	invalidations.push_back({advanceGeneration, std::move(targets)});
}

bool PreparedBonusGraphStorage::payloadChanged(const Entry & entry) const noexcept
{
	return !sameBonuses(entry.original.accepted, entry.replacement.accepted)
		|| !sameBonuses(entry.original.exported, entry.replacement.exported)
		|| entry.original.parents != entry.replacement.parents;
}

int32_t PreparedBonusGraphStorage::nextGeneration(int32_t generation) noexcept
{
	return std::bit_cast<int32_t>(std::bit_cast<uint32_t>(generation) + uint32_t{1});
}

bool PreparedBonusGraphStorage::borrowsGeneration() const noexcept
{
	return !invalidations.empty() && !invalidations.front().advance;
}

int32_t PreparedBonusGraphStorage::finalGeneration(int32_t generation) const noexcept
{
	for(const auto & event : invalidations)
		if(event.advance)
			generation = nextGeneration(generation);
	return generation;
}

bool PreparedBonusGraphStorage::validProgram(int32_t generation) const noexcept
{
	for(const auto & entry : entries)
	{
		int32_t current = generation, final = entry.version;
		bool targeted = false;
		for(const auto & event : invalidations)
		{
			if(event.advance)
				current = nextGeneration(current);
			if(std::find(event.targets.begin(), event.targets.end(), entry.node) != event.targets.end())
			{
				final = current;
				targeted = true;
			}
		}
		const bool changed = payloadChanged(entry);
		if(!targeted)
		{
			if(changed)
				return false;
			continue;
		}
		if(final == entry.version || final == entry.cachedVersion)
			return false;
		// Version wrap must not resurrect a retained named request either.
		for(const auto & request : entry.node->cachedRequests)
			if(request.second.first == final)
				return false;
	}
	return true;
}

void PreparedBonusGraphStorage::validate(const Values & value)
{
	for(const auto * bonuses : {&value.accepted, &value.exported})
		for(const auto & bonus : *bonuses)
			if(!bonus)
				throw std::invalid_argument("Prepared graph storage contains a null bonus");
	for(const auto * parent : value.parents)
		if(!parent)
			throw std::invalid_argument("Prepared graph storage contains a null parent");
	for(const auto * parent : value.propagatingParents)
		if(!parent)
			throw std::invalid_argument("Prepared graph storage contains a null propagation parent");
	for(const auto * child : value.children)
		if(!child)
			throw std::invalid_argument("Prepared graph storage contains a null child");
}

bool PreparedBonusGraphStorage::stillCurrent() const noexcept
{
	if(phase == Phase::PREPARED && borrowsGeneration()
		&& CBonusSystemNode::preparedGraphGeneration() != preparedGeneration)
		return false;
	for(const auto & entry : entries)
	{
		const auto & node = *entry.node;
		if(node.nodeChanged.load() != entry.version || node.cachedLast != entry.cachedVersion
			|| !sameBonuses(node.bonuses, entry.original.accepted)
			|| !sameBonuses(node.exportedBonuses, entry.original.exported)
			|| node.parentsToInherit != entry.original.parents
			|| node.parentsToPropagate != entry.original.propagatingParents
			|| node.children != entry.original.children)
			return false;
	}
	return true;
}

void PreparedBonusGraphStorage::prepare()
{
	requireBuilding();
	if(!stillCurrent())
		throw std::runtime_error("Stale prepared bonus graph storage");
	const auto generation = CBonusSystemNode::preparedGraphGeneration();
	for(const auto & entry : entries)
		validate(entry.replacement);
	if(!validProgram(generation))
		throw std::invalid_argument("Prepared generation program would retain a stale bonus cache");
	for(auto & entry : entries)
	{
		entry.node->bonuses.reserveForReplacement(entry.replacement.accepted.size());
		entry.node->exportedBonuses.reserveForReplacement(entry.replacement.exported.size());
	}
	if(borrowsGeneration() && CBonusSystemNode::preparedGraphGeneration() != generation)
		throw std::runtime_error("Borrowed prepared graph generation changed");
	preparedGeneration = generation;
	phase = Phase::PREPARED;
}

bool PreparedBonusGraphStorage::readyToCommit() const noexcept
{
	return phase == Phase::PREPARED && stillCurrent()
		&& validProgram(CBonusSystemNode::preparedGraphGeneration());
}

bool PreparedBonusGraphStorage::commit() noexcept
{
	if(!readyToCommit())
		return false;
	const auto generation = CBonusSystemNode::preparedGraphGeneration();
	if((borrowsGeneration() && generation != preparedGeneration) || !validProgram(generation)
		|| !CBonusSystemNode::reservePreparedGenerations(generation, finalGeneration(generation)))
		return false;
	for(auto & entry : entries)
	{
		auto & node = *entry.node;
		auto & prepared = entry.replacement;
		static_assert(noexcept(node.parentsToInherit.swap(prepared.parents)));
		static_assert(noexcept(node.parentsToPropagate.swap(prepared.propagatingParents)));
		static_assert(noexcept(node.children.swap(prepared.children)));
		node.bonuses.replacePrepared(prepared.accepted);
		node.exportedBonuses.replacePrepared(prepared.exported);
		node.parentsToInherit.swap(prepared.parents);
		node.parentsToPropagate.swap(prepared.propagatingParents);
		node.children.swap(prepared.children);
	}
	auto current = generation;
	for(const auto & event : invalidations)
	{
		if(event.advance)
			current = nextGeneration(current);
		CBonusSystemNode::publishPreparedInvalidation(current, event.targets);
	}
	phase = Phase::COMMITTED;
	return true;
}

std::unique_ptr<CBonusSystemNode::PreparedLocalBonusReplacement> CBonusSystemNode::prepareLocalBonusReplacement(
	const std::vector<std::shared_ptr<Bonus>> & replacement)
{
	for(const auto & bonus : exportedBonuses)
		if(bonus->propagator && !vstd::contains(replacement, bonus))
			throw std::runtime_error("Exact local bonus replacement cannot remove a propagated bonus");
	for(const auto & bonus : replacement)
		if(!bonus || (bonus->propagator && !vstd::contains(exportedBonuses, bonus)))
			throw std::runtime_error("Exact local bonus replacement cannot add a propagated bonus");
	auto prepared = std::make_unique<PreparedLocalBonusReplacement>();
	for(const auto & bonus : bonuses)
		if(bonus->propagator || !vstd::contains(exportedBonuses, bonus))
			prepared->accepted.push_back(bonus);
	for(const auto & bonus : replacement)
	{
		prepared->exported.push_back(bonus);
		if(!bonus->propagator)
			prepared->accepted.push_back(bonus);
	}
	exportedBonuses.reserveForReplacement(prepared->exported.size());
	bonuses.reserveForReplacement(prepared->accepted.size());
	return prepared;
}

void CBonusSystemNode::commitLocalBonusReplacement(const PreparedLocalBonusReplacement & prepared) noexcept
{
	exportedBonuses.replacePrepared(prepared.exported);
	bonuses.replacePrepared(prepared.accepted);
	nodeHasChanged();
}
