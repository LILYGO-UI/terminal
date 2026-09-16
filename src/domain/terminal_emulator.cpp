#include "domain/terminal_emulator.hpp"

#include <algorithm>
#include <stdexcept>

TerminalEmulator::TerminalEmulator(std::size_t rows, std::size_t columns)
    : terminal_(tmt_open(rows, columns, event_callback, this, nullptr))
{
    if (!terminal_) throw std::runtime_error("Could not initialize terminal emulator");
}

TerminalEmulator::~TerminalEmulator()
{
    if (terminal_) tmt_close(terminal_);
}

bool TerminalEmulator::valid() const noexcept
{
    return terminal_ != nullptr;
}

std::size_t TerminalEmulator::rows() const noexcept
{
    return terminal_ ? tmt_screen(terminal_)->nline : 0;
}

std::size_t TerminalEmulator::columns() const noexcept
{
    return terminal_ ? tmt_screen(terminal_)->ncol : 0;
}

std::vector<std::string> TerminalEmulator::screen_rows() const
{
    std::vector<std::string> result;
    if (!terminal_) return result;

    const auto *screen = tmt_screen(terminal_);
    const auto *cursor = tmt_cursor(terminal_);
    result.reserve(screen->nline);
    for (std::size_t row = 0; row < screen->nline; ++row) {
        std::string text;
        for (std::size_t column = 0; column < screen->ncol; ++column) {
            wchar_t character = screen->lines[row]->chars[column].c;
            if (cursor_visible_ && cursor && cursor->r == row && cursor->c == column) {
                character = character == L' ' || character == 0 ? L'_' : character;
            }
            append_utf8(text, character == 0 ? L' ' : character);
        }
        while (text.size() > 1 && text.back() == ' ') text.pop_back();
        if (text.empty()) text = " ";
        result.push_back(std::move(text));
    }
    return result;
}

std::string TerminalEmulator::take_answer()
{
    std::string result;
    result.swap(answer_);
    return result;
}

bool TerminalEmulator::resize(std::size_t rows, std::size_t columns)
{
    return terminal_ && tmt_resize(terminal_, rows, columns);
}

void TerminalEmulator::write(std::string_view bytes)
{
    if (terminal_ && !bytes.empty()) tmt_write(terminal_, bytes.data(), bytes.size());
}

void TerminalEmulator::clear()
{
    if (terminal_) tmt_reset(terminal_);
}

void TerminalEmulator::event_callback(tmt_msg_t message, TMT *, const void *argument, void *user_data)
{
    auto *emulator = static_cast<TerminalEmulator *>(user_data);
    if (!emulator) return;

    switch (message) {
        case TMT_MSG_ANSWER:
            if (argument) emulator->answer_ += static_cast<const char *>(argument);
            break;
        case TMT_MSG_CURSOR:
            if (argument) emulator->cursor_visible_ = static_cast<const char *>(argument)[0] != 'f';
            break;
        case TMT_MSG_MOVED:
        case TMT_MSG_UPDATE:
        case TMT_MSG_BELL:
            break;
    }
}

void TerminalEmulator::append_utf8(std::string &output, wchar_t character)
{
    const auto codepoint = static_cast<unsigned int>(character);
    if (codepoint <= 0x7f) {
        output.push_back(static_cast<char>(codepoint));
    } else if (codepoint <= 0x7ff) {
        output.push_back(static_cast<char>(0xc0 | (codepoint >> 6)));
        output.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
    } else if (codepoint <= 0xffff) {
        output.push_back(static_cast<char>(0xe0 | (codepoint >> 12)));
        output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
        output.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
    } else if (codepoint <= 0x10ffff) {
        output.push_back(static_cast<char>(0xf0 | (codepoint >> 18)));
        output.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3f)));
        output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
        output.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
    } else {
        output.push_back('?');
    }
}
