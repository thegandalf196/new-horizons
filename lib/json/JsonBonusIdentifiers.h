/*
 * JsonBonusIdentifiers.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "../bonuses/BonusCustomTypes.h"
#include "../bonuses/BonusEnum.h"

class JsonNode;

/// Resolves the parts of a bonus json that name another entity: the subtype and the source instance.
/// While content loads, the name may belong to a mod that has not been read yet, so the answer can
/// arrive after the request returns. The assignment then runs from the identifier storage, which keeps
/// a copy of it until then - it has to own, or point into something that owns, whatever it writes to.
/// Never hand it a reference to a local of the requesting function.
namespace JsonBonusIdentifiers
{
using SubtypeAssignment = std::function<void(const BonusSubtypeID &)>;
using SourceAssignment = std::function<void(const BonusSourceID &)>;

/// Subtype of a bonus of `type`, as `node` names it. A null, malformed or unsupported subtype is
/// assigned the empty subtype right away, the last two with a warning.
void requestSubtype(BonusType type, const JsonNode & node, const SubtypeAssignment & assign);

/// Source instance of a bonus coming from `source`, as `node` names it. A null or malformed name is
/// assigned a custom source right away, a malformed one with a warning.
void requestSourceInstance(BonusSource source, const JsonNode & node, const SourceAssignment & assign);
}
