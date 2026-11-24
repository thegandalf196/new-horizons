/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "SylvanAmbushState.h"
#include <optional>
#include <string_view>
class CGHeroInstance;
class CBattleInfoCallback;
struct BattleAttackInfo;
struct DamageAttackInfo;
namespace battle { class Unit; class CUnitState; }

/// Sylvan Tactics policy 5 (Ambush). A Waiting friendly ordinary stack may
/// become Concealed (rank slots, Forest Cover's terrain slot, never two rounds
/// in a row). Concealment blocks ranged attacks and hostile single-target
/// spells, never melee; its delayed activation may make one Ambush strike
/// against prey, an enemy whose latest voluntary approach this round ended
/// closer to the ambusher. All state is plain unit state (SylvanAmbushState).
namespace newHorizonsSylvanAmbush
{
inline constexpr int PATIENT_HUNTER_AMBUSH_PERCENT = 30;
inline constexpr int APEX_PREDATOR_PERCENT = 25;
inline constexpr int STARTLE_RETALIATION_PERCENT = 50;
inline constexpr int DAPPLED_COVER_PERCENT = 25;

/// Policy-5 capture, independent of rank: decides which meaning a reused slot has.
DLL_LINKAGE bool captured(const CGHeroInstance * hero);
/// Policy-5 capture with at least Basic Sylvan Tactics.
DLL_LINKAGE bool enabled(const CGHeroInstance * hero);
/// A selected policy-5 perk, by its slot suffix (for example "startle").
DLL_LINKAGE bool hasPerk(const CGHeroInstance * hero, std::string_view perk);
/// Either fighting hero has policy-5 Sylvan Tactics.
DLL_LINKAGE bool hasBattlePolicy(const CBattleInfoCallback & battle);
DLL_LINKAGE int rankSlots(const CGHeroInstance * hero);

DLL_LINKAGE bool concealed(const battle::Unit * unit);
DLL_LINKAGE bool strikeReady(const battle::Unit * unit);
/// The smallest hex distance between the two footprints (two-hex units use both).
DLL_LINKAGE int footprintDistance(const battle::Unit & unit, BattleHex position,
	const battle::Unit & other, BattleHex otherPosition);
/// The target's current-round approach record ended closer to the ambusher.
/// A pending strike measures from the hex the ambusher hid on.
DLL_LINKAGE bool prey(const CBattleInfoCallback & battle, const battle::Unit * ambusher, const battle::Unit * target);
/// The first primary attack of a delayed activation that began Concealed, against prey.
DLL_LINKAGE bool ambushStrike(const CBattleInfoCallback & battle, const BattleAttackInfo & attack);
/// This retaliation answers an Ambush strike by a Startle hero's stack.
DLL_LINKAGE bool startled(const CBattleInfoCallback & battle, const BattleAttackInfo & attack);

/// Patient Hunter and Apex Predator, added into one physical damage percent.
/// Older policies keep Patient Hunter's +15% after Waiting.
DLL_LINKAGE int waitStrikePercent(const CBattleInfoCallback & battle, const BattleAttackInfo & attack,
	bool patientHunter, int ordinaryPercent);
DLL_LINKAGE int huntingPackPercent(const CBattleInfoCallback & battle, const battle::Unit * attacker,
	const battle::Unit * target);
/// Startle's retaliation factor.
DLL_LINKAGE void finalizeDamage(const CBattleInfoCallback & battle, const BattleAttackInfo & info,
	DamageAttackInfo & payload);
/// Damage that depends on Ambush state: never reuse an ID-only cached value.
DLL_LINKAGE bool dynamicDamage(const CBattleInfoCallback & battle, const battle::Unit * attacker,
	const battle::Unit * defender);

/// What a Wait by this stack would grant now. The server grants exactly
/// `slot`; the client's Wait button reads the same offer.
struct ConcealmentOffer
{
	std::optional<battle::SylvanAmbushState::Source> slot;
	int rankSlots = 0;
	int rankUsed = 0;
};
DLL_LINKAGE ConcealmentOffer concealmentOffer(const CBattleInfoCallback & battle, const battle::CUnitState & unit);
/// The slot a Waiting stack would take now, or none when it cannot hide.
DLL_LINKAGE std::optional<battle::SylvanAmbushState::Source> concealmentSlot(const CBattleInfoCallback & battle,
	const battle::CUnitState & unit);
/// Call after Forest Ambush has accepted the same Wait.
DLL_LINKAGE void acceptWait(const CBattleInfoCallback & battle, battle::CUnitState & unit);
/// Call before Forest Ambush ends its concealment for the same activation.
DLL_LINKAGE void beginActivation(const CBattleInfoCallback & battle, battle::CUnitState & unit, bool delayed);
/// Any attack reveals and marks the round; an Ambush strike also marks its victim.
DLL_LINKAGE void receiveAttack(const CBattleInfoCallback & battle, const BattleAttackInfo & attack,
	battle::CUnitState & target);
/// One committed voluntary movement step or segment.
DLL_LINKAGE void recordApproach(const CBattleInfoCallback & battle, battle::CUnitState & unit,
	BattleHex before, BattleHex after);
}
