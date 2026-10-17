/*
 * NetworkWriteFailureInfo.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once

#include <boost/system/error_code.hpp>
#include <exception>
#include <cstddef>

/// Typed, nonformatting diagnostics. Observer and cleanup failures do not
/// overwrite the original accepted-publication transport failure.
struct DLL_LINKAGE NetworkWriteFailureInfo
{
	enum class Origin { NONE, INITIATION, COMPLETION, SYNCHRONOUS_DRAIN, INVALID_COMPLETION };
	Origin origin = Origin::NONE;
	boost::system::error_code error;
	std::exception_ptr cause;
	std::exception_ptr cleanupFailure;
	std::exception_ptr observationFailure;
	bool notificationAttempted = false;
	bool socketStillOpen = false;
};

struct DLL_LINKAGE NetworkWriteStatus
{
	bool terminal = false;
	bool inFlight = false;
	size_t frontOffset = 0;
	size_t queuedChunks = 0;
	NetworkWriteFailureInfo failure;
};
