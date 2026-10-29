/* Part of VCMI / New Horizons. GPL v2.0 or later; see license.txt. */
#pragma once
#include <cstdint>
#include <vector>
struct PendingPetrifyingBlood
{
	struct Attempt { uint32_t recipient; uint32_t attacker; };
	int round;
	std::vector<Attempt> attempts;
};
/// Synchronous action-local ownership. Destruction only restores the binding;
/// resolution is explicit after a completed melee exchange, never on unwind.
class PetrifyingBloodSequence final
{
	PendingPetrifyingBlood *& binding;
	PendingPetrifyingBlood * previous;
	PendingPetrifyingBlood pending;
public:
	PetrifyingBloodSequence(PendingPetrifyingBlood *& binding, int round)
		: binding(binding), previous(binding), pending{round, {}}
	{
		binding = &pending;
	}
	PetrifyingBloodSequence(const PetrifyingBloodSequence &) = delete;
	PetrifyingBloodSequence & operator=(const PetrifyingBloodSequence &) = delete;
	~PetrifyingBloodSequence() { binding = previous; }
	const PendingPetrifyingBlood & result() const { return pending; }
};
