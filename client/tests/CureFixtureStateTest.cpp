/* License: GNU General Public License v2.0 or later; see license.txt. */
#include "../../server/tests/CureFixtureState.h"
#include <iostream>

namespace
{
void check(bool condition)
{
	if(!condition)
		throw std::runtime_error("Cure fixture state regression");
}

template<typename Operation> void refuses(Operation operation)
{
	bool rejected = false;
	try { operation(); }
	catch(const std::runtime_error &) { rejected = true; }
	check(rejected);
}

constexpr auto digest = "44b24ce7605c482609df466550e03e57528b59b5892be1cfc58fb6e71ecf4313";

void admission()
{
	cureFixture::Context absent(nullptr, nullptr, nullptr);
	check(!absent.enabled() && !absent.pendingFor(0, true));
	for(const char * value : {"", "1", "Ordinary44b24", "ordinary44b24 "})
		refuses([value]() { cureFixture::Context invalid(value, "ordinary44b24", "same-session"); });
	refuses([]() { cureFixture::Context invalid("ordinary44b24", nullptr, "same-session"); });
	refuses([]() { cureFixture::Context invalid("ordinary44b24", "ordinary44b24", "other"); });
	for(const char * mode : {"same-session", "late-session", "replaced-owner"})
	{
		cureFixture::Context context("ordinary44b24", "ordinary44b24", mode);
		context.authenticate(digest);
		check(!context.pendingFor(0, false)); // Real Ice activation is not setup.
		check(context.pendingFor(0, true));
		context.published(0, 17, 1);
		check(context.isReceipt(0, 17, 1) && !context.isReceipt(1, 17, 1));
		check(!context.pendingFor(0, true) && !context.pendingFor(1, true)); // Restart cannot rearm.
		refuses([&]() { context.published(0, 17, 1); });
	}
}

void failedSetupNeverRetries()
{
	cureFixture::Context unauthenticated("ordinary44b24", "ordinary44b24", "same-session");
	refuses([&]() { unauthenticated.pendingFor(0, true); });
	check(!unauthenticated.pendingFor(0, true));
	refuses([&]() { unauthenticated.authenticate(digest); });
	cureFixture::Context wrongBattle("ordinary44b24", "ordinary44b24", "same-session");
	wrongBattle.authenticate(digest);
	refuses([&]() { wrongBattle.pendingFor(1, true); });
	check(!wrongBattle.pendingFor(0, true));
	cureFixture::Context failedPublication("ordinary44b24", "ordinary44b24", "same-session");
	failedPublication.authenticate(digest);
	check(failedPublication.pendingFor(0, true));
	// Failure after consumption but before sendAndApply/receipt: no retry or fake PASS.
	check(!failedPublication.isReceipt(0, 17, 1) && !failedPublication.pendingFor(0, true));
	refuses([&]() { failedPublication.published(0, 17, 2); });
	refuses([&]() { failedPublication.published(0, 17, 1); });
	cureFixture::Context badDigest("ordinary44b24", "ordinary44b24", "same-session");
	refuses([&]() { badDigest.authenticate("wrong"); });
	refuses([&]() { badDigest.authenticate(digest); });
	check(!badDigest.pendingFor(0, true));
}
}

int main()
{
	admission();
	failedSetupNeverRetries();
	std::cout << "Cure fixture pure state checks passed\n";
}
