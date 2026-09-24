#include "clicalc/engine.hpp"
#include <algorithm>
#include <cctype>
#include <regex>
#include <set>
#include <sstream>

namespace clicalc {
Settings Engine::settings() const {
    return {base_, digits_, degrees_, style_, word_bits_, signed_words_, preview_enabled_};
}
Result Engine::process(const std::string& line) {
    try { return execute(line); }
    catch (const CalcError& error) {
        Result result; result.error = error.diagnostic; result.settings = settings(); return result;
    } catch (const std::exception& error) {
        Result result; result.error = Diagnostic{ErrorCode::internal, error.what(), {}, {}};
        result.settings = settings(); return result;
    }
}
Result Engine::preview(const std::string& line) const {
    Engine staged = *this; staged.previewing_ = true;
    auto result = staged.process(line);
    // Only calculations have a visual preview. Commands, definitions, and control
    // signals never escape the speculative session.
    if (result.error) return result;
    std::optional<Item> last;
    for (const auto& item : result.items)
        if (item.kind == ItemKind::value && !item.suppressed) last = item;
    Result preview; preview.settings = settings();
    if (last) { preview.items.push_back(*last); preview.output.push_back(last->text); }
    return preview;
}
Number Engine::normalize(const Number& n) const {
    if (!word_bits_) return n;
    auto value = n.as_integer(); mpz_class modulus = mpz_class(1) << word_bits_;
    mpz_mod(value.get_mpz_t(), value.get_mpz_t(), modulus.get_mpz_t());
    if (signed_words_ && value >= (modulus >> 1)) value -= modulus;
    Number result; mpfr_set_z(result.data(), value.get_mpz_t(), MPFR_RNDN); return result;
}
std::string Engine::format_base(const Number& value, int base) const {
    if (!word_bits_ || !value.integer()) return value.format(base, digits_, style_);
    auto number = normalize(value);
    if (base == 10) return number.as_integer().get_str();
    auto bits = number.as_integer(); if (bits < 0) bits += mpz_class(1) << word_bits_;
    auto text = bits.get_str(base);
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c){ return std::toupper(c); });
    size_t width = base == 2 ? word_bits_ : word_bits_ / 4;
    return std::string(base == 2 ? "0b" : "0x") + std::string(width - text.size(), '0') + text;
}
std::vector<UnitInfo> Engine::units(const std::string& filter) const {
    std::vector<UnitInfo> result;
    for (const auto& [name, unit] : units_) {
        bool matches = name.find(filter) != std::string::npos;
        for (const auto& [dimension, power] : unit.dimensions) {
            (void)power;
            matches = matches || dimension.find(filter) != std::string::npos;
        }
        if (matches) result.push_back({name, unit});
    }
    return result;
}
Completions Engine::complete(const std::string& line, size_t cursor) const {
    cursor = std::min(cursor, line.size()); size_t start = cursor;
    auto word = [](unsigned char c){ return std::isalnum(c) || c == '_'; };
    while (start && word(line[start-1])) --start;
    auto prefix = line.substr(start, cursor-start);
    auto before = line.substr(0,start);
    std::istringstream context(before); std::string command; context >> command;
    std::map<std::string, ItemKind> choices;
    auto add = [&](const std::string& text, ItemKind kind) {
        if (text.compare(0,prefix.size(),prefix) == 0) choices[text] = kind;
    };
    auto words = [&](const std::string& text, ItemKind kind) {
        std::istringstream stream(text); std::string name; while (stream >> name) add(name,kind);
    };
    bool only_arguments = !command.empty() && before.find(';') == std::string::npos;
    if (only_arguments && (command == "base" || command == "display")) words("bin dec hex",ItemKind::setting);
    else if (only_arguments && command == "mode") words("rad deg",ItemKind::setting);
    else if (only_arguments && command == "format") words("auto never always eng prefix finance",ItemKind::setting);
    else if (only_arguments && command == "programmer") words("off 8 16 32 64 signed unsigned",ItemKind::setting);
    else if (only_arguments && command == "preview") words("on off",ItemKind::setting);
    else {
        bool unit_context = before.find("->") != std::string::npos || command == "units" || command == "unit";
        if (!unit_context) {
            words("help base bases display vlist list disp del rem mode format sigfigs unit units programmer preview echo exit quit clear cls",ItemKind::message);
            words("sqrt ln log log2 log8 log10 exp sin cos tan asin acos atan sinh cosh tanh abs round ceil floor trunc rand mod min max pow atan2",ItemKind::function);
            for (const auto& [name,value] : variables_) { (void)value; add(name,ItemKind::variable); }
            for (const auto& [name,value] : functions_) { (void)value; add(name,ItemKind::function); }
        }
        for (const auto& [name,unit] : units_) {
            add(name,ItemKind::unit);
            if (!unit.affine) for (const auto* p : {"P","T","G","M","k","c","m","u","n","p","f","a","peta","tera","giga","mega","kilo","centi","milli","micro","nano","pico","femto","atto"})
                add(std::string(p)+name,ItemKind::unit);
        }
    }
    Completions result{{start,cursor-start},{}};
    for (const auto& [text,kind] : choices) result.candidates.push_back({text,kind});
    return result;
}
std::string Engine::session() const {
    std::ostringstream stream;
    stream << "clicalc-session 1\nsettings " << base_ << ' ' << digits_ << ' ' << degrees_
           << ' ' << style_ << ' ' << word_bits_ << ' ' << signed_words_ << ' ' << preview_enabled_ << '\n';
    for (const auto& [name,value] : variables_) if (name != "pi" && name != "e")
        stream << "value " << name << ' ' << value.serialize() << '\n';
    stream << definitions(); return stream.str();
}
void Engine::restore_session(const std::string& text) {
    auto fail = []{ throw CalcError(ErrorCode::invalid_state,"Invalid or unsupported session file."); };
    if (text.size() > 1024*1024) fail();
    Engine restored; std::istringstream stream(text); std::string line;
    if (!std::getline(stream,line) || line != "clicalc-session 1") fail();
    if (!std::getline(stream,line)) fail();
    std::istringstream settings_line(line); std::string tag, style, extra;
    int base, digits, degrees, bits, signed_words, preview;
    if (!(settings_line >> tag >> base >> digits >> degrees >> style >> bits >> signed_words >> preview) || tag != "settings" || (settings_line >> extra)) fail();
    if ((base != 2 && base != 10 && base != 16) || digits < 1 || digits > 100 ||
        (degrees != 0 && degrees != 1) || (bits != 0 && bits != 8 && bits != 16 && bits != 32 && bits != 64) ||
        (signed_words != 0 && signed_words != 1) || (preview != 0 && preview != 1) ||
        !std::set<std::string>{"auto","never","always","eng","prefix","finance"}.count(style)) fail();
    std::string definitions; std::set<std::string> names;
    while (std::getline(stream,line)) {
        if (line.size() > 16384) fail();
        if (line.substr(0,6) != "value ") { definitions += line+"\n"; continue; }
        std::istringstream record(line); std::string name, encoded; record >> tag;
        if (!(record >> name >> encoded) || (record >> extra) || !names.insert(name).second ||
            !std::regex_match(name,std::regex("[A-Za-z_][A-Za-z_0-9]*")) || name == "pi" || name == "e") fail();
        try {
            // Validate non-ans names through the normal assignment rules.
            if (name != "ans") restored.execute(name+"=0");
            restored.variables_[name] = Number(encoded,16);
        } catch (const std::exception&) { fail(); }
    }
    // Assignment validation may update ans; restore its exact saved value last.
    std::istringstream values(text);
    while (std::getline(values,line)) if (line.substr(0,10) == "value ans ") restored.variables_["ans"] = Number(line.substr(10),16);
    if (!names.count("ans")) fail();
    try { restored.restore_definitions(definitions); } catch (const std::exception&) { fail(); }
    restored.base_ = base; restored.digits_ = static_cast<unsigned>(digits); restored.degrees_ = degrees;
    restored.style_ = style; restored.word_bits_ = static_cast<unsigned>(bits);
    restored.signed_words_ = signed_words; restored.preview_enabled_ = preview;
    *this = std::move(restored);
}
}
