#include "test_support.hpp"

#include "pseudo/diagnostics/diagnostic_engine.hpp"
#include "pseudo/source/source_manager.hpp"

#include <cstddef>
#include <sstream>
#include <string>

namespace {

using tpp::DiagnosticEngine;
using tpp::SourceManager;
using tpp::SourceSpan;

std::string render(
    const DiagnosticEngine& diagnostics,
    const SourceManager& sources)
{
    std::ostringstream output;
    tpp::render_diagnostics(output, diagnostics.diagnostics(), sources);
    return output.str();
}

void warning_does_not_increase_error_count()
{
    SourceManager sources;
    const auto source = sources.add_source("sample.tpp", "value");
    DiagnosticEngine diagnostics;

    diagnostics.warning(SourceSpan{source, 0, 5}, "unused value");

    TPP_CHECK_EQ(diagnostics.error_count(), std::size_t{0});
    TPP_CHECK(!diagnostics.has_errors());
    TPP_CHECK_EQ(diagnostics.diagnostics().size(), std::size_t{1});
}

void error_increases_error_count()
{
    DiagnosticEngine diagnostics;

    diagnostics.error("compilation failed");

    TPP_CHECK_EQ(diagnostics.error_count(), std::size_t{1});
    TPP_CHECK(diagnostics.has_errors());
    TPP_CHECK_EQ(diagnostics.diagnostics().size(), std::size_t{1});
}

void has_errors_tracks_reported_severity()
{
    SourceManager sources;
    const auto source = sources.add_source("sample.tpp", "value");
    DiagnosticEngine diagnostics;

    TPP_CHECK(!diagnostics.has_errors());
    diagnostics.warning(SourceSpan{source, 0, 5}, "unused value");
    TPP_CHECK(!diagnostics.has_errors());
    diagnostics.error(SourceSpan{source, 0, 5}, "invalid value");
    TPP_CHECK(diagnostics.has_errors());
}

void source_less_diagnostic_has_a_stable_format()
{
    SourceManager sources;
    DiagnosticEngine diagnostics;
    diagnostics.error("cannot open input");

    TPP_CHECK(!diagnostics.diagnostics().front().primary_span.has_value());
    TPP_CHECK_EQ(
        render(diagnostics, sources),
        std::string("pseudo: error: cannot open input\n"));
}

void diagnostic_position_is_derived_from_its_span()
{
    SourceManager sources;
    const auto source = sources.add_source("sample.tpp", "a\nbc\n");
    DiagnosticEngine diagnostics;
    diagnostics.error(SourceSpan{source, 3, 4}, "expected expression");

    const auto output = render(diagnostics, sources);
    tpp::test::check_contains(
        output,
        "sample.tpp:2:2: error: expected expression\n");
    tpp::test::check_contains(output, "  2 | bc\n");
}

void zero_length_span_renders_one_caret()
{
    SourceManager sources;
    const auto source = sources.add_source("sample.tpp", "a\nbc\n");
    DiagnosticEngine diagnostics;
    diagnostics.error(SourceSpan{source, 3, 3}, "expected expression");

    TPP_CHECK_EQ(
        render(diagnostics, sources),
        std::string(
            "sample.tpp:2:2: error: expected expression\n"
            "  2 | bc\n"
            "    |  ^\n"));
}

void rendered_diagnostic_format_is_stable()
{
    SourceManager sources;
    const auto source = sources.add_source("sample.tpp", "a\nbc\n");
    DiagnosticEngine diagnostics;
    diagnostics.error(SourceSpan{source, 2, 4}, "expected expression");

    TPP_CHECK_EQ(
        render(diagnostics, sources),
        std::string(
            "sample.tpp:2:1: error: expected expression\n"
            "  2 | bc\n"
            "    | ^~\n"));
}

}

int main()
{
    return tpp::test::run({
        {"warning does not increase error count", warning_does_not_increase_error_count},
        {"error increases error count", error_increases_error_count},
        {"has_errors tracks reported severity", has_errors_tracks_reported_severity},
        {"source-less diagnostic has a stable format", source_less_diagnostic_has_a_stable_format},
        {"diagnostic position is derived from its span", diagnostic_position_is_derived_from_its_span},
        {"zero-length span renders one caret", zero_length_span_renders_one_caret},
        {"rendered diagnostic format is stable", rendered_diagnostic_format_is_stable},
    });
}
