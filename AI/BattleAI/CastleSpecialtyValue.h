/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once

#include "PossibleSpellcast.h"
#include "../../lib/battle/BattleSide.h"
#include <memory>
#include <vector>

class CBattleInfoCallback;
class CPlayerBattleCallback;
class HypotheticBattle;
class DamageCache;
class BattleHex;
class Environment;
namespace spells { class Mechanics; }

/// Battle AI riders for the revised Castle specialties that a spell candidate's
/// own forecast cannot see: Loynis's Sermon prayer on his Light spells, Adela's
/// Benediction of Arms on her Bless target and Adelaide's Frost Mantle on a
/// stack-centred Frost Ring. Heuristic exposure terms only; no search.
namespace castleSpecialtyAI
{
DLL_LINKAGE void addSpecialtyValue(std::vector<PossibleSpellcast> & candidates, const CPlayerBattleCallback & battle,
	const std::shared_ptr<HypotheticBattle> & board, BattleSide side, DamageCache & damage, float positive);
/// Expected Sermon prayer value for one of Loynis's Light spells (0 when inactive).
DLL_LINKAGE float sermonValue(const CPlayerBattleCallback & battle, const std::shared_ptr<HypotheticBattle> & board,
	BattleSide side, DamageCache & damage);
/// Value of Adela's future Benediction prayer on a Blessed friendly stack.
DLL_LINKAGE float benedictionValue(const CPlayerBattleCallback & battle, const std::shared_ptr<HypotheticBattle> & board,
	BattleSide side, uint32_t blessed, DamageCache & damage);
/// Orrin's Volley Command: immediate reduced shots of every ready shooter at
/// the Focus Fire! target, with the Order's ranged bonus (0 when inactive).
DLL_LINKAGE float volleyValue(const CBattleInfoCallback & battle, const std::shared_ptr<CBattleInfoCallback> & board,
	BattleSide side, uint32_t target, int focusFirePercent, DamageCache & damage);
/// Adelaide's Frost Ring centred on a stack is a distinct candidate (cloak or chill).
DLL_LINKAGE bool keepsFrostMantleCentre(const spells::Mechanics & mechanics, BattleHex centre);
/// Value of Frost Mantle cloaking a friendly Frost Ring centre stack.
DLL_LINKAGE float frostMantleValue(const CBattleInfoCallback & battle, const std::shared_ptr<HypotheticBattle> & board,
	BattleSide side, uint32_t centre, DamageCache & damage);
/// Value of Frost Mantle chilling an enemy Frost Ring centre stack (0 for a friendly or immune one).
DLL_LINKAGE float frostMantleChillValue(const CBattleInfoCallback & battle, const std::shared_ptr<HypotheticBattle> & board,
	BattleSide side, uint32_t centre, DamageCache & damage);

/// Riders the hero-action shortlist must see when it ranks candidates, or a
/// Quick search never refines them: Orrin's volley on Focus Fire!, Adelaide's
/// Frost Mantle on a stack-centred Frost Ring and the Rampart cast riders (Uland's
/// Living Remedy, Alagar's Brittle). The board is built only when needed.
class DLL_LINKAGE ScoutRiders
{
	const Environment * environment;
	std::shared_ptr<CBattleInfoCallback> source;
	BattleSide side;
	DamageCache * damage;
	std::shared_ptr<HypotheticBattle> board;
public:
	ScoutRiders(const Environment * environment, std::shared_ptr<CBattleInfoCallback> source, BattleSide side,
		DamageCache & damage);
	/// Unscaled rider value of the candidate (0 when it has none).
	float value(const PossibleSpellcast & candidate);
};
}
