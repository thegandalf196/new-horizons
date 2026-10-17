/*
 * PurifyRuleFixtures.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#pragma once
#include "../../lib/json/JsonNode.h"

namespace purifyTest
{
/// Valid historical magic input, independent of the fixture's current Hero capture.
JsonNode savedV2MagicRules();
/// Existing current-command numeric neutralizer, NOT a runtime gameplay rule.
JsonNode neutralSpellCompetitionCommands();
}
