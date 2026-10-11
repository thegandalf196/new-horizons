/* CPU-only public release transport probe. GPL-2.0-or-later; see license.txt. */
#include "../mainmenu/ReleaseUpdateTransport.h"

#include <chrono>
#include <iostream>
#include <string_view>

int main(int argc, char ** argv)
{
	std::stop_source cancelled;
	cancelled.request_stop();
	const auto empty = releaseUpdates::fetchLatest(cancelled.get_token());
	if(empty.status != 0 || !empty.body.empty())
		return 1;
	std::cout << "PASS pre-cancelled request is silent\n";
	if(argc == 1)
		return 0;
	if(argc != 2 || std::string_view(argv[1]) != "--live")
		return 2;
	const auto start = std::chrono::steady_clock::now();
	const auto result = releaseUpdates::fetchLatest({});
	const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
		std::chrono::steady_clock::now() - start).count();
	// No response contents, headers, local paths or connection details are printed.
	std::cout << "Public endpoint status=" << result.status
		<< " bytes=" << result.body.size() << " elapsed_ms=" << elapsed << '\n';
	if(result.body.size() > 1024 * 1024 || elapsed > 6000)
		return 1;
	return 0; // status zero is explicitly an offline/error result, not live success.
}
