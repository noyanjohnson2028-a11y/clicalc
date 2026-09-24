#include "clicalc/engine.hpp"
#include <cctype>
#include <functional>
#include <sstream>
#include <stdexcept>

namespace clicalc {
namespace {
int dimension_power(long value) {
    if (value < -100 || value > 100)
        throw CalcError(ErrorCode::limit, "Combined unit powers must be between -100 and 100.");
    return static_cast<int>(value);
}
Unit combine(Unit a, const Unit& b, int sign) {
    if (a.affine || b.affine) throw CalcError(ErrorCode::incompatible_units,"Offset temperatures cannot appear in compound units; use deltaC or deltaF for differences.");
    a.scale = sign == 1 ? a.scale*b.scale : a.scale/b.scale;
    for (const auto& [dimension,power] : b.dimensions) {
        a.dimensions[dimension] = dimension_power(static_cast<long>(a.dimensions[dimension])+sign*power);
        if (!a.dimensions[dimension]) a.dimensions.erase(dimension);
    }
    return a;
}
}
Unit Engine::unit_expression(const std::string& text) const {
    size_t p = 0; unsigned nesting = 0;
    auto space = [&] { while (p < text.size() && std::isspace(static_cast<unsigned char>(text[p]))) ++p; };
    auto resolve = [&](const std::string& name) -> Unit {
        auto exact = units_.find(name); if (exact != units_.end()) return exact->second;
        if (name.size() > 1 && name.back() == 's') { auto singular = units_.find(name.substr(0,name.size()-1)); if (singular != units_.end()) return singular->second; }
        const std::vector<std::pair<std::string,int>> prefixes{{"femto",-15},{"micro",-6},{"milli",-3},{"centi",-2},{"nano",-9},{"pico",-12},{"kilo",3},{"mega",6},{"giga",9},{"tera",12},{"peta",15},{"atto",-18},{"da",1},{"k",3},{"M",6},{"G",9},{"T",12},{"P",15},{"c",-2},{"m",-3},{"u",-6},{"n",-9},{"p",-12},{"f",-15},{"a",-18}};
        for (const auto& [prefix,exponent] : prefixes) {
            if (name.substr(0,prefix.size()) != prefix) continue;
            auto rest = name.substr(prefix.size()); auto unit = units_.find(rest);
            if (unit == units_.end() && rest.size() > 1 && rest.back() == 's') unit = units_.find(rest.substr(0,rest.size()-1));
            if (unit != units_.end() && !unit->second.affine) { auto result = unit->second; result.scale = result.scale*Number("1e"+std::to_string(exponent)); return result; }
        }
        throw CalcError(ErrorCode::unknown_name, "Unknown unit '"+name+"'.");
    };
    std::function<Unit()> expression, factor;
    factor = [&]() -> Unit {
        space(); if (++nesting > 64) throw CalcError(ErrorCode::argument, "Unit nesting exceeds 64.");
        Unit result{Number(1),{}};
        if (p < text.size() && text[p] == '(') { ++p; result = expression(); space(); if (p == text.size() || text[p++] != ')') throw CalcError(ErrorCode::argument, "Expected ')' in unit expression."); }
        else {
            size_t start = p; while (p < text.size() && (std::isalpha(static_cast<unsigned char>(text[p])) || text[p] == '_')) ++p;
            if (start == p) { if (p < text.size() && text[p] == '1') ++p; else throw CalcError(ErrorCode::argument, "Expected a unit name."); }
            else {
                try { result = resolve(text.substr(start,p-start)); }
                catch (const CalcError& error) { auto diagnostic = error.diagnostic; diagnostic.span = SourceSpan{start,p-start}; throw CalcError(std::move(diagnostic)); }
            }
        }
        space();
        if (p < text.size() && text[p] == '^') {
            if (result.affine) throw CalcError(ErrorCode::incompatible_units,"Offset temperatures cannot be raised to powers; use deltaC or deltaF.",SourceSpan{p,1});
            ++p; space(); int sign = 1;
            if (p < text.size() && (text[p] == '-' || text[p] == '+')) { if (text[p] == '-') sign = -1; ++p; }
            int power = 0; size_t start = p;
            while (p < text.size() && std::isdigit(static_cast<unsigned char>(text[p]))) { power = power*10 + text[p++]-'0'; if (power > 100) throw CalcError(ErrorCode::argument, "Unit powers must be between -100 and 100."); }
            if (p == start) throw CalcError(ErrorCode::argument, "Expected integer unit power.");
            power *= sign; Number scale;
            mpfr_pow_si(scale.data(),result.scale.data(),power,MPFR_RNDN); result.scale = checked(scale);
            for (auto& [dimension,n] : result.dimensions) n = dimension_power(static_cast<long>(n)*power);
            if (!power) result.dimensions.clear();
        }
        --nesting; return result;
    };
    expression = [&]() -> Unit {
        auto result = factor(); space();
        while (p < text.size() && (text[p] == '*' || text[p] == '-' || text[p] == '/')) {
            char op = text[p++]; result = combine(result,factor(),op == '/' ? -1 : 1); space();
        }
        return result;
    };
    try {
        auto result = expression(); space(); if (p != text.size()) throw CalcError(ErrorCode::syntax,"Unexpected text in unit expression: " + text.substr(p)); return result;
    } catch (const CalcError& error) {
        auto diagnostic = error.diagnostic;
        if (!diagnostic.span) diagnostic.span = SourceSpan{p,p < text.size() ? 1u : 0u};
        throw CalcError(std::move(diagnostic));
    }
}
Number Engine::conversion(const std::string& text) const {
    auto arrow = text.find("->");
    auto left = text.substr(0,arrow), right = text.substr(arrow+2);
    Unit source; Number quantity(1); bool found = false, explicit_quantity = false;
    std::optional<Diagnostic> unit_error;
    // A bare unit expression means a quantity of one.
    try { source = unit_expression(left); found = true; } catch (const CalcError& error) { unit_error = error.diagnostic; }
    if (!found) {
        // Quantity and units are separated by whitespace; try each boundary.
        for (size_t i=1;i<left.size();++i) {
            if (!std::isspace(static_cast<unsigned char>(left[i]))) continue;
            try { source = unit_expression(left.substr(i)); }
            catch (const CalcError& error) {
                auto diagnostic = error.diagnostic; if (diagnostic.span) diagnostic.span->offset += i;
                unit_error = diagnostic; continue;
            }
            quantity = eval(left.substr(0,i),{},0); found = true; explicit_quantity = true; break;
        }
    }
    if (!found) { if (unit_error) throw CalcError(*unit_error); throw CalcError(ErrorCode::syntax,"Expected '<expression> <units> -> <units>', for example 10 in -> cm."); }
    Unit target;
    try { target = unit_expression(right); }
    catch (const CalcError& error) { auto diagnostic = error.diagnostic; if (diagnostic.span) diagnostic.span->offset += arrow+2; throw CalcError(std::move(diagnostic)); }
    if (source.dimensions != target.dimensions) throw CalcError(ErrorCode::incompatible_units, "Cannot convert between incompatible dimensions.",SourceSpan{arrow+2,right.size()});
    if (!explicit_quantity && (source.affine || target.affine)) throw CalcError(ErrorCode::argument,"Offset temperature conversion requires an explicit quantity.",SourceSpan{0,left.size()});
    return (quantity*source.scale+source.offset-target.offset)/target.scale;
}
void Engine::initialize_units() {
    auto add = [&](const std::string& names, const Unit& value) { std::istringstream stream(names); std::string name; while (stream >> name) units_[name] = value; };
    add("m meter",{Number(1),{{"length",1}}});
    add("kg kilogram",{Number(1),{{"mass",1}}});
    add("g gram",{Number("0.001"),{{"mass",1}}});
    add("s sec second",{Number(1),{{"time",1}}});
    add("A ampere",{Number(1),{{"current",1}}});
    add("K kelvin",{Number(1),{{"temperature",1}}});
    add("degC celsius Celsius",{Number(1),{{"temperature",1}},Number("273.15"),true});
    add("degF fahrenheit Fahrenheit",{Number(5)/Number(9),{{"temperature",1}},Number("273.15")-Number(32)*Number(5)/Number(9),true});
    add("deltaC",{Number(1),{{"temperature",1}}});
    add("deltaF",{Number(5)/Number(9),{{"temperature",1}}});
    add("mol mole",{Number(1),{{"amount",1}}});
    add("cd candela",{Number(1),{{"luminosity",1}}});
    add("rad radian sr steradian",{Number(1),{}});
    auto derived = [&](const std::string& names, const std::string& scale, const std::string& expression) { auto unit = unit_expression(expression); unit.scale = unit.scale*Number(scale); add(names,unit); };
    derived("Hz hertz Bq becquerel","1","1/s");
    derived("N newton","1","kg*m/s^2");
    derived("Pa pascal","1","N/m^2");
    derived("J joule","1","N*m");
    derived("W watt","1","J/s");
    derived("C coulomb","1","s*A");
    derived("V volt","1","W/A");
    derived("F farad","1","C/V");
    derived("ohm","1","V/A");
    derived("S siemens mho","1","1/ohm");
    derived("Wb weber","1","V*s");
    derived("T tesla","1","Wb/m^2");
    derived("H henry","1","Wb/A");
    derived("lm lumen","1","cd");
    derived("lx lux","1","lm/m^2");
    derived("Gy gray Sv sievert","1","J/kg");
    derived("in inch inches","0.0254","m");
    derived("ft foot feet","0.3048","m");
    derived("yd yard","0.9144","m");
    derived("mi mile","1609.344","m");
    derived("mil","0.0000254","m");
    derived("angstrom","1e-10","m");
    derived("micron","1e-6","m");
    derived("fermi","1e-15","m");
    derived("AU astronomicalUnit","149597870700","m");
    derived("lightYear","9460730472580800","m");
    derived("pc parsec","3.0856775814913673e16","m");
    derived("pica","0.00423333333333333333333333333333333","m");
    derived("min minute","60","s");
    derived("h hr hour","3600","s");
    derived("day","86400","s");
    derived("wk week","604800","s");
    derived("yr year","31557600","s");
    derived("acre","4046.8564224","m^2");
    derived("ha hectare","10000","m^2");
    derived("L l liter litre","0.001","m^3");
    derived("gal gallon","3.785411784","L");
    derived("qt quart","0.25","gal");
    derived("pint","0.125","gal");
    derived("cup","0.0625","gal");
    derived("tbsp tablespoon","0.0625","cup");
    derived("tsp teaspoon","0.0208333333333333333333333333333333333333333333333333","cup");
    derived("lb pound","0.45359237","kg");
    derived("oz ounce","0.0625","lb");
    derived("gr grain","0.00006479891","kg");
    derived("bar","100000","Pa");
    derived("atm atmosphere","101325","Pa");
    derived("cal calorie","4.1868","J");
    derived("eV electronVolt","1.602176634e-19","J");
    derived("erg","1e-7","J");
    derived("curie","3.7e10","Bq");
    derived("gauss","0.0001","T");
    derived("Mx maxwell","1e-8","Wb");
    add("degree",{variables_.at("pi")/Number(180),{}});
    add("revolution",{variables_.at("pi")*Number(2),{}});
    add("rpm revolutionPerMinute",{variables_.at("pi")/Number(30),{{"time",-1}}});
}
}
