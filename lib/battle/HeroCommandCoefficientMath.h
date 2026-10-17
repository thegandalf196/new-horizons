/*
 * HeroCommandCoefficientMath.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once
#include <array>

namespace heroCommands::coefficientMath
{
DLL_LINKAGE int boundedCoefficient(const std::array<double, 3> & terms, const std::array<int, 3> & factors);
DLL_LINKAGE double boundedCoefficientComponent(double coefficient, double factor);
}
