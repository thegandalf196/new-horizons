/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once

class CGameHandler;

namespace newHorizonsForcedMarch
{
/// At the start of each week, give every owned hero with Sylvia's Forced March
/// specialty its pending next-combat Morale penalty (never stacked).
void grantWeeklyMoralePenalties(CGameHandler & handler);
}
