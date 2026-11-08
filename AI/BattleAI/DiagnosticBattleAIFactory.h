/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#pragma once
#include "BattleAIDiagnosticAdmission.h"
#include <string>
class CBattleGameInterface;
namespace battleAIDiagnostic
{
/// DIAGNOSTIC ONLY. Normal AIFactory and preferences remain unchanged.
/// The caller retains observer until the returned AI is destroyed; IDs resolve once before this call.
DLL_LINKAGE std::shared_ptr<CBattleGameInterface> createBattleAI(const std::string & name,
	Spec spec, Observer * observer = nullptr);
}
