#include "clicalc/engine.hpp"
#include <algorithm>
#include <cctype>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>

namespace clicalc {
void validate_expression(const Engine&, const std::string&);
namespace {
std::string trim(const std::string& s) {
    auto begin = s.find_first_not_of(" \t\r\n");
    return begin == std::string::npos ? "" : s.substr(begin,s.find_last_not_of(" \t\r\n")-begin+1);
}
bool identifier(const std::string& s) { return std::regex_match(s,std::regex("[A-Za-z_][A-Za-z_0-9]*")); }
bool reserved(const std::string& s) {
    static const std::set<std::string> names{"pi","e","ans","help","base","bases","display","vlist","list","disp","del","rem","mode","format","sigfigs","unit","units","programmer","preview","echo","exit","quit","clear","cls"};
    return names.count(s);
}
std::string definition(const std::string& name, const Function& f) {
    std::string s = name+"(";
    for (size_t i=0;i<f.parameters.size();++i) { if (i) s += ","; s += f.parameters[i]; }
    return s+") = "+f.expression;
}
}
Engine::Engine() {
    variables_["ans"] = Number(0);
    mpfr_const_pi(variables_["pi"].data(),MPFR_RNDN);
    mpfr_exp(variables_["e"].data(),Number(1).data(),MPFR_RNDN);
    initialize_units();
}
std::string Engine::format(const Number& value) const { return format_base(value,base_); }
Result Engine::execute(const std::string& line) {
    if (line.size() > 16384) throw CalcError(ErrorCode::limit, "Input exceeds 16384 characters.");
    // A failed line leaves the session unchanged, including definitions and ans.
    Engine staged = *this; Result combined;
    std::string input = line.substr(0,line.find('#'));
    size_t start = 0;
    while (start < input.size()) {
        auto end = input.find(';',start);
        auto text = trim(input.substr(start,end == std::string::npos ? end : end-start));
        if (!text.empty()) {
            Result result;
            try { result = staged.statement(text); }
            catch (const CalcError& error) {
                auto diagnostic = error.diagnostic;
                auto leading = input.find_first_not_of(" \t\r\n",start);
                if (!diagnostic.span) diagnostic.span = SourceSpan{0,text.size()};
                diagnostic.span->offset += leading;
                throw CalcError(std::move(diagnostic));
            }
            if (end == std::string::npos) combined.output.insert(combined.output.end(),result.output.begin(),result.output.end());
            for (auto item : result.items) { item.suppressed = end != std::string::npos; combined.items.push_back(std::move(item)); }
            combined.clear = combined.clear || result.clear;
            combined.exit = result.exit;
            if (result.exit) break;
        }
        if (end == std::string::npos) break;
        start = end + 1;
    }
    *this = std::move(staged); combined.settings = settings(); return combined;
}
Result Engine::statement(const std::string& input) {
    Result result;
    auto split = input.find_first_of(" \t");
    auto command = input.substr(0,split);
    auto args = split == std::string::npos ? "" : trim(input.substr(split));
    auto say = [&](const std::string& s, ItemKind kind = ItemKind::message, const std::string& name = "") {
        result.output.push_back(s); Item item; item.kind = kind; item.name = name; item.text = s;
        result.items.push_back(std::move(item));
    };
    auto number = [&](const std::string& name, const Number& value, ItemKind kind, int base) {
        auto formatted = format_base(value,base);
        say(name+" = "+formatted,kind,name); result.items.back().value = word_bits_ && value.integer() ? normalize(value) : value;
        result.items.back().formatted = formatted;
        result.items.back().base = base;
    };
    auto expression_at = [&](const std::string& expression, size_t offset, size_t inserted = 0) {
        try { return evaluate(expression); }
        catch (const CalcError& error) {
            auto diagnostic = error.diagnostic;
            if (diagnostic.span) diagnostic.span->offset = offset + (diagnostic.span->offset > inserted ? diagnostic.span->offset - inserted : 0);
            else diagnostic.span = SourceSpan{offset,expression.size()-inserted};
            throw CalcError(std::move(diagnostic));
        }
    };
    auto no_args = [&] { if (!args.empty()) throw CalcError(ErrorCode::argument, command+" takes no arguments."); };
    if (command == "exit" || command == "quit") { no_args(); result.exit = true; }
    else if (command == "clear" || command == "cls") { no_args(); result.clear = true; }
    else if (command == "help") {
        no_args(); say(
            "Expressions: 4+5*4, 2^10, 5!, 0xFF+0b101, sqrt(2), 5M+100k\n"
            "Variables:   x = 25; x/2     ans is the previous result; pi and e are constants\n"
            "Functions:   par(x,y) = x*y/(x+y)     par(10,10)\n"
            "Units:       10 in -> cm     70 mi/h -> m/s     unit furlong = 201.168 m\n"
            "base [bin|dec|hex|2|10|16]  Set display base and redisplay ans (input stays decimal)\n"
            "bases [EXPR]              Show decimal, hex, and binary together\n"
            "programmer [off|8|16|32|64] [signed|unsigned]  Wrap integer arithmetic to a word\n"
            "units [FILTER]            Discover units by name or dimension\n"
            "preview [on|off]          Toggle the reserved bottom preview line\n"
            "vlist                     List variables, including ans, pi, and e\n"
            "list                      List user-defined functions\n"
            "disp NAME                 Display a variable\n"
            "del NAME | del all        Delete a variable/function or all user variables/functions\n"
            "mode [rad|deg]            Select trigonometric angle units\n"
            "sigfigs 1..100            Set displayed precision (finance: decimal places)\n"
            "format auto|never|always|eng|prefix|finance\n"
            "echo TEXT | clear | exit\n"
            "Math: sqrt ln log log2 logN exp sin cos tan asin acos atan sinh cosh tanh\n"
            "      abs round ceil floor trunc rand(max) mod(x,y) min(x,y) max(x,y)\n"
            "      pow(x,y) atan2(y,x)\n"
            "Operators: + - * / ^ % ! << >> & | @ ~ < <= > >= == != && ||\n"
            "Terminal editor: Up/Down recall history; Ctrl-R searches; Tab completes; Ctrl-L clears.\n"
            "Initial +, *, / insert 'ans '; -- becomes 'ans -'; a single - stays negative.\n"
            "Leading whitespace is ignored for operator expansion.\n"
            "Scripts: initial +, *, /, or -- continue from ans; - starts a negative number.\n"
            "A trailing ; hides output.\n"
            "# starts a comment. display aliases base; rem aliases del.");
    } else if (command == "base" || command == "display") {
        if (args.empty()) say("base = "+std::to_string(base_),ItemKind::setting,"base");
        else {
            if (args == "bin" || args == "binary" || args == "2") base_ = 2;
            else if (args == "dec" || args == "decimal" || args == "10") base_ = 10;
            else if (args == "hex" || args == "hexadecimal" || args == "16") base_ = 16;
            else throw CalcError(ErrorCode::argument, "Usage: base bin|dec|hex (or 2|10|16).");
            number("ans",variables_.at("ans"),ItemKind::variable,base_);
        }
    } else if (command == "bases") {
        auto value = args.empty() ? variables_.at("ans") : expression_at(args,input.find(args,split));
        number("dec",value,ItemKind::variable,10);
        number("hex",value,ItemKind::variable,16);
        number("bin",value,ItemKind::variable,2);
    } else if (command == "programmer") {
        if (!args.empty()) {
            std::istringstream stream(args); std::string width, sign, extra; stream >> width;
            if (width == "off") { if (stream >> extra) throw CalcError(ErrorCode::argument,"Usage: programmer off"); word_bits_ = 0; }
            else {
                if (width != "8" && width != "16" && width != "32" && width != "64") throw CalcError(ErrorCode::argument,"Usage: programmer 8|16|32|64 [signed|unsigned], or programmer off");
                if (stream >> sign) {
                    if (sign != "signed" && sign != "unsigned") throw CalcError(ErrorCode::argument,"Choose signed or unsigned.");
                    if (stream >> extra) throw CalcError(ErrorCode::argument,"Too many programmer arguments.");
                    signed_words_ = sign == "signed";
                }
                word_bits_ = static_cast<unsigned>(std::stoul(width));
            }
        }
        say(word_bits_ ? "programmer = "+std::to_string(word_bits_)+(signed_words_ ? " signed" : " unsigned") : "programmer = off",ItemKind::setting,"programmer");
    } else if (command == "preview") {
        if (!args.empty()) {
            if (args != "on" && args != "off") throw CalcError(ErrorCode::argument,"Usage: preview on|off");
            preview_enabled_ = args == "on";
        }
        say(preview_enabled_ ? "preview = on" : "preview = off",ItemKind::setting,"preview");
    } else if (command == "units") {
        for (const auto& info : units(args)) {
            std::string description = info.name+" : ";
            if (info.unit.dimensions.empty()) description += "dimensionless";
            for (const auto& [dimension,power] : info.unit.dimensions) description += dimension+"^"+std::to_string(power)+" ";
            if (info.unit.affine) description += "(absolute temperature)";
            say(description,ItemKind::unit,info.name); result.items.back().unit = info.unit;
        }
        if (result.items.empty()) say("No matching units.");
    } else if (command == "vlist") {
        no_args(); for (const auto& [name,value] : variables_) number(name,value,ItemKind::variable,base_);
    } else if (command == "list") {
        no_args(); for (const auto& [name,f] : functions_) { say(definition(name,f),ItemKind::function,name); result.items.back().function = f; }
        if (functions_.empty()) say("No user-defined functions.");
    } else if (command == "disp") {
        if (!variables_.count(args)) throw CalcError(ErrorCode::unknown_name, "Unknown variable '"+args+"'.");
        number(args,variables_.at(args),ItemKind::variable,base_);
    } else if (command == "del" || command == "rem") {
        if (args == "all") {
            functions_.clear();
            for (auto it=variables_.begin();it!=variables_.end();) { if (!reserved(it->first)) it=variables_.erase(it); else ++it; }
        } else {
            if (reserved(args)) throw CalcError(ErrorCode::argument, "Cannot delete a built-in constant or ans.");
            auto count = variables_.erase(args)+functions_.erase(args);
            if (!count) throw CalcError(ErrorCode::unknown_name, "Unknown variable or function '"+args+"'.");
        }
    } else if (command == "echo") say(args);
    else if (command == "mode") {
        if (args.empty()) say(degrees_ ? "mode = deg" : "mode = rad",ItemKind::setting,"mode");
        else if (args == "rad" || args == "deg") degrees_ = args == "deg";
        else throw CalcError(ErrorCode::argument, "Usage: mode rad|deg.");
    } else if (command == "sigfigs") {
        if (args.empty()) say("sigfigs = "+std::to_string(digits_),ItemKind::setting,"sigfigs");
        else {
            if (args.size() > 3 || !std::all_of(args.begin(),args.end(),[](unsigned char c){return std::isdigit(c);})) throw CalcError(ErrorCode::argument, "Usage: sigfigs 1..100.");
            auto n = std::stoul(args); if (n < 1 || n > 100) throw CalcError(ErrorCode::argument, "Usage: sigfigs 1..100."); digits_ = static_cast<unsigned>(n);
        }
    } else if (command == "format") {
        static const std::set<std::string> styles{"auto","never","always","eng","prefix","finance"};
        if (args.empty()) say("format = "+style_,ItemKind::setting,"format");
        else if (styles.count(args)) style_ = args;
        else throw CalcError(ErrorCode::argument, "Usage: format auto|never|always|eng|prefix|finance.");
    } else if (command == "unit") {
        auto equal = args.find('=');
        if (equal == std::string::npos) throw CalcError(ErrorCode::argument, "Usage: unit NAME = NUMBER UNITS.");
        auto name = trim(args.substr(0,equal)); auto rhs = trim(args.substr(equal+1));
        if (!identifier(name) || units_.count(name)) throw CalcError(ErrorCode::argument, "Unit name is invalid or already defined.");
        auto space = rhs.find_first_of(" \t");
        if (space == std::string::npos) throw CalcError(ErrorCode::argument, "Separate the scale and unit with a space.");
        Number scale(rhs.substr(0,space)); if (!(Number(0) < scale)) throw CalcError(ErrorCode::argument, "Unit scale must be positive.");
        auto value = unit_expression(trim(rhs.substr(space)));
        if (value.affine) throw CalcError(ErrorCode::argument,"Custom unit scales cannot be based on an offset temperature. Use K or deltaC/deltaF.");
        value.scale = value.scale*scale;
        units_[name] = value;
        const std::map<std::string,std::string> symbols{{"length","m"},{"mass","kg"},{"time","s"},{"current","A"},{"temperature","K"},{"amount","mol"},{"luminosity","cd"}};
        std::string canonical = value.scale.format(10,100)+" 1";
        for (const auto& [dimension,power] : value.dimensions) canonical += "*"+symbols.at(dimension)+"^"+std::to_string(power);
        custom_units_[name] = canonical;
        say("Defined unit "+name+".",ItemKind::unit,name); result.items.back().unit = value;
    } else {
        std::smatch match;
        static const std::regex function_pattern("^([A-Za-z_][A-Za-z_0-9]*)\\s*\\(([^()]*)\\)\\s*=([^=].*)$");
        static const std::regex variable_pattern("^([A-Za-z_][A-Za-z_0-9]*)\\s*=([^=].*)$");
        if (std::regex_match(input,match,function_pattern)) {
            std::string name = match[1]; if (reserved(name)) throw CalcError(ErrorCode::argument, "Reserved function name.");
            Function f; f.expression = trim(match[3]);
            auto parameters = trim(match[2]);
            if (!parameters.empty()) {
                size_t start = 0;
                do {
                    auto end = parameters.find(',',start); auto parameter = trim(parameters.substr(start,end == std::string::npos ? end : end-start));
                    if (!identifier(parameter) || std::find(f.parameters.begin(),f.parameters.end(),parameter) != f.parameters.end()) throw CalcError(ErrorCode::argument, "Invalid or duplicate parameter name.");
                    f.parameters.push_back(parameter);
                    if (end == std::string::npos) break;
                    start = end+1;
                } while (true);
            }
            try { validate_expression(*this,f.expression); }
            catch (const CalcError& error) {
                auto diagnostic = error.diagnostic;
                if (diagnostic.span) diagnostic.span->offset += input.find_first_not_of(" \t",static_cast<size_t>(match.position(3)));
                throw CalcError(std::move(diagnostic));
            }
            functions_[name] = f; say(definition(name,f),ItemKind::function,name); result.items.back().function = f;
        } else {
            std::string name = "ans", expression = input;
            size_t offset = 0, inserted = 0;
            if (std::regex_match(input,match,variable_pattern)) {
                name = match[1]; expression = trim(match[2]);
                offset = input.find_first_not_of(" \t",static_cast<size_t>(match.position(2)));
                if (reserved(name)) throw CalcError(ErrorCode::argument, "Cannot assign to a reserved name.");
            } else if (expression.substr(0,2) == "--") { expression = "ans-"+expression.substr(2); inserted = 2; }
            else if (expression[0] == '+' || expression[0] == '*' || expression[0] == '/') { expression = "ans"+expression; inserted = 3; }
            auto value = expression_at(expression,offset,inserted);
            number(name,value,ItemKind::value,base_); variables_[name] = value; variables_["ans"] = value;
        }
    }
    return result;
}
std::string Engine::definitions() const {
    std::string text = "# clicalc user definitions\n";
    for (const auto& [name,f] : functions_) text += definition(name,f)+"\n";
    for (const auto& [name,expr] : custom_units_) text += "unit "+name+" = "+expr+"\n";
    return text;
}
void Engine::restore_definitions(const std::string& text) {
    Engine staged = *this; std::istringstream stream(text); std::string line;
    static const std::regex allowed("^([A-Za-z_][A-Za-z_0-9]*\\s*\\([^()]*\\)\\s*=.*|unit\\s+.*)$");
    while (std::getline(stream,line)) {
        line = trim(line); if (line.empty() || line[0] == '#') continue;
        if (line.find(';') != std::string::npos || !std::regex_match(line,allowed)) throw CalcError(ErrorCode::invalid_state, "State file must contain only function and unit definitions.");
        staged.execute(line);
    }
    *this = std::move(staged);
}
}
