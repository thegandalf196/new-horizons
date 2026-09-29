/*
 * StackInfoStatusPresentation.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace newHorizonsBattleStatus
{
struct PhysicalPoisonStatus
{
	int64_t baseDamage = 0;
	int32_t activationsRemaining = 0;
	int64_t nextTickDamage = 0;

	bool active() const
	{
		return baseDamage > 0 && activationsRemaining > 0 && nextTickDamage > 0;
	}

	bool operator==(const PhysicalPoisonStatus &) const = default;
};

struct RegenerationStatus
{
	int32_t rateMillionths = 0;
	int64_t healablePendingHealth = 0;
	int32_t remainingRounds = 0;

	bool active() const
	{
		return rateMillionths > 0 && remainingRounds > 0;
	}

	bool operator==(const RegenerationStatus &) const = default;
};

struct TemporaryCreatureStatus
{
	int32_t remainingCount = 0;

	bool active() const
	{
		return remainingCount > 0;
	}

	bool operator==(const TemporaryCreatureStatus &) const = default;
};

inline TemporaryCreatureStatus makeTemporaryCreatureStatus(int32_t resurrectedCount)
{
	return {std::max<int32_t>(0, resurrectedCount)};
}

inline PhysicalPoisonStatus makePhysicalPoisonStatus(int64_t baseDamage, int32_t activationsRemaining, int64_t nextTickDamage)
{
	if(baseDamage <= 0 || activationsRemaining <= 0 || nextTickDamage <= 0)
		return {};

	return {baseDamage, activationsRemaining, nextTickDamage};
}

enum class StackStatusIconKind
{
	TIME_STOP,
	SPELL_LOCK,
	TEMPORARY_CREATURES,
	VAMPIRISM,
	PHYSICAL_POISON,
	REGENERATION,
	SHADOW_GIFT_BUFF,
	SHADOW_GIFT_CAP,
	FOCUS_OR_ARCANE,
	ORDINARY
};

struct StackStatusDisplayPlan
{
	std::vector<std::size_t> visibleEntryIndices;
	bool overflow = false;
	bool ellipsisUsesSlot = false;
};

inline int stackStatusPriority(StackStatusIconKind kind)
{
	switch(kind)
	{
		case StackStatusIconKind::TIME_STOP: return 0;
		case StackStatusIconKind::SPELL_LOCK: return 1;
		case StackStatusIconKind::VAMPIRISM: return 2;
		case StackStatusIconKind::TEMPORARY_CREATURES: return 2;
		case StackStatusIconKind::PHYSICAL_POISON: return 3;
		case StackStatusIconKind::REGENERATION: return 4;
		case StackStatusIconKind::SHADOW_GIFT_BUFF: return 5;
		case StackStatusIconKind::SHADOW_GIFT_CAP: return 6;
		case StackStatusIconKind::FOCUS_OR_ARCANE: return 7;
		case StackStatusIconKind::ORDINARY: return 8;
	}
	return 9;
}

inline StackStatusDisplayPlan stackStatusDisplayPlan(const std::vector<StackStatusIconKind> & visibleEntryKinds,
	std::size_t totalEffectCount)
{
	std::vector<std::size_t> orderedIndices;
	orderedIndices.reserve(visibleEntryKinds.size());
	for(std::size_t index = 0; index < visibleEntryKinds.size(); ++index)
		orderedIndices.push_back(index);

	std::stable_sort(orderedIndices.begin(), orderedIndices.end(), [&visibleEntryKinds](std::size_t left, std::size_t right)
	{
		return stackStatusPriority(visibleEntryKinds[left]) < stackStatusPriority(visibleEntryKinds[right]);
	});

	const bool hasPhysicalPoison = std::find(visibleEntryKinds.begin(), visibleEntryKinds.end(), StackStatusIconKind::PHYSICAL_POISON)
		!= visibleEntryKinds.end();
	StackStatusDisplayPlan result;
	result.overflow = totalEffectCount > 3;
	result.ellipsisUsesSlot = result.overflow && !hasPhysicalPoison;
	const std::size_t visibleLimit = result.ellipsisUsesSlot ? 2 : 3;
	const auto visibleCount = std::min(orderedIndices.size(), visibleLimit);
	result.visibleEntryIndices.assign(orderedIndices.begin(), orderedIndices.begin() + visibleCount);
	return result;
}
}
