/*
 * NewHorizonsMagicPresentation.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include <cstdint>
#include <string>

class CGHeroInstance;
namespace spells { class Spell; }

// Internal presentation stages; consumers retain NewHorizonsMagic.h's API.
namespace newHorizonsMagic::presentation
{
void applyBaseDescription(std::string & result, const CGHeroInstance * hero, const spells::Spell * spell);
void applyContextualOverrides(std::string & result, const CGHeroInstance * hero, const spells::Spell * spell);
std::string percentFromBasisPoints(int basisPoints);
std::string fixedPointFromScaledValue(int64_t scaledValue, int decimalPlaces);
}
