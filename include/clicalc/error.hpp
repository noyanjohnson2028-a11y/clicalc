#pragma once
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace clicalc {
enum class ErrorCode { syntax, unknown_name, argument, domain, division_by_zero,
                       incompatible_units, limit, invalid_state, internal };
// Zero-based UTF-8 byte offsets into the submitted line. Length zero means EOF.
struct SourceSpan { size_t offset = 0; size_t length = 0; };
struct Diagnostic {
    ErrorCode code;
    std::string message;
    std::optional<SourceSpan> span;
    std::vector<std::string> context;
};
class CalcError : public std::runtime_error {
public:
    Diagnostic diagnostic;
    CalcError(ErrorCode code, std::string message, std::optional<SourceSpan> span = {})
        : std::runtime_error(message), diagnostic{code, std::move(message), span, {}} {}
    explicit CalcError(Diagnostic value)
        : std::runtime_error(value.message), diagnostic(std::move(value)) {}
};
}
