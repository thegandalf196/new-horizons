/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../constants/EntityIdentifiers.h"
class CStack;
class CCreature;
namespace battle
{
struct StackFormSourceIdentity
{
	CreatureID baseCreature;
	const CCreature * attached; // Borrowed ACTUAL source node, never a detached-task/owned capture value.
};
/// Exact source metadata only, not a lifetime/visibility/authority certificate.
/// Caller retains native stack/world/library and its real state interval through all use.
DLL_LINKAGE StackFormSourceIdentity stackFormSourceIdentity(const CStack & stack);
}
