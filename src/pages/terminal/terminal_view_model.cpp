#include "pages/terminal/terminal_view_model.hpp"

#include <utility>

TerminalViewModel::TerminalViewModel(std::filesystem::path settings_path) : model_(std::move(settings_path))
{
}

TerminalViewModel::~TerminalViewModel()
{
    stop();
}

bool TerminalViewModel::start()
{
    const bool started = session_.start();
    ++ui_revision_;
    return started;
}

void TerminalViewModel::stop() noexcept
{
    session_.stop();
    ++ui_revision_;
}

void TerminalViewModel::send_key(TerminalKey key)
{
    apply(model_.handle_key(key));
}

void TerminalViewModel::send_text(std::string_view text)
{
    apply(model_.handle_text(text));
}

void TerminalViewModel::set_geometry(std::size_t rows, std::size_t columns)
{
    session_.resize(rows, columns);
}

TerminalViewState TerminalViewModel::state()
{
    auto terminal = session_.snapshot();
    TerminalViewState result;
    result.rows             = std::move(terminal.rows);
    result.status           = model_.ctrl_armed() ? "Ctrl armed" : std::move(terminal.status);
    result.row_count        = terminal.row_count;
    result.column_count     = terminal.column_count;
    result.font_size        = model_.font_size();
    result.revision         = (terminal.revision << 16U) ^ ui_revision_;
    result.running          = terminal.running;
    result.ctrl_armed       = model_.ctrl_armed();
    result.preference_saved = model_.preference_saved();
    return result;
}

void TerminalViewModel::apply(TerminalCommand command)
{
    if (command.restart_shell) {
        (void)session_.restart();
    } else {
        if (!command.bytes.empty()) (void)session_.write(command.bytes);
        if (command.clear_screen) session_.clear();
    }
    ++ui_revision_;
}
