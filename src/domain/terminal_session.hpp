#ifndef LILYGO_UI_TERMINAL_DOMAIN_TERMINAL_SESSION_HPP
#define LILYGO_UI_TERMINAL_DOMAIN_TERMINAL_SESSION_HPP

#include "domain/terminal_emulator.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

struct TerminalSnapshot {
    std::vector<std::string> rows;
    std::string status;
    std::size_t row_count    = 0;
    std::size_t column_count = 0;
    std::uint64_t revision   = 0;
    bool running             = false;
};

class TerminalSession {
public:
    static constexpr std::size_t minimum_rows    = 4;
    static constexpr std::size_t maximum_rows    = 48;
    static constexpr std::size_t minimum_columns = 16;
    static constexpr std::size_t maximum_columns = 120;

    TerminalSession();
    ~TerminalSession();

    TerminalSession(const TerminalSession &)            = delete;
    TerminalSession &operator=(const TerminalSession &) = delete;

    bool start();
    void stop() noexcept;
    bool restart();
    bool write(std::string_view bytes);
    void clear();
    void resize(std::size_t rows, std::size_t columns);
    [[nodiscard]] TerminalSnapshot snapshot();

private:
    static std::string configured_shell();
    static std::string configured_home();
    static bool write_all(int descriptor, std::string_view bytes);

    void reader_loop(int descriptor);
    void reap_child();
    void append_notice_locked(std::string_view text);

    mutable std::mutex mutex_;
    std::mutex write_mutex_;
    TerminalEmulator emulator_{24, 48};
    std::thread reader_;
    std::atomic<bool> stop_reader_{false};
    int master_descriptor_  = -1;
    int child_process_      = -1;
    std::string status_     = "Shell stopped";
    std::uint64_t revision_ = 1;
};

#endif
