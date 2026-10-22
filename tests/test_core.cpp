#include "domain/terminal_emulator.hpp"
#include "domain/terminal_session.hpp"
#include "pages/terminal/terminal_model.hpp"

#include <cassert>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <thread>
#include <utility>

#include <unistd.h>

namespace {

class ScopedEnvironment {
public:
    ScopedEnvironment(const char *name, const char *value) : name_(name)
    {
        if (const char *previous = std::getenv(name)) {
            present_  = true;
            previous_ = previous;
        }
        if (value)
            assert(setenv(name, value, 1) == 0);
        else
            assert(unsetenv(name) == 0);
    }

    ~ScopedEnvironment()
    {
        if (present_)
            (void)setenv(name_.c_str(), previous_.c_str(), 1);
        else
            (void)unsetenv(name_.c_str());
    }

private:
    std::string name_;
    std::string previous_;
    bool present_ = false;
};

std::filesystem::path temporary_settings_path()
{
    return std::filesystem::temp_directory_path() /
           ("lilygo-ui-terminal-test-" + std::to_string(static_cast<long long>(getpid()))) / "settings.conf";
}

bool screen_contains(const TerminalSnapshot &snapshot, const std::string &needle)
{
    for (const auto &row : snapshot.rows) {
        if (row.find(needle) != std::string::npos) return true;
    }
    return false;
}

bool wait_for_output(TerminalSession &session, const std::string &expected)
{
    for (int attempt = 0; attempt < 100; ++attempt) {
        if (screen_contains(session.snapshot(), expected)) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return false;
}

std::string shell_quote(const std::string &text)
{
    std::string quoted = "'";
    for (const char character : text) {
        if (character == '\'')
            quoted += "'\\''";
        else
            quoted += character;
    }
    return quoted + "'";
}

void test_model_and_persistence()
{
    const auto settings = temporary_settings_path();
    std::error_code error;
    std::filesystem::remove_all(settings.parent_path(), error);

    TerminalModel model(settings);
    assert(model.font_level() == TerminalModel::default_font_level);
    assert(model.font_size() == 18);

    auto command = model.handle_key(TerminalKey::ctrl);
    assert(command.bytes.empty());
    assert(model.ctrl_armed());
    command = model.handle_key(TerminalKey::letter_c);
    assert(command.bytes == std::string(1, '\x03'));
    assert(!model.ctrl_armed());

    model.handle_key(TerminalKey::ctrl);
    command = model.handle_text("d");
    assert(command.bytes == std::string(1, '\x04'));
    assert(!model.ctrl_armed());

    command = model.handle_key(TerminalKey::arrow_left);
    assert(command.bytes == "\x1b[D");
    command = model.handle_key(TerminalKey::backtab);
    assert(command.bytes == "\x1b[Z");
    command = model.handle_key(TerminalKey::home);
    assert(command.bytes == "\x1b[H");
    command = model.handle_key(TerminalKey::end);
    assert(command.bytes == "\x1b[F");
    command = model.handle_key(TerminalKey::font_up);
    assert(command.font_changed);
    assert(model.font_size() == 22);
    assert(model.preference_saved());

    TerminalModel restored(settings);
    assert(restored.font_size() == 22);
    command = restored.handle_key(TerminalKey::font_up);
    assert(!command.font_changed);
    command = restored.handle_key(TerminalKey::restart);
    assert(command.restart_shell && command.clear_screen);

    std::filesystem::remove_all(settings.parent_path(), error);
}

void test_emulator()
{
    TerminalEmulator emulator(6, 24);
    emulator.write("hello\r\n\x1b[31mworld\x1b[0m");
    auto rows = emulator.screen_rows();
    assert(rows.size() == 6);
    assert(rows[0].find("hello") != std::string::npos);
    assert(rows[1].find("world") != std::string::npos);

    emulator.write("\x1b[2J\x1b[Hready");
    rows = emulator.screen_rows();
    assert(rows[0].find("ready") != std::string::npos);
    assert(rows[0].find("hello") == std::string::npos);
    assert(emulator.resize(8, 32));
    assert(emulator.rows() == 8);
    assert(emulator.columns() == 32);
}

void test_shell_round_trip()
{
    const ScopedEnvironment shell("LILYGO_UI_TERMINAL_SHELL", "/bin/sh");
    const ScopedEnvironment prompt("LILYGO_UI_TERMINAL_PS1", "test> ");
    const ScopedEnvironment home_override("LILYGO_UI_TERMINAL_HOME", nullptr);
    const ScopedEnvironment shell_startup("ENV", nullptr);
    const ScopedEnvironment bash_startup("BASH_ENV", nullptr);
    const ScopedEnvironment home("HOME", "/tmp");
    const ScopedEnvironment user("USER", "inherited-user");
    const ScopedEnvironment logname("LOGNAME", "inherited-logname");

    TerminalSession session;
    session.resize(24, 120);
    // Root services and ordinary callers both start without any login-session requirement.
    assert(session.start());
    // The expected marker is absent from the command, so PTY input echo cannot pass this test.
    assert(session.write("printf 'terminal-%s\\n' 'session-ok'\r"));
    assert(wait_for_output(session, "terminal-session-ok"));

    assert(
        session.write("printf 'effective-uid=<%s>\\n' \"$(id -u)\"; "
                      "printf 'effective-gid=<%s>\\n' \"$(id -g)\"; "
                      "printf 'real-uid=<%s>\\n' \"$(id -ru)\"; "
                      "printf 'real-gid=<%s>\\n' \"$(id -rg)\"\r"));
    assert(wait_for_output(session, "effective-uid=<" + std::to_string(geteuid()) + ">"));
    assert(wait_for_output(session, "effective-gid=<" + std::to_string(getegid()) + ">"));
    assert(wait_for_output(session, "real-uid=<" + std::to_string(getuid()) + ">"));
    assert(wait_for_output(session, "real-gid=<" + std::to_string(getgid()) + ">"));

    for (const auto &variable :
         {std::pair{"HOME", "/tmp"}, std::pair{"USER", "inherited-user"}, std::pair{"LOGNAME", "inherited-logname"}}) {
        const std::string command = "test \"$" + std::string(variable.first) + "\" = " + shell_quote(variable.second) +
                                    " && printf '" + variable.first + "-%s\\n' 'correct'\r";
        assert(session.write(command));
        assert(wait_for_output(session, std::string(variable.first) + "-correct"));
    }
    assert(session.restart());
    assert(session.write("printf 'restart-%s\\n' 'succeeded'\r"));
    assert(wait_for_output(session, "restart-succeeded"));
    session.stop();
    assert(!session.snapshot().running);
}

}  // namespace

int main()
{
    test_model_and_persistence();
    test_emulator();
    test_shell_round_trip();
    return 0;
}
