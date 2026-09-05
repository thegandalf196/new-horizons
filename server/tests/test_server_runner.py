#!/usr/bin/env python3
# Part of VCMI engine. Authors: listed in AUTHORS.
# License: GNU General Public License v2.0 or later; see license.txt.
"""Focused runner checks, without game assets or a GUI.

Default: source contract checks only.
Build owner: add --build-dir build/new-horizons-linux to compile/run the actual
ServerThreadRunner implementation against a deterministic fake server. This
must name an existing assigned build directory; it never configures CMake.
"""

import argparse
from pathlib import Path
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[2]
SOURCE = (ROOT / "client/ServerRunner.cpp").read_text()
SERVER = (ROOT / "server/CVCMIServer.cpp").read_text()


def body(source, signature):
    start = source.index("{", source.index(signature))
    end, depth = start + 1, 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start + 1:end - 1]


class SourceContract(unittest.TestCase):
    def test_readiness_failure_is_joined_before_rethrow(self):
        start = body(SOURCE, "ServerThreadRunner::start(")
        self.assertIn("promise = std::move(promise)", start)
        self.assertIn("promise.set_exception(std::current_exception());", start)
        self.assertIn("serverPort = ready.get();", start)
        self.assertIn("wait();\n\t\tserver.reset();\n\t\tthrow;", start)

    def test_cancel_does_not_mutate_server_state_from_client(self):
        shutdown = body(SOURCE, "ServerThreadRunner::shutdown(")
        self.assertIn("if(server)", shutdown)
        self.assertIn("server->stop();", shutdown)
        self.assertNotIn("setState", shutdown)
        self.assertEqual(body(SERVER, "CVCMIServer::stop(").strip(), "networkHandler->stop();")

    def test_join_and_destructor_cleanup(self):
        self.assertIn("if(threadRunLocalServer.joinable())", body(SOURCE, "ServerThreadRunner::wait("))
        self.assertIn("shutdown();\n\twait();", body(SOURCE, "ServerThreadRunner::~ServerThreadRunner("))

    def test_discovery_requires_explicit_listening(self):
        self.assertNotIn("startDiscoveryListener", body(SERVER, "CVCMIServer::CVCMIServer("))
        startup = body(SERVER, "CVCMIServer::startAcceptingIncomingConnections(")
        guarded = body(startup, "if (listenForConnections)")
        self.assertIn("startDiscoveryListener();", guarded)
        self.assertLess(guarded.index("networkServer->start(port)"), guarded.index("startDiscoveryListener();"))
        self.assertEqual(startup.count("startDiscoveryListener();"), 1)


# The harness compiles production runner bodies/header, not a rewritten model of
# their control flow. Only external collaborators are fakes. It does not prove
# full CVCMIServer/Asio/game integration, which belongs to the native build.
PRELUDE = r'''
#include <atomic>
#include <cassert>
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <future>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#define VCMI_MOBILE
namespace boost { struct noncopyable {}; }
struct StartInfo {};
struct INetworkClientListener {};
struct INetworkServer {};
struct INetworkHandler
{
    int internal = 0;
    void createInternalConnection(INetworkClientListener &, INetworkServer &) { ++internal; }
    void connectToRemote(INetworkClientListener &, const std::string &, uint16_t) { assert(false); }
};
struct Settings
{
    const Settings & operator[](const char *) const { return *this; }
    int Integer() const { return 0; }
    std::string String() const { return {}; }
} settings;
struct Logger
{
    template<typename... T> void trace(T...) {}
    template<typename... T> void debug(T...) {}
    template<typename... T> void info(T...) {}
} logger;
auto logNetwork = &logger;
void setThreadName(const char *) {}
struct PreparationFailure : std::runtime_error { using std::runtime_error::runtime_error; };
class CVCMIServer
{
    std::mutex mutex;
    std::condition_variable condition;
    bool stopped = false;
    INetworkServer network;
public:
    inline static std::atomic<int> live{0};
    inline static std::atomic<int> loops{0};
    inline static bool failConstruction = false;
    inline static bool failPrepare = false;
    std::shared_ptr<StartInfo> si;
    CVCMIServer(uint16_t, bool)
    {
        if(failConstruction) throw PreparationFailure("construct");
        ++live;
    }
    ~CVCMIServer() { --live; }
    uint16_t prepare(bool lobby, bool listen)
    {
        assert(!lobby && !listen);
        if(failPrepare) throw PreparationFailure("prepare");
        return 0;
    }
    void run()
    {
        ++loops;
        std::unique_lock lock(mutex);
        condition.wait(lock, [this]() { return stopped; });
        --loops;
    }
    void stop()
    {
        std::lock_guard lock(mutex);
        stopped = true;
        condition.notify_all();
    }
    INetworkServer & getNetworkServer() { return network; }
};
'''

