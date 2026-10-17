/*
 * MovementBonusProvenance.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once

class IBonusBearer;
struct Bonus;

namespace movementBonusProvenance
{
/// Conservative availability proof, NOT a current-position bonus evaluator.
/// Reject every limiter/updater (including currently inactive native records),
/// unknown bearer, source cycle or bounded-scan overflow. No cache is refreshed.
/// Native root dispatch is exact CStack/CGHeroInstance/CBonusSystemNode only.
/// The caller MUST retain original graph/provider/Bonus lifetimes and exclude
/// external mutation throughout this scan, projection and retained receipt.
bool invariantSources(const IBonusBearer & source);
bool invariantRecord(const Bonus & bonus);
}
