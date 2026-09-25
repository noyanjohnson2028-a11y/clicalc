#include "clicalc/engine.hpp"
#include "terminal.hpp"
#include <algorithm>
#include <cstdlib>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>
#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#include <io.h>
#include <process.h>
#else
#include <unistd.h>
#endif
namespace {
bool terminal() {
#ifdef _WIN32
    return _isatty(_fileno(stdin)) && _isatty(_fileno(stdout));
#else
    return isatty(STDIN_FILENO) && isatty(STDOUT_FILENO);
#endif
}
std::filesystem::path state_path() {
    if (auto xdg = std::getenv("XDG_CONFIG_HOME"); xdg && *xdg) return std::filesystem::path(xdg)/"clicalc"/"definitions.calc";
#ifdef _WIN32
    if (auto appdata = _wgetenv(L"APPDATA"); appdata && *appdata) return std::filesystem::path(appdata)/"clicalc"/"definitions.calc";
#endif
    if (auto home = std::getenv("HOME"); home && *home) return std::filesystem::path(home)/".config"/"clicalc"/"definitions.calc";
    return {};
}
std::string read_file(const std::filesystem::path& path) {
    std::ifstream stream(path);
    if (!stream) throw std::runtime_error("Cannot read state file: "+path.string());
    if (std::filesystem::file_size(path) > 1024*1024) throw std::runtime_error("State file exceeds 1 MiB.");
    return std::string(std::istreambuf_iterator<char>(stream),{});
}
void atomic_write(const std::filesystem::path& path, const std::string& text) {
    auto parent = path.parent_path(); if (!parent.empty()) std::filesystem::create_directories(parent);
    auto temporary = path;
#ifdef _WIN32
    temporary += ".tmp."+std::to_string(_getpid());
#else
    temporary += ".tmp."+std::to_string(getpid());
#endif
    std::ofstream stream(temporary,std::ios::trunc);
    if (!stream) throw std::runtime_error("Cannot save state: "+temporary.string());
    stream << text; stream.close();
    if (!stream) throw std::runtime_error("Could not finish saving state.");
#ifdef _WIN32
    // Windows rename does not replace an existing destination. Keep the old
    // file intact if replacement fails rather than deleting it first.
    if (!MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        auto error = std::error_code(GetLastError(),std::system_category());
        std::error_code ignored;
        std::filesystem::remove(temporary,ignored);
        throw std::filesystem::filesystem_error("Cannot replace state file",path,error);
    }
#else
    std::filesystem::rename(temporary,path);
#endif
}
const char* usage =
    "Usage: clicalc [options] [expression]\n"
    "  -e, --eval EXPR     Evaluate an expression or command (repeatable)\n"
    "  -f, --file FILE     Run a script; '-' reads standard input\n"
    "  --base bin|dec|hex  Select display base\n"
    "  --precision N      Display 1..100 significant digits\n"
    "  --state FILE       Load/save function and unit definitions at FILE\n"
    "  --restore-session  Restore/save variables and settings as well as definitions\n"
    "  --session FILE     Use a full session snapshot at FILE\n"
    "  --history FILE     Use a persistent history file (interactive only)\n"
    "  --no-history       Disable persistent command history\n"
    "  --no-preview       Disable live result previews\n"
    "  --no-state         Disable all persistent state and history\n"
    "  -h, --help         Show this help\n"
    "  --version          Show version\n"
    "Without an expression or file, start a terminal session or read piped input.\n"
    "Quote shell expressions: clicalc '2^10 + sqrt(16)'\n";
}
int main(int argc, char** argv) {
    try {
        std::vector<std::pair<std::string,std::string>> actions;
        std::string base, precision, positional;
        auto state = state_path(); bool state_explicit = false, no_state = false;
        std::filesystem::path session, history;
        bool restore_session = false, no_history = false, no_preview = false;
        for (int i=1;i<argc;++i) {
            std::string arg = argv[i];
            auto value = [&]() -> std::string { if (++i == argc) throw std::runtime_error("Missing value after "+arg); return argv[i]; };
            if (arg == "--help" || arg == "-h") { std::cout << usage; return 0; }
            if (arg == "--version") { std::cout << "clicalc 0.1.0\n"; return 0; }
            if (arg == "--eval" || arg == "-e") actions.emplace_back("eval",value());
            else if (arg == "--file" || arg == "-f") actions.emplace_back("file",value());
            else if (arg == "--base") base = value();
            else if (arg == "--precision") precision = value();
            else if (arg == "--state") { state = value(); state_explicit = true; }
            else if (arg == "--no-state") no_state = true;
            else if (arg == "--restore-session") restore_session = true;
            else if (arg == "--session") { session = value(); restore_session = true; }
            else if (arg == "--history") history = value();
            else if (arg == "--no-history") no_history = true;
            else if (arg == "--no-preview") no_preview = true;
            else if (arg == "--") { while (++i < argc) { if (!positional.empty()) positional += " "; positional += argv[i]; } break; }
            else if (arg.size() > 1 && arg[0] == '-' && !std::isdigit(static_cast<unsigned char>(arg[1])) && arg[1] != '.') throw std::runtime_error("Unknown option: "+arg+" (use -- before an expression starting with '-').");
            else { if (!positional.empty()) positional += " "; positional += arg; }
        }
        if (!positional.empty()) actions.emplace_back("eval",positional);
        bool interactive = actions.empty() && terminal();
        bool persistent = !no_state && !state.empty() && (interactive || state_explicit);
        if (session.empty() && !state.empty()) session = state.parent_path()/"session.calc";
        if (history.empty() && !state.empty()) history = state.parent_path()/"history";
        bool full_session = restore_session && !no_state && !session.empty();
        clicalc::Engine engine;
        if (full_session && std::filesystem::exists(session)) {
            engine.restore_session(read_file(session));
        } else if (persistent && std::filesystem::exists(state)) {
            engine.restore_definitions(read_file(state));
        }
        auto original_definitions = engine.definitions();
        auto original_session = engine.session();
        if (!base.empty()) engine.execute("base "+base);
        if (!precision.empty()) engine.execute("sigfigs "+precision);
        if (no_preview) engine.execute("preview off");
        bool exit = false; int status = 0;
        auto run = [&](const std::string& line, const std::string& location) {
            auto result = engine.process(line);
            if (result.error) {
                const auto& diagnostic = *result.error;
                std::cerr << location << "error: " << diagnostic.message << '\n';
                if (diagnostic.span) {
                    auto offset = std::min(diagnostic.span->offset,line.size());
                    auto padding = line.substr(0,offset);
                    for (char& c : padding) if (c != '\t') c = ' ';
                    auto length = std::min(diagnostic.span->length,line.size()-offset);
                    std::cerr << "  " << line << '\n' << "  " << padding << '^'
                              << std::string(length > 1 ? length-1 : 0,'~') << '\n';
                }
                for (const auto& context : diagnostic.context) std::cerr << "  " << context << '\n';
                status = 1; return false;
            }
            exit = result.exit;
            if (result.clear && interactive) clicalc::cli::clear_screen();
            for (const auto& output : result.output) std::cout << output << '\n';
            return true;
        };
        auto read = [&](std::istream& input, const std::string& source) {
            std::string line; unsigned number = 0;
            while (!exit && std::getline(input,line)) {
                ++number; if (!run(line,source+":"+std::to_string(number)+": ")) break;
            }
            if (input.bad()) throw std::runtime_error("Failed to read "+source);
        };
        if (!actions.empty()) {
            for (const auto& [kind,value] : actions) {
                if (kind == "eval") run(value,"");
                else if (value == "-") read(std::cin,"stdin");
                else { std::ifstream file(value); if (!file) throw std::runtime_error("Cannot open script: "+value); read(file,value); }
                if (exit || status) break;
            }
        } else if (!interactive) read(std::cin,"stdin");
        else {
            clicalc::cli::Terminal editor(engine,no_state || no_history ? std::filesystem::path{} : history);
            std::cout << "clicalc 0.1.0 - type help for commands, exit to quit.\n";
            while (!exit) {
                std::string line;
                if (!editor.read(line)) break;
                run(line,"");
            }
            status = 0; // Correctable prompt errors are not a failed batch invocation.
        }
        if (persistent && original_definitions != engine.definitions()) atomic_write(state,engine.definitions());
        if (full_session && (!std::filesystem::exists(session) || original_session != engine.session())) atomic_write(session,engine.session());
        return status;
    } catch (const std::exception& error) { std::cerr << "error: " << error.what() << '\n'; return 1; }
}
