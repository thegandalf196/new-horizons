/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "ConsecratedArms.h"
struct BattleAttackInfo;
struct DamageEstimation;
namespace consecratedArms
{
struct JudgmentPotency
{
	int basisPoints = 0;
	bool righteousSentence = false;
};
DLL_LINKAGE int fearlessAdvanceChance(const battle::Unit & attacker);
DLL_LINKAGE void adjustRetaliationDamage(const BattleAttackInfo & attack, DamageEstimation & damage);
DLL_LINKAGE int64_t judgmentDamage(int64_t actualBodyDamage, int judgmentBP, bool righteousSentence, bool targetAlreadyActed);
DLL_LINKAGE JudgmentPotency judgmentPotency(const battle::Unit & source);
DLL_LINKAGE int64_t judgmentRawDamage(const JudgmentPotency & potency, const battle::Unit & target, int64_t actualBodyDamage);
DLL_LINKAGE int64_t casualtyLimit(int64_t currentCount, int casualtyCeilingBP);
}
