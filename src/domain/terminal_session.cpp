#define _XOPEN_SOURCE 600

#include "domain/terminal_session.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <clocale>
#include <cstdlib>
#include <cstring>

#include <fcntl.h>
#include <pwd.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

namespace {

std::string executable_name(const std::string &path)
{
    const auto slash = path.find_last_of('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

}  // namespace

TerminalSession::TerminalSession()
{
    const char *locale = std::setlocale(LC_CTYPE, "");
    if (!locale || std::strcmp(locale, "C") == 0 || std::strcmp(locale, "POSIX") == 0) {
        if (!std::setlocale(LC_CTYPE, "C.UTF-8")) (void)std::setlocale(LC_CTYPE, "en_US.UTF-8");
    }
}

TerminalSession::~TerminalSession()
{
    stop();
}

bool TerminalSession::start()
{
    stop();
    const std::string shell      = configured_shell();
    const std::string home       = configured_home();
    const char *prompt_override  = std::getenv("LILYGO_UI_TERMINAL_PS1");
    const std::string prompt     = prompt_override ? prompt_override : "";
    const std::string shell_name = executable_name(shell);
    int master                   = posix_openpt(O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (master < 0) {
        std::lock_guard<std::mutex> lock(mutex_);
        status_ = std::string("PTY unavailable: ") + std::strerror(errno);
        append_notice_locked(status_);
        return false;
    }
    if (grantpt(master) != 0 || unlockpt(master) != 0) {
        const std::string error = std::strerror(errno);
        ::close(master);
        std::lock_guard<std::mutex> lock(mutex_);
        status_ = std::string("PTY setup failed: ") + error;
        append_notice_locked(status_);
        return false;
    }
    const char *slave_path_pointer = ptsname(master);
    if (!slave_path_pointer) {
        const std::string error = std::strerror(errno);
        ::close(master);
        std::lock_guard<std::mutex> lock(mutex_);
        status_ = std::string("PTY name failed: ") + error;
        append_notice_locked(status_);
        return false;
    }
    const std::string slave_path = slave_path_pointer;

    std::size_t row_count;
    std::size_t column_count;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        row_count    = emulator_.rows();
        column_count = emulator_.columns();
        status_      = "Starting shell";
        ++revision_;
    }

    const pid_t child = ::fork();
    if (child < 0) {
        const std::string error = std::strerror(errno);
        ::close(master);
        std::lock_guard<std::mutex> lock(mutex_);
        status_ = std::string("Could not start shell: ") + error;
        append_notice_locked(status_);
        return false;
    }

    if (child == 0) {
        (void)::setsid();
        const int slave = ::open(slave_path.c_str(), O_RDWR);
        if (slave < 0) _exit(127);

        struct winsize window_size{};
        window_size.ws_row = static_cast<unsigned short>(row_count);
        window_size.ws_col = static_cast<unsigned short>(column_count);
        (void)::ioctl(slave, TIOCSWINSZ, &window_size);
        (void)::ioctl(slave, TIOCSCTTY, 0);
        (void)::dup2(slave, STDIN_FILENO);
        (void)::dup2(slave, STDOUT_FILENO);
        (void)::dup2(slave, STDERR_FILENO);
        if (slave > STDERR_FILENO) ::close(slave);
        ::close(master);

        (void)::signal(SIGINT, SIG_DFL);
        (void)::signal(SIGTERM, SIG_DFL);
        (void)::signal(SIGQUIT, SIG_DFL);
        (void)::signal(SIGHUP, SIG_DFL);
        (void)::setenv("TERM", "ansi", 1);
        (void)::setenv("SHELL", shell.c_str(), 1);
        (void)::setenv("HOME", home.c_str(), 1);
        if (!prompt.empty()) (void)::setenv("PS1", prompt.c_str(), 1);
        if (::chdir(home.c_str()) != 0) (void)::chdir("/");

        const char *mode = prompt.empty() ? "-l" : "-i";
        ::execl(shell.c_str(), shell_name.c_str(), mode, static_cast<char *>(nullptr));
        _exit(127);
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        master_descriptor_ = master;
        child_process_     = static_cast<int>(child);
        status_            = "Shell ready";
        ++revision_;
    }
    stop_reader_.store(false);
    try {
        reader_ = std::thread(&TerminalSession::reader_loop, this, master);
    } catch (...) {
        stop();
        std::lock_guard<std::mutex> lock(mutex_);
        status_ = "Could not start terminal reader";
        append_notice_locked(status_);
        return false;
    }
    return true;
}

void TerminalSession::stop() noexcept
{
    int descriptor;
    int child;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        descriptor         = master_descriptor_;
        child              = child_process_;
        master_descriptor_ = -1;
        child_process_     = -1;
        stop_reader_.store(true);
    }

    if (child > 0) {
        (void)::kill(-child, SIGHUP);
        (void)::kill(child, SIGHUP);
    }
    if (descriptor >= 0) ::close(descriptor);
    if (reader_.joinable()) reader_.join();

    if (child > 0) {
        int child_status = 0;
        bool reaped      = false;
        for (int attempt = 0; attempt < 20; ++attempt) {
            const pid_t result = ::waitpid(child, &child_status, WNOHANG);
            if (result == child || (result < 0 && errno == ECHILD)) {
                reaped = true;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        if (!reaped) {
            (void)::kill(-child, SIGKILL);
            (void)::kill(child, SIGKILL);
            (void)::waitpid(child, &child_status, 0);
        }
    }

    if (descriptor >= 0 || child > 0) {
        std::lock_guard<std::mutex> lock(mutex_);
        status_ = "Shell stopped";
        ++revision_;
    }
}

bool TerminalSession::restart()
{
    stop();
    clear();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        append_notice_locked("[terminal] restarting shell");
    }
    return start();
}

bool TerminalSession::write(std::string_view bytes)
{
    if (bytes.empty()) return true;
    int descriptor;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        descriptor = master_descriptor_;
    }
    if (descriptor < 0) return false;
    std::lock_guard<std::mutex> write_lock(write_mutex_);
    const bool success = write_all(descriptor, bytes);
    if (!success) {
        std::lock_guard<std::mutex> lock(mutex_);
        status_ = "Input write failed";
        ++revision_;
    }
    return success;
}

void TerminalSession::clear()
{
    std::lock_guard<std::mutex> lock(mutex_);
    emulator_.clear();
    ++revision_;
}

void TerminalSession::resize(std::size_t rows, std::size_t columns)
{
    rows    = std::clamp(rows, minimum_rows, maximum_rows);
    columns = std::clamp(columns, minimum_columns, maximum_columns);

    int descriptor;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (rows == emulator_.rows() && columns == emulator_.columns()) return;
        if (!emulator_.resize(rows, columns)) return;
        descriptor = master_descriptor_;
        ++revision_;
    }
    if (descriptor >= 0) {
        struct winsize window_size{};
        window_size.ws_row = static_cast<unsigned short>(rows);
        window_size.ws_col = static_cast<unsigned short>(columns);
        (void)::ioctl(descriptor, TIOCSWINSZ, &window_size);
    }
}

