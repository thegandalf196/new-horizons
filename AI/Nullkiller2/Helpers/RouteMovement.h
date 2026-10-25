/* RouteMovement.h, part of VCMI engine; GPL v2.0 or later. */
#pragma once

class int3;

namespace NK2AI
{
class AIGateway;
struct HeroPtr;

bool moveRouteActor(AIGateway & gateway, const int3 & destination, const HeroPtr & hero);
}
