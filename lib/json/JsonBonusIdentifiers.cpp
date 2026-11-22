/*
 * JsonBonusIdentifiers.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */

#include "StdInc.h"
#include "JsonBonusIdentifiers.h"

#include "JsonNode.h"

#include "../GameLibrary.h"
#include "../CBonusTypeHandler.h"
#include "../modding/IdentifierStorage.h"

namespace
{
/// Asks for `node` as an entity of `entityType` and assigns it as an `Identifier`. The callback keeps
/// its own copy of the assignment, since it may run long after this call has returned.
template<typename Identifier>
void requestSource(const std::string & entityType, const JsonNode & node, const JsonBonusIdentifiers::SourceAssignment & assign)
{
	LIBRARY->identifiers()->requestIdentifier(entityType, node, [assign](int32_t identifier)
	{
		assign(Identifier(identifier));
	});
}
}

void JsonBonusIdentifiers::requestSubtype(BonusType type, const JsonNode & node, const SubtypeAssignment & assign)
{
	if (node.isNull())
	{
		assign(BonusSubtypeID());
		return;
	}

	if (node.isNumber()) // Compatibility code for 1.3 or older
	{
		logMod->warn("Bonus subtype must be string! (%s)", node.getModScope());
		assign(BonusCustomSubtype(node.Integer()));
		return;
	}

	if (!node.isString())
	{
		logMod->warn("Bonus subtype must be string! (%s)", node.getModScope());
		assign(BonusSubtypeID());
		return;
	}

	const auto entityType = bonusSubtypeEntityType(type);

	if(entityType == EntityTypeEnum::NONE)
	{
		logMod->warn("Bonus type %s does not supports subtypes!", LIBRARY->bth->bonusToString(type));
		assign(BonusSubtypeID());
		return;
	}

	// deferred, unlike the runtime decodeBonusSubtype: while content loads the identifier this
	// names may belong to a mod that has not been read yet
	LIBRARY->identifiers()->requestIdentifier(entityTypeName(entityType), node, [assign, entityType](int32_t identifier)
	{
		assign(bonusSubtypeOf(entityType, identifier));
	});
}

void JsonBonusIdentifiers::requestSourceInstance(BonusSource source, const JsonNode & node, const SourceAssignment & assign)
{
	if (node.isNull())
	{
		assign(BonusCustomSource());
		return;
	}

	if (node.isNumber()) // Compatibility code for 1.3 or older
	{
		logMod->warn("Bonus source must be string!");
		assign(BonusCustomSource(node.Integer()));
		return;
	}

	if (!node.isString())
	{
		logMod->warn("Bonus source must be string!");
		assign(BonusCustomSource());
		return;
	}

	switch (source)
	{
		case BonusSource::ARTIFACT:
		case BonusSource::ARTIFACT_INSTANCE:
			requestSource<ArtifactID>("artifact", node, assign);
			break;
		case BonusSource::OBJECT_TYPE:
			requestSource<Obj>("object", node, assign);
			break;
		case BonusSource::OBJECT_INSTANCE:
		case BonusSource::HERO_BASE_SKILL:
			assign(ObjectInstanceID(ObjectInstanceID::decode(node.String())));
			break;
		case BonusSource::CREATURE_ABILITY:
			requestSource<CreatureID>("creature", node, assign);
			break;
		case BonusSource::TERRAIN_OVERLAY:
			requestSource<BattleField>("battlefield", node, assign);
			break;
		case BonusSource::SPELL_EFFECT:
			requestSource<SpellID>("spell", node, assign);
			break;
		case BonusSource::TOWN_STRUCTURE:
			assert(0); // TODO
			assign(BuildingTypeUniqueID());
			break;
		case BonusSource::SECONDARY_SKILL:
			requestSource<SecondarySkill>("secondarySkill", node, assign);
			break;
		case BonusSource::HERO_SPECIAL:
			requestSource<HeroTypeID>("hero", node, assign);
			break;
		case BonusSource::CAMPAIGN_BONUS:
			assign(CampaignScenarioID(CampaignScenarioID::decode(node.String())));
			break;
		case BonusSource::ARMY:
		case BonusSource::STACK_EXPERIENCE:
		case BonusSource::COMMANDER:
		case BonusSource::GLOBAL:
		case BonusSource::TERRAIN_NATIVE:
		case BonusSource::OTHER:
		default:
			assign(BonusSourceID());
			break;
	}
}
