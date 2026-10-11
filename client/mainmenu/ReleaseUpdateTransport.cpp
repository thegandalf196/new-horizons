/* New Horizons public release transport. GPL-2.0-or-later; see license.txt. */
#include "ReleaseUpdateTransport.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <exception>
#include <initializer_list>
#include <memory>
#include <mutex>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winhttp.h>
#elif defined(__linux__) && !defined(__ANDROID__)
#include <curl/curl.h>
#endif

namespace releaseUpdates
{
namespace
{
using Clock = std::chrono::steady_clock;
constexpr size_t MAX_RESPONSE = 1024 * 1024;
constexpr auto TIMEOUT = std::chrono::seconds(5);

#ifdef _WIN32
struct InternetHandle
{
	HINTERNET value = nullptr;
	explicit InternetHandle(HINTERNET value) : value(value) {}
	~InternetHandle()
	{
		if(value)
			WinHttpCloseHandle(value);
	}
	InternetHandle(const InternetHandle &) = delete;
	InternetHandle & operator=(const InternetHandle &) = delete;
};

struct RequestState
{
	std::mutex mutex;
	std::condition_variable changed;
	DWORD notification = 0;
	DWORD length = 0;
	std::atomic<bool> failed{false};
	std::array<char, 16384> buffer{};
};

// The final HANDLE_CLOSING callback owns this reference. Request buffers
// remain alive even if cancellation returns before WinHTTP finishes closing.
struct CallbackContext
{
	std::shared_ptr<RequestState> state;
};

void CALLBACK onStatus(HINTERNET, DWORD_PTR rawContext, DWORD notification, void * information, DWORD length) noexcept
{
	if(!rawContext)
		return;
	auto * context = reinterpret_cast<CallbackContext *>(rawContext);
	if(notification == WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING)
	{
		delete context;
		return;
	}
	if(notification != WINHTTP_CALLBACK_STATUS_REQUEST_ERROR
		&& notification != WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE
		&& notification != WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE
		&& notification != WINHTTP_CALLBACK_STATUS_DATA_AVAILABLE
		&& notification != WINHTTP_CALLBACK_STATUS_READ_COMPLETE)
		return;
	const auto state = context->state;
	try
	{
		std::lock_guard lock(state->mutex);
		if(notification == WINHTTP_CALLBACK_STATUS_REQUEST_ERROR)
			state->failed = true;
		else
		{
			state->notification = notification;
			if(notification == WINHTTP_CALLBACK_STATUS_DATA_AVAILABLE)
			{
				if(!information || length != sizeof(DWORD))
					state->failed = true;
				else
					std::memcpy(&state->length, information, sizeof(DWORD));
			}
			if(notification == WINHTTP_CALLBACK_STATUS_READ_COMPLETE)
				state->length = length;
		}
	}
	catch(...)
	{
		// Never unwind C++ exceptions through the WinHTTP callback ABI.
		// Atomic failure remains publishable even if mutex acquisition failed.
		state->failed.store(true);
	}
	state->changed.notify_one();
}

bool waitFor(const std::shared_ptr<RequestState> & state, DWORD expected, Clock::time_point deadline, std::stop_token stop)
{
	std::unique_lock lock(state->mutex);
	while(!stop.stop_requested() && Clock::now() < deadline && !state->failed)
	{
		if(state->notification == expected)
		{
			state->notification = 0;
			return true;
		}
		state->changed.wait_until(lock, std::min(deadline, Clock::now() + std::chrono::milliseconds(50)));
	}
	return false;
}

Response fetchWindows(std::stop_token stop, Clock::time_point deadline)
{
	// Direct public request: no proxy discovery, ambient credentials or cookies.
	InternetHandle session(WinHttpOpen(L"New-Horizons-Update-Check", WINHTTP_ACCESS_TYPE_NO_PROXY,
		WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, WINHTTP_FLAG_ASYNC));
	if(!session.value || !WinHttpSetTimeouts(session.value, 5000, 5000, 5000, 5000))
		return {};
	InternetHandle connection(WinHttpConnect(session.value, L"api.github.com", INTERNET_DEFAULT_HTTPS_PORT, 0));
	if(!connection.value)
		return {};
	InternetHandle request(WinHttpOpenRequest(connection.value, L"GET",
		L"/repos/thegandalf196/new-horizons/releases/latest", nullptr,
		WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
	if(!request.value)
		return {};
	DWORD redirects = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
	DWORD disabled = WINHTTP_DISABLE_COOKIES | WINHTTP_DISABLE_AUTHENTICATION;
	if(!WinHttpSetOption(request.value, WINHTTP_OPTION_REDIRECT_POLICY, &redirects, sizeof(redirects))
		|| !WinHttpSetOption(request.value, WINHTTP_OPTION_DISABLE_FEATURE, &disabled, sizeof(disabled)))
		return {};
	const auto state = std::make_shared<RequestState>();
	auto context = std::make_unique<CallbackContext>();
	context->state = state;
	DWORD_PTR rawContext = reinterpret_cast<DWORD_PTR>(context.get());
	if(!WinHttpSetOption(request.value, WINHTTP_OPTION_CONTEXT_VALUE, &rawContext, sizeof(rawContext)))
		return {};
	if(WinHttpSetStatusCallback(request.value, onStatus,
		WINHTTP_CALLBACK_FLAG_ALL_COMPLETIONS | WINHTTP_CALLBACK_FLAG_HANDLES, 0) == WINHTTP_INVALID_STATUS_CALLBACK)
		return {};
	context.release(); // Closing request owns callback-context destruction now.
	if(stop.stop_requested() || Clock::now() >= deadline)
		return {};
	constexpr auto headers = L"Accept: application/vnd.github+json\r\nX-GitHub-Api-Version: 2022-11-28\r\n";
	if(!WinHttpSendRequest(request.value, headers, static_cast<DWORD>(-1), WINHTTP_NO_REQUEST_DATA,
		0, 0, rawContext) || !waitFor(state, WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE, deadline, stop))
		return {};
	if(!WinHttpReceiveResponse(request.value, nullptr)
		|| !waitFor(state, WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE, deadline, stop))
		return {};
	DWORD status = 0;
	DWORD statusSize = sizeof(status);
	if(!WinHttpQueryHeaders(request.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
		WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX))
		return {};
	if(status != 200)
		return {static_cast<int>(status), {}};
	Response response{200, {}};
	for(;;)
	{
		if(!WinHttpQueryDataAvailable(request.value, nullptr)
			|| !waitFor(state, WINHTTP_CALLBACK_STATUS_DATA_AVAILABLE, deadline, stop))
			return {};
		const DWORD available = state->length;
		if(!available)
			return response;
		if(available > MAX_RESPONSE - response.body.size())
			return {};
		const DWORD chunk = std::min<DWORD>(available, static_cast<DWORD>(state->buffer.size()));
		if(!WinHttpReadData(request.value, state->buffer.data(), chunk, nullptr)
			|| !waitFor(state, WINHTTP_CALLBACK_STATUS_READ_COMPLETE, deadline, stop))
			return {};
		if(state->length > chunk || state->length == 0)
			return {};
		response.body.append(state->buffer.data(), state->length);
	}
}
#elif defined(__linux__) && !defined(__ANDROID__)
struct CurlRequest
{
	std::stop_token stop;
	Clock::time_point deadline;
	std::string body;
};

struct CurlHeaders
{
	curl_slist * value = nullptr;
	~CurlHeaders()
	{
		curl_slist_free_all(value);
	}
};

size_t receiveBytes(char * bytes, size_t size, size_t count, void * context) noexcept
{
	auto & request = *static_cast<CurlRequest *>(context);
	if(request.stop.stop_requested() || Clock::now() >= request.deadline
		|| (size && count > MAX_RESPONSE / size))
		return 0;
	const size_t length = size * count;
	if(length > MAX_RESPONSE - request.body.size())
		return 0;
	try
	{
		request.body.append(bytes, length);
		return length;
	}
	catch(...)
	{
		return 0;
	}
}

int transferProgress(void * context, curl_off_t, curl_off_t, curl_off_t, curl_off_t) noexcept
{
	const auto & request = *static_cast<CurlRequest *>(context);
	return request.stop.stop_requested() || Clock::now() >= request.deadline ? 1 : 0;
}

Response fetchLinux(std::stop_token stop, Clock::time_point deadline)
{
	static const auto initialized = curl_global_init(CURL_GLOBAL_DEFAULT);
	if(initialized != CURLE_OK)
		return {};
	const auto * features = curl_version_info(CURLVERSION_NOW);
	// NOSIGNAL cannot bound synchronous DNS. Fail closed on that provider
	// rather than silently promise a five-second deadline it cannot enforce.
	if(!features || !(features->features & CURL_VERSION_ASYNCHDNS))
		return {};
	std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> handle(curl_easy_init(), curl_easy_cleanup);
	if(!handle)
		return {};
	CurlHeaders headers;
	for(const auto * header : {"Accept: application/vnd.github+json", "X-GitHub-Api-Version: 2022-11-28"})
	{
		auto * appended = curl_slist_append(headers.value, header);
		if(!appended)
			return {};
		headers.value = appended;
	}
	CurlRequest request{stop, deadline, {}};
	const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count();
	if(stop.stop_requested() || remaining <= 0)
		return {};
	bool configured = true;
	const auto option = [&](CURLoption key, auto value)
	{
		configured &= curl_easy_setopt(handle.get(), key, value) == CURLE_OK;
	};
	option(CURLOPT_URL, "https://api.github.com/repos/thegandalf196/new-horizons/releases/latest");
	option(CURLOPT_USERAGENT, "New-Horizons-Update-Check");
	option(CURLOPT_HTTPHEADER, headers.value);
	option(CURLOPT_TIMEOUT_MS, static_cast<long>(remaining));
	option(CURLOPT_CONNECTTIMEOUT_MS, static_cast<long>(remaining));
	option(CURLOPT_NOSIGNAL, 1L);
	option(CURLOPT_SSL_VERIFYPEER, 1L);
	option(CURLOPT_SSL_VERIFYHOST, 2L);
	option(CURLOPT_FOLLOWLOCATION, 0L);
	option(CURLOPT_NETRC, static_cast<long>(CURL_NETRC_IGNORED));
	option(CURLOPT_PROXY, ""); // Match WinHTTP's direct, unauthenticated policy.
	option(CURLOPT_HTTPAUTH, static_cast<long>(CURLAUTH_NONE));
	option(CURLOPT_VERBOSE, 0L);
#if LIBCURL_VERSION_NUM >= 0x075500
	option(CURLOPT_PROTOCOLS_STR, "https");
#else
	option(CURLOPT_PROTOCOLS, static_cast<long>(CURLPROTO_HTTPS));
#endif
	option(CURLOPT_WRITEFUNCTION, receiveBytes);
	option(CURLOPT_WRITEDATA, &request);
	option(CURLOPT_NOPROGRESS, 0L);
	option(CURLOPT_XFERINFOFUNCTION, transferProgress);
	option(CURLOPT_XFERINFODATA, &request);
	if(!configured || curl_easy_perform(handle.get()) != CURLE_OK
		|| stop.stop_requested() || Clock::now() >= deadline)
		return {};
	long status = 0;
	if(curl_easy_getinfo(handle.get(), CURLINFO_RESPONSE_CODE, &status) != CURLE_OK)
		return {};
	return {static_cast<int>(status), std::move(request.body)};
}
#endif
}

Response fetchLatest(std::stop_token stop)
{
	if(stop.stop_requested())
		return {};
	const auto deadline = Clock::now() + TIMEOUT;
	try
	{
#ifdef _WIN32
		return fetchWindows(stop, deadline);
#elif defined(__linux__) && !defined(__ANDROID__)
		return fetchLinux(stop, deadline);
#else
		return {};
#endif
	}
	catch(const std::exception &)
	{
		return {};
	}
}
}
