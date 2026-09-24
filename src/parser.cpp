#include "clicalc/engine.hpp"
#include <cctype>
#include <set>
#include <stdexcept>

namespace clicalc {
namespace {
struct Token { std::string text; size_t position; enum Kind { End, Numeric, Name, Symbol } kind; };
const std::map<std::string, int> prefixes{{"Q",30},{"R",27},{"Y",24},{"Z",21},{"E",18},{"P",15},{"T",12},{"G",9},{"M",6},{"k",3},{"c",-2},{"m",-3},{"u",-6},{"n",-9},{"p",-12},{"f",-15},{"a",-18},{"z",-21},{"y",-24},{"r",-27},{"q",-30}};
std::vector<Token> lex(const std::string& s) {
    if (s.size() > 16384) throw CalcError(ErrorCode::limit,"Expression exceeds 16384 characters.");
    std::vector<Token> tokens;
    size_t i = 0;
    while (i < s.size()) {
        unsigned char c = s[i];
        if (std::isspace(c)) { ++i; continue; }
        size_t start = i;
        if (std::isdigit(c) || c == '.') {
            int base = 10;
            if (c == '0' && i + 1 < s.size()) {
                if (s[i+1] == 'x' || s[i+1] == 'X') base = 16;
                if (s[i+1] == 'b' || s[i+1] == 'B') base = 2;
            }
            if (base != 10) i += 2;
            bool digit = false, point = false;
            while (i < s.size()) {
                unsigned char ch = s[i];
                if ((base == 16 && std::isxdigit(ch)) || (base == 10 && std::isdigit(ch)) || (base == 2 && (ch == '0' || ch == '1'))) { digit = true; ++i; }
                else if (ch == '.' && !point) { point = true; ++i; }
                else break;
            }
            if (!digit) throw CalcError(ErrorCode::syntax,"Expected digits.",SourceSpan{start,i-start});
            bool exponent_marker = i < s.size() && (s[i] == 'e' ||
                (s[i] == 'E' && i+1 < s.size() && (std::isdigit(static_cast<unsigned char>(s[i+1])) || s[i+1] == '+' || s[i+1] == '-')));
            if (base == 10 && exponent_marker) {
                ++i; if (i < s.size() && (s[i] == '+' || s[i] == '-')) ++i;
                size_t expstart = i; while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) ++i;
                if (expstart == i) throw CalcError(ErrorCode::syntax,"Expected exponent digits.",SourceSpan{i,0});
            }
            if (base == 10 && i < s.size() && prefixes.count(s.substr(i,1))) ++i;
            tokens.push_back({s.substr(start, i-start), start, Token::Numeric});
        } else if (std::isalpha(c) || c == '_') {
            ++i; while (i < s.size() && (std::isalnum(static_cast<unsigned char>(s[i])) || s[i] == '_')) ++i;
            tokens.push_back({s.substr(start, i-start), start, Token::Name});
        } else {
            static const std::set<std::string> pairs{"<<",">>","<=",">=","==","!=","&&","||"};
            if (pairs.count(s.substr(i,2))) i += 2; else ++i;
            tokens.push_back({s.substr(start,i-start),start,Token::Symbol});
        }
    }
    tokens.push_back({"",s.size(),Token::End}); return tokens;
}
int precedence(const std::string& op) {
    if (op == "||") return 1;
    if (op == "&&") return 2;
    if (op == "|") return 3;
    if (op == "@") return 4;
    if (op == "&") return 5;
    if (op == "==" || op == "!=") return 6;
    if (op == "<" || op == "<=" || op == ">" || op == ">=") return 7;
    if (op == "<<" || op == ">>") return 8;
    if (op == "+" || op == "-") return 9;
    if (op == "*" || op == "/" || op == "%") return 10;
    if (op == "^") return 12;
    return 0;
}
Number from_integer(const mpz_class& n) { Number result; mpfr_set_z(result.data(),n.get_mpz_t(),MPFR_RNDN); return checked(result); }
Number binary(const std::string& op, const Number& a, const Number& b, unsigned bits) {
    if (op == "+") return a+b;
    if (op == "-") return a-b;
    if (op == "*") return a*b;
    if (op == "/") {
        if (b.zero()) throw CalcError(ErrorCode::division_by_zero,"Division by zero.");
        if (bits) return from_integer(a.as_integer()/b.as_integer());
        return a/b;
    }
    if (op == "^") {
        if (bits) {
            auto x = a.as_integer(), y = b.as_integer(); mpz_class n, modulus = mpz_class(1) << bits;
            if (y < 0) throw CalcError(ErrorCode::domain,"Programmer powers require a nonnegative exponent.");
            mpz_powm(n.get_mpz_t(),x.get_mpz_t(),y.get_mpz_t(),modulus.get_mpz_t()); return from_integer(n);
        }
        Number n; mpfr_pow(n.data(),a.data(),b.data(),MPFR_RNDN); return checked(n);
    }
    if (op == "==") return Number(a == b);
    if (op == "!=") return Number(!(a == b));
    if (op == "<") return Number(a < b);
    if (op == "<=") return Number(a < b || a == b);
    if (op == ">") return Number(b < a);
    if (op == ">=") return Number(b < a || a == b);
    if (op == "&&") return Number(!a.zero() && !b.zero());
    if (op == "||") return Number(!a.zero() || !b.zero());
    auto x = a.as_integer(), y = b.as_integer();
    if (op == "%") { if (y == 0) throw CalcError(ErrorCode::division_by_zero,"Modulo by zero."); return from_integer(x % y); }
    if (op == "&") return from_integer(x & y);
    if (op == "|") return from_integer(x | y);
    if (op == "@") return from_integer(x ^ y);
    if (y < 0 || y > 65536 || (bits && y >= bits)) throw CalcError(ErrorCode::domain,bits ? "Shift count must be smaller than the word width and nonnegative." : "Shift count must be between 0 and 65536.");
    if (op == "<<") { x <<= y.get_ui(); return from_integer(x); }
    x >>= y.get_ui(); return from_integer(x);
}
}
class Parser {
    const Engine& engine_;
    const std::map<std::string, Number>& locals_;
    std::vector<Token> tokens_;
    size_t index_ = 0;
    unsigned calls_, nesting_ = 0;
    const Token& peek() const { return tokens_[index_]; }
    Token take() { return tokens_[index_++]; }
    bool accept(const std::string& s) { if (peek().text != s) return false; ++index_; return true; }
    [[noreturn]] void error(const std::string& message) const {
        throw CalcError(ErrorCode::syntax,message,SourceSpan{peek().position,peek().text.size()});
    }
    void expect(const std::string& s) { if (!accept(s)) error("Expected '"+s+"'"); }
    Number expression(int minimum, bool active) {
        auto initial = peek();
        if (++nesting_ > 128) throw CalcError(ErrorCode::limit,"Expression nesting exceeds 128.",SourceSpan{initial.position,initial.text.size()});
        if (++engine_.evaluation_steps_ > (engine_.previewing_ ? 2000u : 20000u))
            throw CalcError(ErrorCode::limit,"Evaluation step limit exceeded.",SourceSpan{initial.position,initial.text.size()});
        try {
        Number left;
        if (accept("+")) left = expression(11,active);
        else if (accept("-")) left = -expression(11,active);
        else if (accept("!")) left = Number(expression(11,active).zero());
        else if (accept("~")) { auto n = expression(11,active); if (active) left = from_integer(~n.as_integer()); }
        else if (peek().text == "(" || peek().text == "[") {
            auto end = take().text == "(" ? ")" : "]";
            left = expression(1,active); expect(end);
        } else if (peek().kind == Token::Numeric) {
            std::string s = take().text;
            if (active) {
                if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X' || s[1] == 'b' || s[1] == 'B')) left = Number(s.substr(2),std::tolower(s[1]) == 'x' ? 16 : 2);
                else {
                    auto suffix = s.substr(s.size()-1);
                    if (prefixes.count(suffix)) { s.pop_back(); left = Number(s) * Number("1e" + std::to_string(prefixes.at(suffix))); }
                    else left = Number(s);
                }
            }
        } else if (peek().kind == Token::Name) {
            auto token = take(); auto name = token.text;
            if (accept("(")) {
                std::vector<Number> args;
                if (!accept(")")) { do { args.push_back(expression(1,active)); } while (accept(",")); expect(")"); }
                if (active) {
                    try { left = engine_.call(name,args,calls_); }
                    catch (const CalcError& error) {
                        auto diagnostic = error.diagnostic;
                        if (engine_.functions_.count(name)) diagnostic.context.push_back("In function "+name);
                        diagnostic.span = SourceSpan{token.position,name.size()};
                        throw CalcError(std::move(diagnostic));
                    }
                }
            } else if (active) {
                if (locals_.count(name)) left = locals_.at(name);
                else if (engine_.variables_.count(name)) left = engine_.variables_.at(name);
                else throw CalcError(ErrorCode::unknown_name,"Unknown variable '"+name+"'.",SourceSpan{token.position,name.size()});
            }
        } else error("Expected a number, variable, or '('");
        if (active) left = engine_.normalize(left);
        while (true) {
            if (peek().text == "!" && 13 >= minimum) {
                auto token = take();
                if (active) {
                    auto n = left.as_integer();
                    if (n < 0 || n > 10000) throw CalcError(ErrorCode::domain,"Factorial requires an integer between 0 and 10000.",SourceSpan{token.position,1});
                    if (engine_.word_bits_) {
                        mpz_class value = 1, modulus = mpz_class(1) << engine_.word_bits_;
                        for (unsigned long i=2;i<=n.get_ui();++i) { value *= i; value %= modulus; }
                        left = engine_.normalize(from_integer(value));
                    } else { mpfr_fac_ui(left.data(),n.get_ui(),MPFR_RNDN); left = checked(left); }
                }
                continue;
            }
            int p = precedence(peek().text); if (!p || p < minimum) break;
            auto token = take(); auto op = token.text;
            bool right_active = active && !(op == "&&" && left.zero()) && !(op == "||" && !left.zero());
            auto right = expression(p + (op == "^" ? 0 : 1),right_active);
            if (active) {
                try { left = engine_.normalize(binary(op,left,right,engine_.word_bits_)); }
                catch (const CalcError& error) { auto diagnostic = error.diagnostic; diagnostic.span = SourceSpan{token.position,op.size()}; throw CalcError(std::move(diagnostic)); }
            }
        }
        --nesting_; return left;
        } catch (const CalcError& error) {
            auto diagnostic = error.diagnostic;
            if (!diagnostic.span) diagnostic.span = SourceSpan{initial.position,initial.text.size()};
            throw CalcError(std::move(diagnostic));
        }
    }
public:
    Parser(const Engine& e, const std::string& s, const std::map<std::string, Number>& locals, unsigned calls)
        : engine_(e), locals_(locals), tokens_(lex(s)), calls_(calls) { if (!calls) engine_.evaluation_steps_ = 0; }
    Number run(bool active = true) { auto result = expression(1,active); if (peek().kind != Token::End) error("Unexpected token '"+peek().text+"'"); return result; }
};
Number Engine::eval(const std::string& s, const std::map<std::string, Number>& locals, unsigned depth) const {
    if (depth > 64) throw CalcError(ErrorCode::limit,"Function recursion exceeds 64 calls.");
    return Parser(*this,s,locals,depth).run();
}
Number Engine::evaluate(const std::string& s) const {
    evaluation_steps_ = 0;
    if (s.size() > 16384) throw CalcError(ErrorCode::limit,"Expression exceeds 16384 characters.");
    return normalize(s.find("->") != std::string::npos ? conversion(s) : eval(s,{},0));
}
// Validate grammar at definition time, without resolving variables or executing functions.
void validate_expression(const Engine& engine, const std::string& expression) { Parser(engine,expression,{},0).run(false); }
}
