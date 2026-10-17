/*
 * NetworkWriteAllocation.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once

#include <atomic>
#include <cstddef>
#include <limits>
#include <memory>
#include <new>
#include <utility>

/// Optional per-connection allocation resource control. It constrains REAL
/// Asio operation allocation, never decides packet validity or publication.
/// The default connection does not install it and retains its usual allocator.
class DLL_LINKAGE NetworkWriteAllocationControl final
{
	std::atomic<bool> refusing{false};
	std::atomic<size_t> requested{0};
	const size_t writeLimit;

public:
	explicit NetworkWriteAllocationControl(size_t maximumNativeWrite = std::numeric_limits<size_t>::max());
	size_t nativeWriteLimit() const noexcept;
	void refuse() noexcept;
	void allow() noexcept;
	size_t attempts() const noexcept;
	void beforeAllocate();
};

template<typename T> class NetworkWriteAllocator
{
public:
	using value_type = T;
	std::shared_ptr<NetworkWriteAllocationControl> control;

	NetworkWriteAllocator() = default;
	explicit NetworkWriteAllocator(std::shared_ptr<NetworkWriteAllocationControl> control)
		: control(std::move(control)) {}
	template<typename U> NetworkWriteAllocator(const NetworkWriteAllocator<U> & other)
		: control(other.control) {}
	T * allocate(size_t count)
	{
		if(control)
			control->beforeAllocate();
		return std::allocator<T>{}.allocate(count);
	}
	void deallocate(T * memory, size_t count) noexcept
	{
		std::allocator<T>{}.deallocate(memory, count);
	}
	template<typename U> bool operator==(const NetworkWriteAllocator<U> & other) const noexcept
	{
		return control == other.control;
	}
};

template<typename Handler> struct ControlledNetworkWriteHandler
{
	using allocator_type = NetworkWriteAllocator<std::byte>;
	Handler handler;
	std::shared_ptr<NetworkWriteAllocationControl> control;
	allocator_type get_allocator() const noexcept { return allocator_type(control); }
	template<typename... Args> void operator()(Args &&... args)
	{
		handler(std::forward<Args>(args)...);
	}
};
