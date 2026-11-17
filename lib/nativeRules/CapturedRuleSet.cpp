/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "CapturedRuleSet.h"
#include "CapturedRulePool.h"
#include "CompiledMetamagicRules.h"
#include "../spells/NewHorizonsMagic.h"
#include "../entities/hero/NewHorizonsMasteryRules.h"
#include "../entities/hero/NewHorizonsHeroRules.h"
#include "../entities/hero/NewHorizonsCapabilityRules.h"
#include <stdexcept>

namespace nativeRules
{
std::shared_ptr<const CapturedRuleDocument> captureMagicRules(
	std::shared_ptr<const CapturedRuleDocument> owner, const JsonNode & raw)
{
	if(owner && owner->kind() == CapturedRuleKind::WorldMagic
		&& (&owner->document() == &raw || equalCapturedDocuments(owner->document(), raw)))
		return owner;
	return CapturedRuleDocument::compile(CapturedRuleKind::WorldMagic, raw);
}

CapturedRuleDocument::CapturedRuleDocument(CapturedRuleKind kind, JsonNode document)
	: type(kind), coldDocument(std::move(document))
{
	switch(type)
	{
	case CapturedRuleKind::Mastery:
		masteryRules = std::make_shared<const CompiledMasteryRules>(coldDocument);
		break;
	case CapturedRuleKind::Perks:
		perkRules = CapturedPerkRules::compile(std::move(coldDocument));
		break;
	case CapturedRuleKind::ResolvedCapability:
		capabilityRules = std::make_shared<const CompiledCapabilityRules>(coldDocument);
		break;
	case CapturedRuleKind::ResolvedDevelopment:
		developmentRules = std::make_shared<const CompiledDevelopmentRules>(coldDocument);
		break;
	case CapturedRuleKind::WorldCapability:
		capabilityRules = std::make_shared<const CompiledCapabilityRules>(coldDocument, CapabilityContext::World);
		break;
	case CapturedRuleKind::WorldDevelopment:
		developmentRules = std::make_shared<const CompiledDevelopmentRules>(coldDocument, DevelopmentContext::World);
		validatePilotCreatures(coldDocument);
		break;
	case CapturedRuleKind::WorldMagic:
		newHorizonsMagic::validateRules(coldDocument);
		magicEnergy = std::make_shared<const CompiledMetamagicEnergy>(coldDocument);
		break;
	default:
		throw std::runtime_error("Unknown captured rule document kind");
	}
}

CapturedRuleDocument::CapturedRuleDocument(std::shared_ptr<const CapturedPerkRules> perks)
	: type(CapturedRuleKind::Perks), perkRules(std::move(perks))
{
	if(!perkRules) throw std::runtime_error("Missing immutable perk catalogue");
}

std::shared_ptr<const CapturedRuleDocument> CapturedRuleDocument::wrapPerks(std::shared_ptr<const CapturedPerkRules> perks)
{
	return std::shared_ptr<const CapturedRuleDocument>(new CapturedRuleDocument(std::move(perks)));
}

std::shared_ptr<const CapturedRuleDocument> CapturedRuleDocument::compile(CapturedRuleKind kind, JsonNode document)
{
	return std::shared_ptr<const CapturedRuleDocument>(new CapturedRuleDocument(kind, std::move(document)));
}
}
