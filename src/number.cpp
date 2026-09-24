#include "clicalc/number.hpp"
#include <algorithm>
#include <cstdlib>
#include <cctype>
#include <map>
#include <stdexcept>
#include <utility>

namespace clicalc {
Number::Number() { mpfr_init2(value_, precision); mpfr_set_zero(value_, 0); }
Number::Number(long n) : Number() { mpfr_set_si(value_, n, MPFR_RNDN); }
Number::Number(const std::string& s, int base) : Number() {
    if (mpfr_set_str(value_, s.c_str(), base, MPFR_RNDN) || !mpfr_number_p(value_))
        throw CalcError(ErrorCode::syntax, "Invalid number: " + s);
}
Number::Number(const Number& n) : Number() { mpfr_set(value_, n.value_, MPFR_RNDN); }
Number::Number(Number&& n) noexcept : Number() { mpfr_swap(value_, n.value_); }
Number& Number::operator=(Number n) { mpfr_swap(value_, n.value_); return *this; }
Number::~Number() { mpfr_clear(value_); }
bool Number::zero() const { return mpfr_zero_p(value_); }
bool Number::integer() const { return mpfr_integer_p(value_); }
std::string Number::serialize() const {
    mpfr_exp_t exponent;
    char* raw = mpfr_get_str(nullptr, &exponent, 16, 0, value_, MPFR_RNDN);
    std::string digits(raw); mpfr_free_str(raw);
    auto count = digits.size() - (digits.front() == '-' ? 1 : 0);
    return digits + "@" + std::to_string(exponent - static_cast<mpfr_exp_t>(count));
}
mpz_class Number::as_integer() const {
    if (!integer()) throw CalcError(ErrorCode::domain, "This operation requires an integer.");
    if (!zero() && mpfr_get_exp(value_) > 65536)
        throw CalcError(ErrorCode::limit, "Integer exceeds the 65536-bit operation limit.");
    mpz_class n; mpfr_get_z(n.get_mpz_t(), value_, MPFR_RNDN); return n;
}
Number checked(Number n) {
    if (!mpfr_number_p(n.data())) throw CalcError(ErrorCode::domain, "Result is outside the real-number domain or numeric range.");
    return n;
}
#define BINARY(OP, FN) Number operator OP(const Number& a, const Number& b) { \
    Number n; FN(n.data(), a.data(), b.data(), MPFR_RNDN); return checked(n); }
BINARY(+, mpfr_add)
BINARY(-, mpfr_sub)
BINARY(*, mpfr_mul)
Number operator/(const Number& a, const Number& b) {
    if (b.zero()) throw CalcError(ErrorCode::division_by_zero, "Division by zero.");
    Number n; mpfr_div(n.data(), a.data(), b.data(), MPFR_RNDN); return checked(n);
}
Number operator-(const Number& a) { Number n; mpfr_neg(n.data(), a.data(), MPFR_RNDN); return n; }
bool operator==(const Number& a, const Number& b) { return mpfr_equal_p(a.data(), b.data()); }
bool operator<(const Number& a, const Number& b) { return mpfr_less_p(a.data(), b.data()); }

static std::string printed(mpfr_srcptr n, const char* spec, unsigned digits) {
    char* buffer = nullptr;
    int length = mpfr_asprintf(&buffer, spec, static_cast<int>(digits), n);
    if (length < 0 || !buffer) throw CalcError(ErrorCode::domain, "Could not format result.");
    std::string result(buffer); mpfr_free_str(buffer); return result;
}
std::string Number::format(int base, unsigned digits, const std::string& style) const {
    if (base != 2 && base != 10 && base != 16) throw CalcError(ErrorCode::domain, "Base must be 2, 10, or 16.");
    if (digits < 1 || digits > 100) throw CalcError(ErrorCode::domain, "Significant figures must be between 1 and 100.");
    if (zero()) return style == "finance" ? printed(value_, "%.*Rf", digits) : "0";
    if (base != 10) {
        // MPFR values are binary rationals: enough digits give an exact finite expansion.
        mpfr_exp_t exponent;
        char* raw = mpfr_get_str(nullptr, &exponent, base, 0, value_, MPFR_RNDN);
        std::string s(raw); mpfr_free_str(raw);
        bool negative = s.front() == '-'; if (negative) s.erase(0, 1);
        if (std::abs(exponent) > 65536) throw CalcError(ErrorCode::domain, "Base output exceeds the 65536-digit display limit.");
        while (s.size() > 1 && s.back() == '0') s.pop_back();
        if (exponent <= 0) s = "0." + std::string(static_cast<size_t>(-exponent), '0') + s;
        else if (static_cast<size_t>(exponent) >= s.size()) s.append(static_cast<size_t>(exponent) - s.size(), '0');
        else s.insert(static_cast<size_t>(exponent), ".");
        if (base == 16) std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::toupper(c); });
        return (negative ? "-" : "") + std::string(base == 2 ? "0b" : "0x") + s;
    }
    if (style == "finance" || style == "never") {
        if (mpfr_get_exp(value_) > 200000 || mpfr_get_exp(value_) < -200000)
            throw CalcError(ErrorCode::domain, "Fixed output is too large; use format auto.");
        if (style == "finance") return printed(value_, "%.*Rf", digits);
        auto s = printed(value_, "%.*Re", digits - 1);
        auto pos = s.find('e'); int exponent = std::stoi(s.substr(pos + 1));
        auto result = printed(value_, "%.*Rf", static_cast<unsigned>(std::max(0, static_cast<int>(digits) - 1 - exponent)));
        if (result.find('.') != std::string::npos) { while (result.back() == '0') result.pop_back(); if (result.back() == '.') result.pop_back(); }
        return result;
    }
    if (style == "always") return printed(value_, "%.*Re", digits - 1);
    if (style == "eng" || style == "prefix") {
        auto s = printed(value_, "%.*Re", digits - 1);
        int exponent = std::stoi(s.substr(s.find('e') + 1));
        int eng = exponent >= 0 ? exponent / 3 * 3 : (exponent - 2) / 3 * 3;
        const std::map<int, std::string> prefixes{{-30,"q"},{-27,"r"},{-24,"y"},{-21,"z"},{-18,"a"},{-15,"f"},{-12,"p"},{-9,"n"},{-6,"u"},{-3,"m"},{0,""},{3,"k"},{6,"M"},{9,"G"},{12,"T"},{15,"P"},{18,"E"},{21,"Z"},{24,"Y"},{27,"R"},{30,"Q"}};
        // Even outside the SI prefix range, eng never falls back to e notation.
        if (!prefixes.count(eng)) return format(10,digits,"never");
        Number scale("1e" + std::to_string(eng));
        auto mantissa = (*this / scale).format(10, digits, "never");
        return mantissa + prefixes.at(eng);
    }
    return printed(value_, "%.*Rg", digits);
}
}
