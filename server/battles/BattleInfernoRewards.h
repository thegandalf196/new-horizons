/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
class CGameHandler;
class CGHeroInstance;
namespace battleInfernoRewards
{
void grantCutpurse(CGameHandler & handler, const CGHeroInstance * winner, const CGHeroInstance * loser);
}
