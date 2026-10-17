/*
 * NetworkLagRollbackCopy.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once
#include <memory>
struct CPackForClient;

/// Value copies of the current concrete rollback producer outputs only.
std::unique_ptr<CPackForClient> copyCurrentPredictionRollback(const CPackForClient & packet);
