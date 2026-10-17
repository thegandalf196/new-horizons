/*
 * NetworkWriteAllocation.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "NetworkWriteAllocation.h"

NetworkWriteAllocationControl::NetworkWriteAllocationControl(size_t maximumNativeWrite)
	: writeLimit(maximumNativeWrite)
{
	if(!writeLimit)
		throw std::invalid_argument("Native network write span must be nonzero");
}

size_t NetworkWriteAllocationControl::nativeWriteLimit() const noexcept
{
	return writeLimit;
}

void NetworkWriteAllocationControl::refuse() noexcept
{
	refusing.store(true);
}

void NetworkWriteAllocationControl::allow() noexcept
{
	refusing.store(false);
}

size_t NetworkWriteAllocationControl::attempts() const noexcept
{
	return requested.load();
}

void NetworkWriteAllocationControl::beforeAllocate()
{
	requested.fetch_add(1);
	if(refusing.load())
		throw std::bad_alloc();
}
