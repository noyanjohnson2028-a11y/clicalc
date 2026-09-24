#pragma once
#include <mpfr.h>
#include <gmpxx.h>
#include <string>
#include "clicalc/error.hpp"

namespace clicalc {
// 384 binary bits (~115 decimal digits), with round-to-nearest operations.
class Number {
public:
    static constexpr mpfr_prec_t precision = 384;
    Number();
    Number(long value);
    explicit Number(const std::string& text, int base = 10);
    Number(const Number& other);
    Number(Number&& other) noexcept;
    Number& operator=(Number other);
    ~Number();
    mpfr_ptr data() { return value_; }
    mpfr_srcptr data() const { return value_; }
    bool zero() const;
    bool integer() const;
    mpz_class as_integer() const;
    std::string serialize() const;
    std::string format(int base = 10, unsigned digits = 30,
                       const std::string& style = "auto") const;
private:
    mpfr_t value_;
};
Number operator+(const Number&, const Number&);
Number operator-(const Number&, const Number&);
Number operator*(const Number&, const Number&);
Number operator/(const Number&, const Number&);
Number operator-(const Number&);
bool operator==(const Number&, const Number&);
bool operator<(const Number&, const Number&);
Number checked(Number value);
}
