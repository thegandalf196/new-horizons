/*
 * InitialExportLegacyMovement.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "InitialExportBattleSubject.h"
#include "HypotheticBattle.h"
#include "../../lib/battle/BattleInfo.h"
#include "../../lib/CStack.h"
#include <stdexcept>

namespace
{
struct Recipient
{
	InitialExportWorld::Source source;
	const battle::CUnitState * values;
	std::vector<recipientBonusOverlay::Layer> layers;
};

Recipient captureRecipient(const battle::Unit & unit, InitialExportBattleSubject & result)
{
	const auto source = result.world().sourceAt(result.occurrence(unit.unitId()));
	const auto * values = dynamic_cast<const battle::CUnitState *>(&unit);
	if(!values)
		throw std::invalid_argument("Legacy movement requires actual stack scalar values");
	std::vector<recipientBonusOverlay::Layer> layers;
	const CStack * native = nullptr;
	if(const auto * projected = dynamic_cast<const StackWithBonuses *>(&unit))
	{
		if(!projected->captureRecipientOverlayLayers(layers, native))
			throw std::invalid_argument("Legacy movement has no recoverable recipient layer chain");
	}
	else
		native = dynamic_cast<const CStack *>(&unit);
	if(native != source.original || !native || typeid(*native) != typeid(CStack))
		throw std::invalid_argument("Legacy recipient must retain its exact original native occurrence");
	return {source, values, std::move(layers)};
}
}

std::shared_ptr<InitialExportBattleSubject> InitialExportBattleSubject::captureLegacyMovement(
	Subject source, BattleInfo & native)
{
	if(!source || source->battleGetMySide() != BattleSide::ALL_KNOWING)
		throw std::invalid_argument("Legacy movement import requires an explicit full-information source");
	Subject root = source;
	std::set<const HypotheticBattle *> seen;
	while(const auto * projected = dynamic_cast<const HypotheticBattle *>(root.get()))
	{
		if(seen.size() >= 512 || !seen.insert(projected).second || projected->initialExportSubject())
			throw std::invalid_argument("Legacy movement source chain is not a finite legacy parent chain");
		root = projected->subject;
	}
	if(root.get() != static_cast<CBattleInfoCallback *>(&native) || root->getBattle() != &native)
		throw std::invalid_argument("Legacy movement requires the declared actual native root, not a guessed world");
	auto result = std::shared_ptr<InitialExportBattleSubject>(new InitialExportBattleSubject(
		source, std::make_unique<InitialExportWorld>(native)));
	const auto roster = source->battleGetUnitsIf([](const battle::Unit *) { return true; });
	if(roster.size() != result->world().size() || roster.size() > 256)
		throw std::invalid_argument("Legacy movement requires the complete original roster");
	std::vector<Recipient> recipients;
	std::set<uint32_t> ids;
	for(const auto * unit : roster)
	{
		if(!unit || !ids.insert(unit->unitId()).second)
			throw std::invalid_argument("Legacy recipient identities must be nonnull and unique");
		recipients.push_back(captureRecipient(*unit, *result));
	}
	// Install all query layers and copy ALL scalar/environment inputs before
	// saving any recipient; adjacency/HP/environment queries use this same graph.
	for(const auto & recipient : recipients)
		result->world().installRecipientOverlays(recipient.source, recipient.layers);
	const auto environmentValues = battle::ProjectedUnitEnvironmentState::capture(*source->getBattle());
	result->world().copyEnvironmentValues(environmentValues);
	for(const auto & recipient : recipients)
		result->world().copyQueryValues(recipient.source, *recipient.values);
	result->world().recordEnvironmentSnapshot(environmentValues);
	for(const auto & recipient : recipients)
		result->world().recordQuerySnapshot(recipient.source);
	return result;
}
