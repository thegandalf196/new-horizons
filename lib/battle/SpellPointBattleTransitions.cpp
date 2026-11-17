/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "SpellPointBattleTransitions.h"
namespace battle::spellPoints
{
void Payment::validate() const
{
	if(normal < 0 || permanentBuffer < 0 || temporaryBuffer < 0)
		throw std::invalid_argument("Negative battle Spell Point payment provenance");
}
void State::validate() const
{
	if(normalCapacity < 0 || temporaryBufferRemaining < 0
		|| temporaryBufferRemaining > pools.getBuffer())
		throw std::invalid_argument("Invalid battle Spell Point pools");
}
bool State::operator==(const State & other) const
{
	return pools.getNormal() == other.pools.getNormal() && pools.getBuffer() == other.pools.getBuffer()
		&& temporaryBufferRemaining == other.temporaryBufferRemaining
		&& normalCapacity == other.normalCapacity && twoPools == other.twoPools;
}
std::optional<Payment> spend(State & state, int64_t amount)
{
	state.validate();
	auto staged = state;
	if(!staged.pools.spend(amount)) return std::nullopt;
	const auto bufferSpent = state.pools.getBuffer() - staged.pools.getBuffer();
	const auto temporary = std::min(state.temporaryBufferRemaining, bufferSpent);
	const Payment payment{state.pools.getNormal() - staged.pools.getNormal(), bufferSpent - temporary, temporary};
	staged.temporaryBufferRemaining -= temporary;
	state = std::move(staged);
	return payment;
}
bool refund(State & state, Payment payment)
{
	state.validate();
	payment.validate();
	const int64_t maximum = state.normalLimit();
	const int64_t restoredBuffer = static_cast<int64_t>(payment.permanentBuffer) + payment.temporaryBuffer;
	const int64_t nextBuffer = static_cast<int64_t>(state.pools.getBuffer()) + restoredBuffer;
	const int64_t nextTemporary = static_cast<int64_t>(state.temporaryBufferRemaining) + payment.temporaryBuffer;
	if(static_cast<int64_t>(state.pools.getNormal()) + payment.normal > maximum
		|| nextBuffer > std::numeric_limits<int32_t>::max() || nextTemporary > nextBuffer
		|| nextTemporary > std::numeric_limits<int32_t>::max() || (!state.twoPools && restoredBuffer != 0))
		return false;
	auto staged = state;
	if(!staged.pools.restoreNormal(payment.normal, static_cast<int32_t>(maximum))
		|| !staged.pools.grantBuffer(static_cast<int32_t>(restoredBuffer))) return false;
	staged.temporaryBufferRemaining = static_cast<int32_t>(nextTemporary);
	state = std::move(staged);
	return true;
}
bool refundCharge(State & state, Payment original, Payment & refunded, Payment payment)
{
	original.validate(); refunded.validate(); payment.validate();
	const auto bounded = [](int32_t paid, int32_t prior, int32_t next)
		{ return prior <= paid && next <= paid - prior; };
	if(!bounded(original.normal, refunded.normal, payment.normal)
		|| !bounded(original.permanentBuffer, refunded.permanentBuffer, payment.permanentBuffer)
		|| !bounded(original.temporaryBuffer, refunded.temporaryBuffer, payment.temporaryBuffer)) return false;
	auto staged = state;
	if(!refund(staged, payment)) return false;
	refunded.normal += payment.normal;
	refunded.permanentBuffer += payment.permanentBuffer;
	refunded.temporaryBuffer += payment.temporaryBuffer;
	state = std::move(staged);
	return true;
}
}
