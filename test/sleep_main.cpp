#include <chrono>
#include <iostream>
#include <print>
#include <thread>

#if defined(_WIN32) || defined(_WIN64)
#include <Windows.h>

std::atomic_flag g_signal_received = ATOMIC_FLAG_INIT;

BOOL CtrlHandler(DWORD fdwCtrlType) {
    if (fdwCtrlType == CTRL_C_EVENT) {
        std::println("sleep_main.exe: Ctrl+C received. Cleaning up and exiting.");
        g_signal_received.test_and_set();
        return TRUE;
    }
    return FALSE;
}

#else
#include <csignal>

volatile std::sig_atomic_t g_signal_received = false;

void signal_handler(int signal) {
    if (signal == SIGINT) {
        std::println("Ctrl+C received. Cleaning up and exiting.");
        g_signal_received.test_and_set();
    }
}

#endif

void sleep_seconds(double seconds) {
    auto duration = std::chrono::duration<double>(seconds);
    auto start = std::chrono::steady_clock::now();

    while (std::chrono::steady_clock::now() - start < duration) {
        if (g_signal_received.test()) {
            std::println("Breaking out of sleep due to Ctrl+C signal.");
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

int main(int argc, char** argv) {
#if defined(_WIN32) || defined(_WIN64)
    if (!SetConsoleCtrlHandler(reinterpret_cast<PHANDLER_ROUTINE>(CtrlHandler), TRUE))
#else
    if (std::signal(SIGINT, signal_handler) == SIG_ERR)
#endif
    {
        std::println(stderr, "Failed to set signal handler.");
        return 1;
    }

    if (argc != 2) {
        std::println(stderr, "Usage: {} <seconds>", argv[0]);
        return 1;
    }

    sleep_seconds(std::stod(argv[1]));
    return 0;
}
