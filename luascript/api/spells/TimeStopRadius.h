/*
 * TimeStopRadius.h, part of VCMI engine
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

/// Add the read-only cast-time radius query to the existing Mechanics proxy.
void registerTimeStopRadiusMethods(MethodRegistrar & registrar);
}
