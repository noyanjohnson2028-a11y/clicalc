#include "clicalc/engine.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>

using namespace clicalc;
int checks = 0;
void check(bool condition, const std::string& description) {
    ++checks; if (!condition) throw std::runtime_error(description);
}
void value(Engine& engine, const std::string& expression, const std::string& expected) {
    auto result = engine.process(expression);
    check(!result.error,expression+": "+(result.error ? result.error->message : ""));
    check(!result.items.empty() && result.items.back().value.has_value(),"Missing numeric result");
    check(result.items.back().value->format(10,30) == expected,expression+": got "+result.output.back());
}
void error(Engine& engine, const std::string& line, ErrorCode code, size_t offset) {
    auto result = engine.process(line);
    check(result.error.has_value(),"Expected error for "+line);
    check(result.error->code == code,"Wrong error code for "+line);
    check(result.error->span && result.error->span->offset == offset,"Wrong source location for "+line);
    check(result.items.empty() && result.output.empty(),"Failed lines must not expose partial results");
}
bool completes(Engine& engine, const std::string& line, const std::string& expected) {
    auto result = engine.complete(line,line.size());
    return std::any_of(result.candidates.begin(),result.candidates.end(),[&](const Completion& c){return c.text == expected;});
}
int main() {
    try {
        Engine e;
        auto r = e.process("x=42; x+2");
        check(!r.error && r.items.size() == 2,"Structured calculation records");
        check(r.items[0].name == "x" && r.items[0].suppressed,"Suppressed assignment metadata");
        check(r.items[1].kind == ItemKind::value && *r.items[1].value == Number(44),"Numeric value available independently of text");
        check(r.settings && r.settings->base == 10,"Settings snapshot");
        e.execute("double(x)=2*x"); r = e.process("list");
        check(r.items[0].function->parameters.at(0) == "x","Structured function parameters");
        auto before = e.session();
        error(e,"   x = 12 + sqrt(",ErrorCode::syntax,17);
        error(e,"x=1; 4/0",ErrorCode::division_by_zero,6);
        check(e.session() == before,"Failed line rollback");
        error(e,"2 + unknown",ErrorCode::unknown_name,4);
        error(e,"sqrt(-1)",ErrorCode::domain,0);
        error(e,"2 + 1e+",ErrorCode::syntax,7);
        e.execute("bad(x)=1/x"); error(e,"2+bad(0)",ErrorCode::division_by_zero,2);
        check(!e.process("bad(0)").error->context.empty(),"Function error context");
        error(e,"1 m -> s",ErrorCode::incompatible_units,6);
        error(e,"1 m -> noSuchUnit",ErrorCode::unknown_name,7);
        error(e,"1/0 m -> cm",ErrorCode::division_by_zero,1);

        check(completes(e,"sq","sqrt"),"Built-in completion");
        check(completes(e,"dou","double"),"User function completion");
        check(completes(e,"x","x"),"Variable completion");
        check(completes(e,"bas","bases"),"Command completion");
        check(completes(e,"base h","hex"),"Contextual setting completion");
        check(!completes(e,"base s","sqrt"),"Setting completion excludes expressions");
        check(completes(e,"1 m -> cent","centimeter"),"Long SI prefix completion");
        check(completes(e,"1 m -> millim","millimeter"),"Prefixed unit completion");
        check(completes(e,"units deg","degC"),"Temperature completion");
        check(e.complete("12 + sq",7).replacement.offset == 5,"Completion replacement span");
        check(completes(e,"forma","format"),"Renamed format command completion");
        check(completes(e,"format e","eng"),"Contextual format completion");
        auto format_query = e.process("format");
        check(format_query.items[0].name == "format" && format_query.output[0] == "format = auto","Structured format query");
        e.execute("format eng; sigfigs 6");
        for (const auto& [input,expected] : std::vector<std::pair<std::string,std::string>>{
            {"12345","12.345k"},{"-1200000","-1.2M"},{"0.000012345","12.345u"},
            {"0","0"},{"1e18","1E"},{"1e21","1Z"},{"1e24","1Y"},{"1e27","1R"},{"1e30","1Q"},
            {"1e-21","1z"},{"1e-24","1y"},{"1e-27","1r"},{"1e-30","1q"},
            {"1e33","1000000000000000000000000000000000"},{"1e-33","0.000000000000000000000000000000001"}}) {
            auto result = e.process(input);
            check(!result.error && result.items.back().formatted == expected,"SI format: "+input);
            check(result.items.back().formatted.find('e') == std::string::npos,"eng never emits scientific notation");
            check(e.evaluate(expected) == e.evaluate(input),"SI output can be reused as input: "+expected);
        }
        check(e.evaluate("1E3") == Number(1000) && e.evaluate("1E-3") == Number("0.001"),"Uppercase exponent syntax preserved");
        e.execute("sigfigs 3");
        check(e.process("999.9").items.back().formatted == "1k","Rounding across prefix boundary");
        check(e.preview("0.0009999").items.back().formatted == "1m","SI preview rounding");
        e.execute("format auto; sigfigs 30");

        before = e.session(); auto preview = e.preview("x=7; x*6");
        check(!preview.error && *preview.items.back().value == Number(42),"Preview assignment and calculation");
        check(e.session() == before,"Preview does not commit any state");
        check(e.preview("base hex").items.empty(),"Commands have no numeric preview");
        check(!e.preview("exit").exit && !e.preview("clear").clear,"Preview has no UI control effects");
        check(e.preview("1+").error->code == ErrorCode::syntax,"Incomplete preview error");
        Engine clone = e; e.preview("rand(100)");
        check(e.evaluate("rand(100)") == clone.evaluate("rand(100)"),"Preview does not consume random sequence");
        e.execute("loop(x)=loop(x)");
        check(e.preview("loop(1)").error->code == ErrorCode::limit,"Preview recursion bounded");
        e.execute("branch(x)=branch(x)+branch(x)");
        check(e.preview("branch(1)").error->code == ErrorCode::limit,"Recursive preview is bounded");

        value(e,"0 degC -> degF","32"); value(e,"32 degF -> degC","0");
        value(e,"100 Celsius -> Fahrenheit","212"); value(e,"-40 degC -> degF","-40");
        value(e,"0 degC -> K","273.15"); value(e,"273.15 K -> degC","0");
        value(e,"9 deltaF -> deltaC","5"); value(e,"2 deltaC/s -> K/s","2");
        check(e.process("1 degC/s -> K/s").error.has_value(),"Offset compound unit rejected");
        check(e.process("1 degC^2 -> K^2").error.has_value(),"Offset powers rejected");
        check(e.process("unit wrong = 1 degC").error.has_value(),"Offset custom scale rejected");
        check(e.process("degC -> degF").error.has_value(),"Offset factor needs explicit quantity");
        r = e.process("units temperature");
        check(r.items.size() >= 9,"Temperature unit discovery");
        bool celsius = false;
        for (const auto& item : r.items) if (item.name == "degC") celsius = item.unit && item.unit->offset == Number("273.15");
        check(celsius,"Structured unit offset and dimensions");
        e.execute("unit furlong = 201.168 m");
        check(completes(e,"1 m -> furl","furlong"),"Custom unit completion");

        for (auto bits : {8,16,32,64}) {
            e.execute("programmer "+std::to_string(bits)+" unsigned");
            mpz_class modulus = mpz_class(1) << bits;
            value(e,"~0",mpz_class(modulus-1).get_str());
            value(e,"ans+1","0");
            value(e,"0-1",mpz_class(modulus-1).get_str());
            value(e,"ans>>1",mpz_class((modulus-1)/2).get_str());
            e.execute("programmer "+std::to_string(bits)+" signed");
            value(e,"~0","-1"); value(e,"-4>>1","-2");
            value(e,mpz_class(modulus/2-1).get_str()+"+1",mpz_class(-modulus/2).get_str());
            value(e,"7/2","3"); value(e,"-7/2","-3");
            mpz_class power, three = 3, hundred = 100;
            mpz_powm(power.get_mpz_t(),three.get_mpz_t(),hundred.get_mpz_t(),modulus.get_mpz_t());
            if (power >= modulus/2) power -= modulus;
            value(e,"3^100",power.get_str());
        }
        e.execute("programmer 8 unsigned"); value(e,"3^10","169"); value(e,"pow(3,10)","169"); value(e,"6!","208");
        e.execute("programmer 8 signed"); value(e,"255","-1");
        r = e.process("bases");
        check(r.output == std::vector<std::string>{"dec = -1","hex = 0xFF","bin = 0b11111111"},"Three bases with signed decimal and padded bit patterns");
        check(e.process("1<<8").error.has_value(),"Shift count bounded by word width");
        check(e.process("1.5+1").error.has_value(),"Programmer arithmetic rejects fractions");
        check(e.process("programmer 7").error.has_value(),"Unsupported word width rejected");
        e.execute("programmer off"); value(e,"7/2","3.5");

        e.execute("x=1/7; pi_copy=pi; negative=-0.1; zero=0; mode deg; sigfigs 75; base hex; format eng; preview off; programmer 32 unsigned");
        auto snapshot = e.session(); Engine restored; restored.restore_session(snapshot);
        check(restored.session() == snapshot,"Exact full-session round trip");
        check(restored.variables().at("x") == e.variables().at("x"),"No precision loss across restore");
        check(restored.variables().at("ans") == e.variables().at("ans"),"ans survives variable validation");
        check(restored.settings().word_bits == 32 && !restored.settings().signed_words && !restored.settings().preview,"Programmer/preview settings restored");
        auto invalid = snapshot; invalid.replace(0,17,"clicalc-session 2");
        bool rejected = false; try { restored.restore_session(invalid); } catch (const CalcError& error) { rejected = error.diagnostic.code == ErrorCode::invalid_state; }
        check(rejected && restored.session() == snapshot,"Invalid restore is atomic");
        std::cout << checks << " feature checks passed.\n";
    } catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}
