/*
 * ServerCallback.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */

#pragma once

#include <cstdint>
#include <functional>

#include "scripting/ApiTags.h"

namespace vstd
{
	class RNG;
}

namespace battle
{
	class Unit;
}

class IBattleInfoCallback;
class BattleID;
enum class BattleSide : int8_t;

struct CPackForClient;
struct BattleLogMessage;
struct BattleStackMoved;
struct BattleUnitsChanged;
struct SetStackEffect;
struct StacksInjured;
struct BattleObstaclesChanged;
struct CatapultAttack;

class DLL_LINKAGE ServerCallback : public scripting::ApiRawPointer<ServerCallback>
{
public:
	virtual ~ServerCallback() = default;

	virtual void complain(const std::string & problem) = 0;
	virtual bool describeChanges() const = 0;

	virtual vstd::RNG * getRNG() = 0;

	/// Rolls a chance-based combat ability of the given unit
	virtual bool rollCombatAbility(const IBattleInfoCallback & battle, const battle::Unit & actor, int percentageChance) = 0;
	/// Resolve a classified adverse result for its affected army. Older callbacks
	/// keep a single draw; authoritative battle callbacks may spend a reroll.
	virtual bool resolveAdverseCombatRoll(const BattleID &, BattleSide, bool, bool,
		const std::function<bool()> & draw)
	{
		return draw();
	}

	virtual void apply(CPackForClient & pack) = 0;

	virtual void apply(BattleLogMessage & pack) = 0;
	virtual void apply(BattleStackMoved & pack) = 0;
	virtual void apply(BattleUnitsChanged & pack) = 0;
	virtual void apply(SetStackEffect & pack) = 0;
	virtual void apply(StacksInjured & pack) = 0;
	virtual void apply(BattleObstaclesChanged & pack) = 0;
	virtual void apply(CatapultAttack & pack) = 0;

	/// Records a completed hero spell in a detached projection; live callbacks do not mutate battle state.
	virtual void recordCompletedHeroSpellCast(BattleSide side) { (void)side; }
	/// Records the saved level of a completed hero spell in a detached projection.
	/// The default preserves older callbacks while remaining a no-op for live state.
	virtual void recordCompletedHeroSpellCast(BattleSide side, int32_t spellLevel)
	{
		(void)spellLevel;
		recordCompletedHeroSpellCast(side);
	}
};
