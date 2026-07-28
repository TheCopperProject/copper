#pragma once

#include "span.hpp"
#include <string>
#include <vector>
#include <cstdint>

enum class Severity
{
    ERROR,
    WARNING,
    INFO,
    HINT,
};

struct Diagnostic
{
    Severity severity = Severity::ERROR;
    std::string message;
    Span span;

    std::string suggestion;
    bool has_suggestion = false;
};

class ErrorCollector
{
private:
    std::vector<Diagnostic> diagnostics_;

public:
    std::string_view src_name;

    ErrorCollector(std::string_view src_name) : src_name(src_name) {};

    void error(const std::string& message, const Span& span)
    {
        diagnostics_.push_back({ Severity::ERROR, message, span, "", false });
    }

    void error(const std::string& message, const Span& span, const std::string& suggestion)
    {
        diagnostics_.push_back({ Severity::ERROR, message, span, suggestion, true });
    }

    void warning(const std::string& message, const Span& span)
    {
        diagnostics_.push_back({ Severity::WARNING, message, span, "", false });
    }

    void info(const std::string& message, const Span& span)
    {
        diagnostics_.push_back({ Severity::INFO, message, span, "", false });
    }

    void hint(const std::string& message, const Span& span)
    {
        diagnostics_.push_back({ Severity::HINT, message, span, "", false });
    }

    bool hasErrors() const
    {
        for (const auto& d : diagnostics_)
            if (d.severity == Severity::ERROR)
                return true;
        return false;
    }

    std::size_t count() const { return diagnostics_.size(); }

    const std::vector<Diagnostic>& all() const { return diagnostics_; }

    std::vector<Diagnostic> forFile(uint32_t file_id) const
    {
        std::vector<Diagnostic> out;
        for (const auto& d : diagnostics_)
            if (d.span.file_id == file_id)
                out.push_back(d);
        return out;
    }

    void clear() { diagnostics_.clear(); }

    void merge(const ErrorCollector& other)
    {
        diagnostics_.insert(diagnostics_.end(), other.diagnostics_.begin(), other.diagnostics_.end());
    }
};