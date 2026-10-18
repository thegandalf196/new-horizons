/*
 * Part of VCMI engine. Authors: listed in AUTHORS.
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once

class CLoadFile;
class CGameState;

namespace gameSave
{
/// Ordinary world envelope and native decode fixups only. Does not construct a
/// map dispatcher or execute its script chunk; the normal load wrapper owns that.
DLL_LINKAGE void readGameData(CLoadFile & file, CGameState & world);
}
