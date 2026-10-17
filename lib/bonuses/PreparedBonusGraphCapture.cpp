/*
 * PreparedBonusGraphCapture.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "PreparedBonusGraphStorage.h"
#include <algorithm>
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

size_t PreparedBonusGraphStorage::captureImpl(const CBonusSystemNode & node, CBonusSystemNode * writable)
{
	requireBuilding();
	for(size_t index = 0; index < entries.size(); ++index)
		if(entries[index].node == &node)
		{
			if(entries[index].writable != writable)
				throw std::invalid_argument("Captured graph classification cannot change");
			return index;
		}
	auto original = read(node);
	Entry entry{&node, writable, node.nodeChanged.load(), node.cachedLast, original, original};
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
	auto & entry = entries.at(index);
	if(!entry.writable && !sameValues(entry.original, replacement))
		throw std::invalid_argument("Read-only graph row cannot change storage");
	entry.replacement = std::move(replacement);
}

bool PreparedBonusGraphStorage::mutableCaptured(const CBonusSystemNode * node) const noexcept
{
	return std::any_of(entries.begin(), entries.end(), [node](const Entry & entry)
	{
		return entry.node == node && entry.writable;
	});
}

size_t PreparedBonusGraphStorage::capture(CBonusSystemNode & node)
{
	return captureImpl(node, &node);
}

size_t PreparedBonusGraphStorage::captureReadOnly(const CBonusSystemNode & node)
{
	return captureImpl(node, nullptr);
}

bool PreparedBonusGraphStorage::sameValues(const Values & first, const Values & second) noexcept
{
	return sameBonuses(first.accepted, second.accepted)
		&& sameBonuses(first.exported, second.exported) && first.parents == second.parents
		&& first.propagatingParents == second.propagatingParents && first.children == second.children;
}
