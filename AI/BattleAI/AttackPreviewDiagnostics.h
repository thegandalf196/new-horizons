/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
class AttackPossibility;
struct BattleAttackInfo;
namespace attackPreviewDiagnostics
{
void result(const char * label, const AttackPossibility &, const BattleAttackInfo &);
}