TerminalSnapshot TerminalSession::snapshot()
{
    reap_child();
    std::lock_guard<std::mutex> lock(mutex_);
    TerminalSnapshot result;
    result.rows         = emulator_.screen_rows();
    result.status       = status_;
    result.row_count    = emulator_.rows();
    result.column_count = emulator_.columns();
    result.revision     = revision_;
    result.running      = master_descriptor_ >= 0 && child_process_ > 0;
    return result;
}

std::string TerminalSession::configured_shell()
{
    if (const char *override_shell = std::getenv("LILYGO_UI_TERMINAL_SHELL")) {
        if (*override_shell) return override_shell;
    }
    return "/bin/sh";
}

std::string TerminalSession::configured_home()
{
    if (const char *override_home = std::getenv("LILYGO_UI_TERMINAL_HOME")) {
        if (*override_home) return override_home;
    }
    if (const char *home = std::getenv("HOME")) {
        if (*home) return home;
    }
    if (const auto *entry = getpwuid(getuid())) {
        if (entry->pw_dir && *entry->pw_dir) return entry->pw_dir;
    }
    return "/";
}

bool TerminalSession::write_all(int descriptor, std::string_view bytes)
{
    std::size_t written = 0;
    while (written < bytes.size()) {
        const ssize_t count = ::write(descriptor, bytes.data() + written, bytes.size() - written);
        if (count > 0) {
            written += static_cast<std::size_t>(count);
            continue;
        }
        if (count < 0 && errno == EINTR) continue;
        if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            fd_set write_descriptors;
            FD_ZERO(&write_descriptors);
            FD_SET(descriptor, &write_descriptors);
            struct timeval timeout{0, 50000};
            if (::select(descriptor + 1, nullptr, &write_descriptors, nullptr, &timeout) >= 0) continue;
        }
        return false;
    }
    return true;
}

void TerminalSession::reader_loop(int descriptor)
{
    char buffer[1024];
    while (!stop_reader_.load()) {
        fd_set read_descriptors;
        FD_ZERO(&read_descriptors);
        FD_SET(descriptor, &read_descriptors);
        struct timeval timeout{0, 100000};
        const int ready = ::select(descriptor + 1, &read_descriptors, nullptr, nullptr, &timeout);
        if (ready < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (ready == 0 || !FD_ISSET(descriptor, &read_descriptors)) continue;

        const ssize_t count = ::read(descriptor, buffer, sizeof(buffer));
        if (count > 0) {
            std::string answer;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                emulator_.write(std::string_view(buffer, static_cast<std::size_t>(count)));
                answer = emulator_.take_answer();
                ++revision_;
            }
            if (!answer.empty()) {
                std::lock_guard<std::mutex> write_lock(write_mutex_);
                (void)write_all(descriptor, answer);
            }
        } else if (count == 0 || !(errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) {
            break;
        }
    }
}

void TerminalSession::reap_child()
{
    int child;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        child = child_process_;
    }
    if (child <= 0) return;

    int child_status   = 0;
    const pid_t result = ::waitpid(child, &child_status, WNOHANG);
    if (result != child) return;

    int descriptor = -1;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (child_process_ != child) return;
        descriptor         = master_descriptor_;
        master_descriptor_ = -1;
        child_process_     = -1;
        stop_reader_.store(true);
        status_ = WIFEXITED(child_status) ? "Shell exited" : "Shell terminated";
        append_notice_locked(std::string("[terminal] ") + status_);
    }
    if (descriptor >= 0) ::close(descriptor);
    if (reader_.joinable()) reader_.join();
}

void TerminalSession::append_notice_locked(std::string_view text)
{
    emulator_.write(text);
    emulator_.write("\r\n");
    ++revision_;
}
