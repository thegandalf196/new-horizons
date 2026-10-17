/*
 * NativeBonusGraphAdmission.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "NativeBonusGraphAdmission.h"
#include <limits>
#include <stdexcept>

namespace
{
struct AdmissionState
{
	size_t ordinaryMutations = 0;
	const NativeBonusGraphPreparation * preparing = nullptr;
};

AdmissionState & admissionState()
{
	// Access ONLY while holding NativeBonusGraphAccess. No TLS, atomics or
	// independently ordered mutex: recursion cannot evade this shared phase.
	static AdmissionState state;
	return state;
}
}

NativeBonusGraphMutation::NativeBonusGraphMutation()
{
	auto & state = admissionState(); // access member already owns graph mutex
	if(state.preparing)
		throw std::logic_error("Prepared native graph forbids reentrant ordinary mutation");
	if(state.ordinaryMutations == std::numeric_limits<size_t>::max())
		throw std::overflow_error("Too many nested native mutation owners");
	++state.ordinaryMutations;
}

NativeBonusGraphMutation::~NativeBonusGraphMutation()
{
	--admissionState().ordinaryMutations; // graph member unlocks AFTER this body
}

NativeBonusGraphPreparation::NativeBonusGraphPreparation()
{
	auto & state = admissionState();
	if(state.preparing || state.ordinaryMutations)
		throw std::logic_error("Native preparation requires its own outer entry");
	state.preparing = this;
}

NativeBonusGraphPreparation::~NativeBonusGraphPreparation()
{
	admissionState().preparing = nullptr; // before graph unlock; no callback/query
}

void NativeBonusGraphPreparation::checkEntry()
{
	NativeBonusGraphAccess access;
	const auto & state = admissionState();
	if(state.preparing || state.ordinaryMutations)
		throw std::logic_error("Native preparation requires its own outer entry");
}

bool NativeBonusGraphPreparation::activeOnThisThread() const noexcept
{
	// Wrong-thread inspection does not touch phase state or acquire a mutex.
	return thread == std::this_thread::get_id() && admissionState().preparing == this;
}