MAIN = r'''
int main()
{
    {
        ServerThreadRunner runner;
        runner.shutdown();
        runner.wait();
        runner.wait();
    }
    for(bool construction : {true, false})
    {
        ServerThreadRunner runner;
        CVCMIServer::failConstruction = construction;
        CVCMIServer::failPrepare = !construction;
        bool caught = false;
        try { runner.start(false, false, {}); }
        catch(const PreparationFailure & error)
        {
            caught = true;
            assert(std::string(error.what()) == (construction ? "construct" : "prepare"));
        }
        assert(caught);
        assert(CVCMIServer::live == 0);
        runner.shutdown();
        runner.wait();
        CVCMIServer::failConstruction = CVCMIServer::failPrepare = false;
        runner.start(false, false, {});
        runner.shutdown();
        runner.wait();
    }
    // Repeated early cancellation, joined reuse, and duplicate-start rejection.
    {
        ServerThreadRunner runner;
        INetworkHandler network;
        INetworkClientListener listener;
        for(int i = 0; i < 100; ++i)
        {
            runner.start(false, false, {});
            bool rejected = false;
            try { runner.start(false, false, {}); }
            catch(const std::logic_error &) { rejected = true; }
            assert(rejected);
            runner.connect(network, listener);
            runner.shutdown();
            runner.shutdown();
            runner.wait();
            runner.wait();
            assert(runner.exitCode() == 0);
        }
        assert(network.internal == 100);
    }
    // An owner unwinding after successful start must not destroy a joinable thread.
    {
        ServerThreadRunner runner;
        runner.start(false, false, {});
    }
    assert(CVCMIServer::live == 0);
    assert(CVCMIServer::loops == 0);
}
'''


def run_harness(build_dir):
    build_dir = build_dir.resolve(strict=True)
    if not (build_dir / "CMakeCache.txt").is_file():
        raise RuntimeError("Use the existing assigned CMake build directory")
    header = (ROOT / "client/ServerRunner.h").read_text().split("#pragma once", 1)[1]
    implementation = SOURCE.split("ServerThreadRunner::ServerThreadRunner()", 1)[1]
    implementation = "ServerThreadRunner::ServerThreadRunner()" + implementation.split("#ifdef ENABLE_SERVER_PROCESS", 1)[0]
    executable = build_dir / "nh-server-runner-test"
    subprocess.run(["g++", "-std=c++20", "-pthread", "-Wall", "-Wextra", "-Werror",
                    "-x", "c++", "-", "-o", str(executable)],
                   input=PRELUDE + header + implementation + MAIN, text=True, check=True, timeout=60)
    subprocess.run([str(executable)], check=True, timeout=15)
    print("Production runner / fake-server lifecycle tests passed", flush=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path)
    args = parser.parse_args()
    result = unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(SourceContract))
    if not result.wasSuccessful():
        raise SystemExit(1)
    if args.build_dir:
        run_harness(args.build_dir)
