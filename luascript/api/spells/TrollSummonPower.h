/*
 * TrollSummonPower.h, part of VCMI engine
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

/// Add the read-only Summon Trolls HP-budget query to the Mechanics proxy.
void registerTrollSummonMethods(MethodRegistrar & registrar);
}
