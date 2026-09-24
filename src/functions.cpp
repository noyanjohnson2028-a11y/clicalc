#include "clicalc/engine.hpp"
#include <algorithm>
#include <cctype>
#include <random>
#include <stdexcept>

namespace clicalc {
Number Engine::call(const std::string& name, const std::vector<Number>& args, unsigned depth) const {
    auto user = functions_.find(name);
    if (user != functions_.end()) {
        if (args.size() != user->second.parameters.size()) throw CalcError(ErrorCode::argument, "Wrong number of arguments to " + name);
        std::map<std::string, Number> locals;
        for (size_t i=0;i<args.size();++i) locals[user->second.parameters[i]] = args[i];
        return eval(user->second.expression,locals,depth+1);
    }
    std::string key = name;
    std::transform(key.begin(),key.end(),key.begin(),[](unsigned char c) { return std::tolower(c); });
    using Unary = int (*)(mpfr_ptr,mpfr_srcptr,mpfr_rnd_t);
    static const std::map<std::string, Unary> unary{
        {"sqrt",mpfr_sqrt},{"ln",mpfr_log},{"log",mpfr_log10},{"exp",mpfr_exp},
        {"sin",mpfr_sin},{"cos",mpfr_cos},{"tan",mpfr_tan},{"asin",mpfr_asin},{"acos",mpfr_acos},{"atan",mpfr_atan},
        {"sinh",mpfr_sinh},{"cosh",mpfr_cosh},{"tanh",mpfr_tanh},{"abs",mpfr_abs}};
    auto arity = [&](size_t n) { if (args.size() != n) throw CalcError(ErrorCode::argument, name + " expects " + std::to_string(n) + " argument(s)."); };
    Number result;
    if (unary.count(key)) {
        arity(1); Number x = args[0];
        if (degrees_ && (key == "sin" || key == "cos" || key == "tan")) {
            // Reduce in degrees before introducing the approximation of pi.
            // Exact quarter turns must stay exact; do not snap nearby angles.
            Number turn(360);
            mpfr_remainder(x.data(),x.data(),turn.data(),MPFR_RNDN);
            if (x == Number(0) || x == Number(90) || x == Number(-90) ||
                x == Number(180) || x == Number(-180)) {
                const bool vertical = x == Number(90) || x == Number(-90);
                if (key == "sin") return vertical ? Number(x == Number(90) ? 1 : -1) : Number(0);
                if (key == "cos") return vertical ? Number(0) : Number(x.zero() ? 1 : -1);
                if (vertical) throw CalcError(ErrorCode::domain,"Tangent is undefined at odd multiples of 90 degrees.");
                return Number(0);
            }
            x = x*variables_.at("pi")/Number(180);
        }
        unary.at(key)(result.data(),x.data(),MPFR_RNDN);
        if (degrees_ && (key == "asin" || key == "acos" || key == "atan")) result = result*Number(180)/variables_.at("pi");
    } else if (key == "round" || key == "ceil" || key == "floor" || key == "trunc") {
        arity(1);
        if (key == "round") mpfr_round(result.data(),args[0].data());
        if (key == "ceil") mpfr_ceil(result.data(),args[0].data());
        if (key == "floor") mpfr_floor(result.data(),args[0].data());
        if (key == "trunc") mpfr_trunc(result.data(),args[0].data());
    } else if (key == "mod") {
        arity(2); if (args[1].zero()) throw CalcError(ErrorCode::division_by_zero, "Modulo by zero.");
        mpfr_fmod(result.data(),args[0].data(),args[1].data(),MPFR_RNDN);
    } else if (key == "min" || key == "max") {
        arity(2); result = (args[0] < args[1]) == (key == "min") ? args[0] : args[1];
    } else if (key == "pow") {
        arity(2);
        if (word_bits_) {
            auto x = args[0].as_integer(), y = args[1].as_integer(); mpz_class n, modulus = mpz_class(1) << word_bits_;
            if (y < 0) throw CalcError(ErrorCode::domain,"Programmer powers require a nonnegative exponent.");
            mpz_powm(n.get_mpz_t(),x.get_mpz_t(),y.get_mpz_t(),modulus.get_mpz_t()); mpfr_set_z(result.data(),n.get_mpz_t(),MPFR_RNDN);
        } else mpfr_pow(result.data(),args[0].data(),args[1].data(),MPFR_RNDN);
    } else if (key == "atan2") {
        arity(2); mpfr_atan2(result.data(),args[0].data(),args[1].data(),MPFR_RNDN);
        if (degrees_) result = result*Number(180)/variables_.at("pi");
    } else if (key == "rand") {
        arity(1); if (!(Number(0) < args[0])) throw CalcError(ErrorCode::domain, "rand maximum must be positive.");
        result = Number(std::to_string(random_())); mpfr_div_2ui(result.data(),result.data(),64,MPFR_RNDN);
        result = result * args[0];
    } else if (key.size() > 3 && key.substr(0,3) == "log" &&
               std::all_of(key.begin()+3,key.end(),[](unsigned char c){return std::isdigit(c);})) {
        arity(1); Number base(key.substr(3)), denominator;
        if (!(Number(1) < base)) throw CalcError(ErrorCode::domain, "Logarithm base must be greater than 1.");
        mpfr_log(result.data(),args[0].data(),MPFR_RNDN); mpfr_log(denominator.data(),base.data(),MPFR_RNDN);
        result = checked(result)/denominator;
    } else throw CalcError(ErrorCode::unknown_name, "Unknown function '"+name+"'.");
    return checked(result);
}
}
