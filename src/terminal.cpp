#include "terminal.hpp"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#ifndef _WIN32
#include <csignal>
#include <sys/ioctl.h>
#include <unistd.h>
#else
#define NOMINMAX
#include <windows.h>
#endif
#ifdef CLICALC_READLINE
#include <readline/readline.h>
#include <readline/history.h>
#endif

namespace clicalc::cli {
#ifdef CLICALC_READLINE
namespace {
Engine* current_engine = nullptr;
std::string previous_line, preview_text;
int screen_rows = 0, screen_columns = 0;
bool reserved_preview = false;
#ifndef _WIN32
volatile std::sig_atomic_t screen_reserved = 0;
using SignalHandler = void (*)(int);
constexpr int exit_signals[] = {SIGINT, SIGTERM, SIGHUP, SIGQUIT};
SignalHandler previous_handlers[4]{};
void restore_on_signal(int number) {
    if (screen_reserved) {
        // Use only async-signal-safe operations; Readline restores terminal modes
        // before forwarding these signals to the application's handler.
        constexpr char reset[] = "\0337\033[r\033[999999;1H\033[2K\0338";
        auto ignored = ::write(STDOUT_FILENO,reset,sizeof(reset)-1); (void)ignored;
    }
    std::signal(number,SIG_DFL);
    ::kill(::getpid(),number);
}
#endif
void paint_preview(const std::string& text) {
    if (!reserved_preview) return;
    auto displayed = text;
    const auto available = static_cast<size_t>(std::max(0,screen_columns-1));
    if (displayed.size() > available) {
        displayed.resize(available);
        if (available >= 3) displayed.replace(available-3,3,"...");
    }
    // No newline and no last-column write: painting can never scroll or wrap.
    std::cout << "\0337\033[" << screen_rows << ";1H\033[2K" << displayed << "\0338" << std::flush;
}
bool configure_screen() {
    int rows = 24, columns = 80;
#ifndef _WIN32
    winsize size{};
    if (::ioctl(STDOUT_FILENO,TIOCGWINSZ,&size) == 0) {
        if (size.ws_row) rows = size.ws_row;
        if (size.ws_col) columns = size.ws_col;
    }
#else
    CONSOLE_SCREEN_BUFFER_INFO info{};
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE),&info)) {
        rows = info.srWindow.Bottom-info.srWindow.Top+1;
        columns = info.srWindow.Right-info.srWindow.Left+1;
    }
#endif
    bool reserve = current_engine->settings().preview && rows >= 3 && columns >= 4;
    if (rows == screen_rows && columns == screen_columns && reserve == reserved_preview) return false;
    // Keep the full-screen scroll region when previews are disabled. Otherwise
    // reserve the last physical row even when there is no complete expression.
    bool was_reserved = reserved_preview;
    if (was_reserved && rows > screen_rows) {
        // A growing terminal exposes the old footer inside the transcript area.
        std::cout << "\0337\033[" << screen_rows << ";1H\033[2K\0338";
    }
    screen_rows = rows; screen_columns = columns; reserved_preview = reserve;
#ifndef _WIN32
    screen_reserved = reserve;
