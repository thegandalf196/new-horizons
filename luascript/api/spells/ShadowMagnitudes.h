/*
 * ShadowMagnitudes.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

namespace scripting::api
{
class MethodRegistrar;

/// Add read-only magnitudes for supported Shadow spells to the Mechanics proxy.
void registerShadowMagnitudeMethods(MethodRegistrar & registrar);
}
