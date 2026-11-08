/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "BattleAIDiagnosticAdmission.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/battle/HeroCommand.h"
#include "../../lib/spells/CSpellHandler.h"
#include <algorithm>
#include <stdexcept>
namespace battleAIDiagnostic
{
namespace
{
bool currentOrder(HeroCommand command)
{
	for(const auto value : {HeroCommand::CHARGE, HeroCommand::HOLD_THE_LINE,
		HeroCommand::FOCUS_FIRE, HeroCommand::RIPOSTE, HeroCommand::BRACE,
		HeroCommand::SECOND_WIND, HeroCommand::FEINT, HeroCommand::VANGUARD})
		if(command == value) return true;
	return false;
}
template<class T> void normalize(std::vector<T> & ids)
{
	std::sort(ids.begin(), ids.end());
	if(std::adjacent_find(ids.begin(), ids.end()) != ids.end())
		throw std::invalid_argument("DIAGNOSTIC admission contains duplicate IDs");
}
}
Mask::Mask(Spec value) : spec(std::move(value)) {}
std::shared_ptr<const Mask> Mask::prepare(Spec spec)
{
	if((spec.suppressAllOrders && !spec.orders.empty())
		|| (spec.suppressAllManualHeroSpells && !spec.manualHeroSpells.empty()))
		throw std::invalid_argument("DIAGNOSTIC admission mixes all and individual IDs");
	normalize(spec.orders);
	normalize(spec.manualHeroSpells);
	for(const auto id : spec.orders)
		if(!currentOrder(id)) throw std::invalid_argument("DIAGNOSTIC admission has unsupported Order ID");
	for(const auto id : spec.manualHeroSpells)
		if(id.getNum() < 0 || !LIBRARY || !LIBRARY->spellh
			|| static_cast<size_t>(id.getNum()) >= LIBRARY->spellh->objects.size()
			|| !LIBRARY->spellh->objects.at(id.getNum()))
			throw std::invalid_argument("DIAGNOSTIC admission has unregistered spell ID");
	if(!spec.suppressAllOrders && !spec.suppressAllManualHeroSpells
		&& spec.orders.empty() && spec.manualHeroSpells.empty()) return {};
	return std::shared_ptr<const Mask>(new Mask(std::move(spec)));
}
bool Mask::suppresses(HeroCommand command) const
{
	return spec.suppressAllOrders || std::binary_search(spec.orders.begin(), spec.orders.end(), command);
}
bool Mask::suppresses(SpellID spell) const
{
	return spec.suppressAllManualHeroSpells
		|| std::binary_search(spec.manualHeroSpells.begin(), spec.manualHeroSpells.end(), spell);
}
void Mask::requireSupportedContext(bool mandatoryOrder, bool pendingSpellFollowup) const
{
	if((mandatoryOrder && (spec.suppressAllOrders || !spec.orders.empty()))
		|| (pendingSpellFollowup && (spec.suppressAllManualHeroSpells || !spec.manualHeroSpells.empty())))
		throw std::invalid_argument("DIAGNOSTIC admission refuses mandatory follow-up suppression");
}
bool Admission::permits(HeroCommand command) const
{
	++counts.orderKindsVisited;
	if(!mask.suppresses(command)) return true;
	++counts.orderKindsPruned;
	return false;
}
bool Admission::permits(SpellID spell) const
{
	++counts.spellRowsVisited;
	if(!mask.suppresses(spell)) return true;
	++counts.spellRowsPruned;
	return false;
}
void Admission::requireSupportedContext(bool mandatoryOrder, bool pendingSpellFollowup) const
{
	mask.requireSupportedContext(mandatoryOrder, pendingSpellFollowup);
}
}
