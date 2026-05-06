// openads_serverd — standalone TCP server CLI.
//
// Wraps openads::network::Server in a long-lived process. Parses
// --host / --port / --backlog from argv, prints the bound port,
// blocks on a signal-handled exit so a Harbour client (or any
// rddads app) can reach the server over LAN.

#include "network/server.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

namespace {

std::atomic<bool> g_running{true};

void on_signal(int) { g_running.store(false); }

void usage(const char* argv0) {
    std::fprintf(stderr,
        "usage: %s [--host HOST] [--port PORT] [--backlog N]\n"
        "  --host     bind address (default 0.0.0.0)\n"
        "  --port     TCP port (default 6262, 0 = ephemeral)\n"
        "  --backlog  listen() backlog (default 16)\n"
        "  --version  print version + exit\n",
        argv0);
}

} // namespace

int main(int argc, char** argv) {
    std::string host    = "0.0.0.0";
    std::uint16_t port  = 6262;
    int backlog         = 16;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if      (a == "--host"    && i + 1 < argc) host = argv[++i];
        else if (a == "--port"    && i + 1 < argc) port = static_cast<std::uint16_t>(std::atoi(argv[++i]));
        else if (a == "--backlog" && i + 1 < argc) backlog = std::atoi(argv[++i]);
        else if (a == "--version") {
            std::printf("openads_serverd 0.3.2\n");
            return 0;
        } else if (a == "-h" || a == "--help") {
            usage(argv[0]);
            return 0;
        } else {
            std::fprintf(stderr, "unknown arg: %s\n", a.c_str());
            usage(argv[0]);
            return 2;
        }
    }

    std::signal(SIGINT,  on_signal);
    std::signal(SIGTERM, on_signal);

    openads::network::Server srv;
    auto r = srv.start(host, port);
    if (!r) {
        std::fprintf(stderr, "server start failed: %s (sub=%d)\n",
                     r.error().message.c_str(), r.error().sub_code);
        return 1;
    }
    std::printf("openads_serverd listening on %s:%u (backlog=%d)\n",
                host.c_str(), srv.port(), backlog);
    std::fflush(stdout);

    while (g_running.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    std::printf("openads_serverd: shutdown signal received\n");
    srv.stop();
    return 0;
}
