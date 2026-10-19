/* License: GNU General Public License v2.0 or later; see license.txt. */
#pragma once

#ifndef NH_CURE_SIMULATION_FIXTURE
#error Cure fixture belongs only to the dedicated integration object target
#endif

#include "CureFixtureState.h"
#include <memory>

class CGameHandler;
class CBattleInfoCallback;
class CStack;

namespace cureFixture
{
/// Non-owning, single attachment rendezvous. The driver owns Context through restart.
void registerContext(const std::shared_ptr<Context> & context);
const CStack * beforeHumanActivation(CGameHandler & handler, const CBattleInfoCallback & battle, const CStack & next);
}
