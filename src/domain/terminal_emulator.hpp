#ifndef LILYGO_UI_TERMINAL_DOMAIN_TERMINAL_EMULATOR_HPP
#define LILYGO_UI_TERMINAL_DOMAIN_TERMINAL_EMULATOR_HPP

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

extern "C" {
#include "tmt.h"
}

class TerminalEmulator {
public:
    TerminalEmulator(std::size_t rows, std::size_t columns);
    ~TerminalEmulator();

    TerminalEmulator(const TerminalEmulator &)            = delete;
    TerminalEmulator &operator=(const TerminalEmulator &) = delete;

    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] std::size_t rows() const noexcept;
    [[nodiscard]] std::size_t columns() const noexcept;
    [[nodiscard]] std::vector<std::string> screen_rows() const;
    [[nodiscard]] std::string take_answer();

    bool resize(std::size_t rows, std::size_t columns);
    void write(std::string_view bytes);
    void clear();

private:
    static void event_callback(tmt_msg_t message, TMT *terminal, const void *argument, void *user_data);
    static void append_utf8(std::string &output, wchar_t character);

    TMT *terminal_       = nullptr;
    bool cursor_visible_ = true;
    std::string answer_;
};

#endif