#endif
    if (reserve) {
        std::cout << "\033[1;" << rows-1 << "r\033[" << rows-1 << ";1H";
        if (!was_reserved) std::cout << '\n';
        std::cout << "\033[2K";
    } else if (was_reserved) {
        std::cout << "\0337\033[r\033[" << rows << ";1H\033[2K\0338";
    }
    rl_set_screen_size(reserve ? rows-1 : rows,columns);
    preview_text.clear(); paint_preview("");
    std::cout << std::flush;
    return true;
}
std::vector<Completion> candidates;
size_t completion_index = 0;
int insert_operator(int count, int key) {
    auto whitespace = [](unsigned char c) { return std::isspace(c); };
    bool empty = std::all_of(rl_line_buffer,rl_line_buffer + rl_end,whitespace);
    bool second_minus = key == '-' && rl_point == rl_end && rl_end > 0 &&
        rl_line_buffer[rl_end-1] == '-' &&
        std::all_of(rl_line_buffer,rl_line_buffer + rl_end-1,whitespace);
    if (count > 0 && ((key != '-' && empty) || second_minus)) {
        rl_replace_line("ans ", 0); rl_point = rl_end;
    }
    return rl_insert(count, key);
}
char* next_completion(const char*, int state) {
    if (!state) completion_index = 0;
    if (completion_index == candidates.size()) return nullptr;
    const auto& text = candidates[completion_index++].text;
    auto* result = static_cast<char*>(std::malloc(text.size()+1));
    if (result) std::memcpy(result,text.c_str(),text.size()+1);
    return result;
}
char** complete(const char* text, int, int end) {
    rl_attempted_completion_over = 1; // Never fall back to shell filename completion.
    try {
        candidates = current_engine->complete(rl_line_buffer,static_cast<size_t>(end)).candidates;
        rl_completion_append_character = ' ';
        if (candidates.size() == 1 && candidates.front().kind == ItemKind::function)
            rl_completion_append_character = '(';
        return rl_completion_matches(text,next_completion);
    } catch (...) { return nullptr; } // No exceptions across a C callback boundary.
}
int update_preview() {
    try {
        if (configure_screen()) {
            previous_line.clear();
            rl_on_new_line(); rl_forced_update_display();
        }
        if (rl_readline_state & (RL_STATE_ISEARCH | RL_STATE_NSEARCH | RL_STATE_COMPLETING)) return 0;
        std::string line = rl_line_buffer;
        if (line == previous_line) return 0;
        previous_line = line; std::string text;
        if (reserved_preview && !line.empty()) {
            auto preview = current_engine->preview(line);
            if (!preview.error && !preview.items.empty()) {
                text = "= "+preview.items.back().formatted;
            }
        }
        if (text != preview_text) { preview_text = text; paint_preview(text); }
    } catch (...) { /* An unavailable preview never prevents editing. */ }
    return 0;
}
int clear_editor(int, int) {
    try {
        clear_screen();
        rl_on_new_line(); rl_forced_update_display();
        paint_preview(preview_text);
    } catch (...) {}
    return 0;
}
}
#endif
void clear_screen() {
#ifdef CLICALC_READLINE
    if (current_engine) configure_screen();
    std::cout << "\033[2J\033[" << (reserved_preview ? screen_rows-1 : 1) << ";1H" << std::flush;
#else
    std::cout << "\033[2J\033[H" << std::flush;
#endif
}
Terminal::Terminal(Engine& engine, std::filesystem::path history) : history_(std::move(history)) {
#ifdef CLICALC_READLINE
    current_engine = &engine;
    rl_readline_name = "clicalc"; rl_initialize();
    for (char key : std::string("+-/*")) rl_bind_key(key,insert_operator);
    rl_bind_keyseq("\033[A",rl_get_previous_history);
    rl_bind_keyseq("\033[B",rl_get_next_history);
    rl_bind_keyseq("\033OA",rl_get_previous_history);
    rl_bind_keyseq("\033OB",rl_get_next_history);
    rl_bind_key(18,rl_reverse_search_history); // Ctrl-R, regardless of inputrc.
    rl_bind_key(12,clear_editor); // Ctrl-L clears the same screen area as cls.
    rl_bind_key('\t',rl_complete);
    rl_completer_word_break_characters = " \t\n\"'`@$><=;|&{(+*/-%^!,)[]";
    rl_attempted_completion_function = complete;
    rl_event_hook = update_preview;
    rl_set_keyboard_input_timeout(100000);
    using_history(); stifle_history(1000);
    if (!history_.empty() && std::filesystem::exists(history_)) {
        // Bound loading work even when a history file was edited externally.
        if (std::filesystem::file_size(history_) > 16*1024*1024) throw std::runtime_error("History file exceeds 16 MiB.");
        if (read_history(history_.c_str()) != 0) throw std::runtime_error("Cannot read history: "+history_.string());
    }
#ifndef _WIN32
    for (size_t i=0;i<4;++i) previous_handlers[i] = std::signal(exit_signals[i],restore_on_signal);
#endif
    configure_screen();
#else
    (void)engine;
#endif
}
Terminal::~Terminal() {
#ifdef CLICALC_READLINE
    paint_preview("");
    if (reserved_preview) std::cout << "\0337\033[r\0338" << std::flush;
    rl_set_screen_size(screen_rows,screen_columns);
    reserved_preview = false; screen_rows = screen_columns = 0;
#ifndef _WIN32
    screen_reserved = 0;
    for (size_t i=0;i<4;++i) std::signal(exit_signals[i],previous_handlers[i]);
#endif
    rl_event_hook = nullptr; rl_attempted_completion_function = nullptr; current_engine = nullptr;
    clear_history();
#endif
}
bool Terminal::read(std::string& line) {
#ifdef CLICALC_READLINE
    configure_screen(); previous_line.clear(); preview_text.clear(); paint_preview("");
    char* buffer = readline("> ");
    preview_text.clear(); paint_preview("");
    if (!buffer) { std::cout << '\n'; return false; }
    line = buffer; std::free(buffer);
    if (!line.empty()) {
        add_history(line.c_str());
        if (!history_.empty()) {
            auto parent = history_.parent_path(); if (!parent.empty()) std::filesystem::create_directories(parent);
            std::ofstream stream(history_,std::ios::app);
            if (!stream) throw std::runtime_error("Cannot save history: "+history_.string());
            stream << line << '\n'; stream.close();
            if (!stream || history_truncate_file(history_.c_str(),1000) != 0) throw std::runtime_error("Could not finish saving history.");
        }
    }
    return true;
#else
    std::cout << "> " << std::flush;
    if (!std::getline(std::cin,line)) { std::cout << '\n'; return false; }
    return true;
#endif
}
}
