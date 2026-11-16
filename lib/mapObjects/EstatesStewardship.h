/* Part of VCMI engine; GNU General Public License v2.0 or later. */
#pragma once

class CGHeroInstance;
class CGTownInstance;

namespace newHorizonsEstates
{
/// Reads the kingdom's single assignment map and revalidates current ownership
/// and qualification. Physical residence is deliberately irrelevant.
DLL_LINKAGE const CGHeroInstance * townSteward(const CGTownInstance & town);
}
