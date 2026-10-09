/*
 * NewHorizonsAcademicStudy.h, part of VCMI engine
 * License: GNU General Public License v2 or later; see license.txt
 */
#pragma once
#include "../GameConstants.h"
class CGHeroInstance;
class CGTownInstance;

namespace newHorizonsLearning
{
/// Unrecorded first arrival only; read-only so authority and AI share admission.
DLL_LINKAGE TExpType academicStudyRawExperience(const CGHeroInstance & hero, const CGTownInstance & town);
DLL_LINKAGE TExpType academicStudyExperience(const CGHeroInstance & hero, const CGTownInstance & town);
}
