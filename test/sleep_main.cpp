#include <iostream>
#include <chrono>
#include <thread>

#if defined(_WIN32) || defined(_WIN64)
#include <Windows.h>

// Global flag to indicate whether Ctrl+C was pressed
std::atomic_flag g_signal_received = ATOMIC_FLAG_INIT;

BOOL CtrlHandler(DWORD fdwCtrlType)
{
    if (fdwCtrlType == CTRL_C_EVENT)
    {
        std::cout << "sleep_main.exe: Ctrl+C received. Cleaning up and exiting." << std::endl;
        // Perform cleanup or other actions if needed
        g_signal_received.test_and_set();
        return TRUE;
    }

    // Return FALSE for unhandled signals
    return FALSE;
}

#else
#include <csignal>

// Global flag to indicate whether Ctrl+C was pressed
volatile std::sig_atomic_t g_signal_received = false;

void signal_handler(int signal)
{
    if (signal == SIGINT)
    {
        std::cout << "Ctrl+C received. Cleaning up and exiting." << std::endl;
        // Perform cleanup or other actions if needed
        g_signal_received.test_and_set();
    }
}

#endif

void sleep_seconds(double seconds)
{
    std::chrono::duration<double> duration(seconds);
    auto start_time = std::chrono::steady_clock::now();

    while (std::chrono::steady_clock::now() - start_time < duration)
    {
        if (g_signal_received.test())
        {
            // If CTRL+C signal received, break out of sleep
            std::cout << "Breaking out of sleep due to Ctrl+C signal." << std::endl;
            return;
        }

        // Sleep for a short duration before checking the signal again
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}


int main(int argc, char** argv)
{
#if defined(_WIN32) || defined(_WIN64)
    // Set the custom console control handler for Ctrl+C on Windows
    if (SetConsoleCtrlHandler((PHANDLER_ROUTINE)CtrlHandler, TRUE))
#else
    // Set the custom signal handler for Ctrl+C on Unix-like systems
    if (std::signal(SIGINT, signal_handler) != SIG_ERR)
#endif
    {
        if (argc != 2)
        {
            std::cerr << "Usage: " << argv[0] << " <seconds>" << std::endl;
            return 1;
        }

        double seconds = std::stod(argv[1]);
        sleep_seconds(seconds);

        if (g_signal_received.test())
        {
            // Perform any additional cleanup if Ctrl+C was received
        }

        return 0;
    }
    else
    {
        std::cerr << "Failed to set signal handler." << std::endl;
        return 1;
    }
}
