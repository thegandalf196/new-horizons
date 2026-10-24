/*
 * Part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#pragma once
class MetaString;
namespace newHorizonsNecromancy { struct NecromancyResult; }
namespace necromancyPresentation
{
void appendConversions(MetaString & text, const newHorizonsNecromancy::NecromancyResult & result);
}
