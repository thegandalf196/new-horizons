/*
 * BonusLimitation.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#pragma once

class BonusList;
class BonusEvaluationContext;
/// Legacy ordered accepted/pending fixed point, with explicit evaluation inputs.
/// Input is already collected/updater-processed by the caller; not a DAG query.
/// Allocates output/pending storage; not a no-throw world commit operation.
DLL_LINKAGE void limitBonuses(const BonusList & allBonuses, BonusList & out, const BonusEvaluationContext & evaluation);
