/*
 * AttackPreviewArcheryAdmission.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once
#include <cstdint>

struct BattleAttackInfo;
class CGHeroInstance;

namespace attackPreview
{
struct ArcheryReceiptAdmission
{
	bool deadeye = false;
	bool crossfire = false;
	bool suppression = false;
	bool rainOfArrows = false;
};

ArcheryReceiptAdmission archeryReceiptAdmission(const BattleAttackInfo & attack,
	bool ordinaryShooter, const CGHeroInstance * hero, int32_t round, int32_t activationSerial);
}
