#pragma once
#include "clicalc/number.hpp"
#include "clicalc/error.hpp"
#include <map>
#include <string>
#include <vector>
#include <random>

namespace clicalc {
struct Unit { Number scale; std::map<std::string, int> dimensions; Number offset{0}; bool affine = false; };
struct Function { std::vector<std::string> parameters; std::string expression; };
enum class ItemKind { value, variable, function, unit, setting, message };
struct Item {
    ItemKind kind = ItemKind::message;
    std::string name;
    std::optional<Number> value;
    std::string text;
    std::string formatted;
    std::optional<Function> function;
    std::optional<Unit> unit;
    bool suppressed = false;
    int base = 10;
};
struct Settings {
    int base;
    unsigned digits;
    bool degrees;
    std::string style;
    unsigned word_bits;
    bool signed_words;
    bool preview;
};
struct Result {
    // Kept for simple text frontends; native clients should use items/error/settings.
    std::vector<std::string> output;
    std::vector<Item> items;
    std::optional<Diagnostic> error;
    std::optional<Settings> settings;
    bool exit = false;
    bool clear = false;
};
struct UnitInfo { std::string name; Unit unit; };
struct Completion { std::string text; ItemKind kind; };
struct Completions { SourceSpan replacement; std::vector<Completion> candidates; };
class Parser;
// UI-independent session. Separate instances can be used on separate threads.
class Engine {
public:
    Engine();
    Result execute(const std::string& line);
    // Calculation errors are returned, not thrown. Failed submissions are atomic.
    Result process(const std::string& line);
    Result preview(const std::string& line) const;
    Settings settings() const;
    Completions complete(const std::string& line, size_t cursor) const;
    std::vector<UnitInfo> units(const std::string& filter = "") const;
    Number evaluate(const std::string& expression) const;
    std::string format(const Number& value) const;
    const std::map<std::string, Number>& variables() const { return variables_; }
    const std::map<std::string, Function>& functions() const { return functions_; }
    // Plain-text definitions only; CLI owns file locations and all I/O.
    std::string definitions() const;
    void restore_definitions(const std::string& text);
    std::string session() const;
    void restore_session(const std::string& text);
    int base() const { return base_; }
private:
    friend class Parser;
    std::map<std::string, Number> variables_;
    std::map<std::string, Function> functions_;
    std::map<std::string, Unit> units_;
    std::map<std::string, std::string> custom_units_;
    int base_ = 10;
    unsigned digits_ = 30;
    bool degrees_ = true;
    unsigned word_bits_ = 0;
    bool signed_words_ = true;
    bool preview_enabled_ = true;
    bool previewing_ = false;
    mutable unsigned evaluation_steps_ = 0;
    mutable std::mt19937_64 random_{std::random_device{}()};
    std::string style_ = "auto";
    Number eval(const std::string&, const std::map<std::string, Number>&, unsigned) const;
    Number call(const std::string&, const std::vector<Number>&, unsigned) const;
    Unit unit_expression(const std::string&) const;
    Number conversion(const std::string&) const;
    Result statement(const std::string&);
    void initialize_units();
    Number normalize(const Number&) const;
    std::string format_base(const Number&, int) const;
};
}
