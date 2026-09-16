#include "pages/terminal/terminal_view_model.hpp"

#include <cassert>
#include <filesystem>
#include <string>

#include <unistd.h>

int main()
{
    const auto settings = std::filesystem::temp_directory_path() /
                          ("lilygo-ui-terminal-vm-" + std::to_string(static_cast<long long>(getpid()))) /
                          "settings.conf";
    std::error_code error;
    std::filesystem::remove_all(settings.parent_path(), error);

    TerminalViewModel view_model(settings);
    auto state = view_model.state();
    assert(state.font_size == 18);
    assert(!state.ctrl_armed);

    view_model.set_geometry(10, 40);
    state = view_model.state();
    assert(state.row_count == 10);
    assert(state.column_count == 40);

    view_model.send_key(TerminalKey::ctrl);
    state = view_model.state();
    assert(state.ctrl_armed);
    assert(state.status == "Ctrl armed");
    view_model.send_text("c");
    assert(!view_model.state().ctrl_armed);

    view_model.send_key(TerminalKey::font_down);
    state = view_model.state();
    assert(state.font_size == 14);
    assert(state.preference_saved);

    TerminalModel restored(settings);
    assert(restored.font_size() == 14);
    std::filesystem::remove_all(settings.parent_path(), error);
    return 0;
}
