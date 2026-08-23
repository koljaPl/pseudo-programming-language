#include "test_support.hpp"

#include "pseudo/ast/printer.hpp"
#include "pseudo/codegen/cpp_generator.hpp"
#include "pseudo/driver/compilation_session.hpp"
#include "pseudo/driver/compiler.hpp"
#include "pseudo/lowering/lowerer.hpp"

#include <filesystem>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#ifndef TPP_TEST_DATA_DIR
#error "TPP_TEST_DATA_DIR must point to the tests/data directory"
#endif

namespace {

std::filesystem::path data_path(const std::string_view filename)
{
    return std::filesystem::path{TPP_TEST_DATA_DIR} / filename;
}

std::optional<std::string> lower_and_generate(
    tpp::CompilationSession& session)
{
    TPP_CHECK(session.program().has_value());
    auto lowered = tpp::lower_program(
        *session.program(),
        tpp::LoweringContext{
            .types = session.types(),
            .symbols = session.symbols(),
            .declarations = session.declarations(),
            .resolutions = session.resolutions(),
            .type_info = session.type_info(),
        },
        session.diagnostics());
    if (!lowered.has_value()) {
        return std::nullopt;
    }

    return tpp::generate_cpp(
        *lowered,
        session.types(),
        session.diagnostics());
}

struct TemporaryRecord {
    std::size_t id;
    tpp::TempRole role;
    tpp::SymbolId owner;

    bool operator==(const TemporaryRecord&) const = default;
};

void append_temporary(
    const tpp::LoweredTemporary& temporary,
    std::vector<TemporaryRecord>& records)
{
    records.push_back(TemporaryRecord{
        .id = temporary.id.value,
        .role = temporary.role,
        .owner = temporary.owner,
    });
}

void collect_temporaries(
    const tpp::LoweredBlock& block,
    std::vector<TemporaryRecord>& records)
{
    for (const auto& statement : block.statements) {
        if (const auto* nested = std::get_if<tpp::LoweredBlockStatement>(
                &statement.node)) {
            TPP_CHECK(nested->block != nullptr);
            collect_temporaries(*nested->block, records);
            continue;
        }

        if (const auto* range = std::get_if<tpp::LoweredRangeStatement>(
                &statement.node)) {
            append_temporary(range->begin_storage, records);
            append_temporary(range->end_storage, records);
            append_temporary(range->cursor_storage, records);
            if (range->active_storage.has_value()) {
                append_temporary(*range->active_storage, records);
            }
            TPP_CHECK(range->body != nullptr);
            collect_temporaries(*range->body, records);
            continue;
        }

        if (const auto* foreach = std::get_if<tpp::LoweredForEachStatement>(
                &statement.node)) {
            append_temporary(foreach->snapshot_storage, records);
            TPP_CHECK(foreach->body != nullptr);
            collect_temporaries(*foreach->body, records);
        }
    }
}

std::vector<TemporaryRecord> temporary_records(
    const tpp::LoweredProgram& program)
{
    std::vector<TemporaryRecord> records;
    for (const auto& function : program.functions) {
        TPP_CHECK(function.body != nullptr);
        collect_temporaries(*function.body, records);
    }
    return records;
}

std::string ast_text(const tpp::CompilationSession& session)
{
    TPP_CHECK(session.program().has_value());
    std::ostringstream output;
    tpp::print_ast(output, *session.program());
    return output.str();
}

bool has_diagnostic(
    const tpp::CompilationSession& session,
    const std::string_view fragment)
{
    for (const auto& diagnostic : session.diagnostics().diagnostics()) {
        if (diagnostic.message.find(fragment) != std::string::npos) {
            return true;
        }
    }
    return false;
}

void combined_supported_program_reaches_deterministic_cpp()
{
    const tpp::Compiler compiler;
    tpp::CompilationSession first;
    tpp::CompilationSession second;

    TPP_CHECK(compiler.compile(data_path("codegen_combined.tpp"), first));
    TPP_CHECK(!first.diagnostics().has_errors());
    const auto first_ast = ast_text(first);
    auto first_lowered = tpp::lower_program(
        *first.program(),
        tpp::LoweringContext{
            .types = first.types(),
            .symbols = first.symbols(),
            .declarations = first.declarations(),
            .resolutions = first.resolutions(),
            .type_info = first.type_info(),
        },
        first.diagnostics());
    TPP_CHECK(first_lowered.has_value());
    const auto first_temporaries = temporary_records(*first_lowered);
    const auto first_cpp = tpp::generate_cpp(
        *first_lowered,
        first.types(),
        first.diagnostics());
    TPP_CHECK(first_cpp.has_value());
    TPP_CHECK(!first_cpp->empty());
    TPP_CHECK(!first.diagnostics().has_errors());

    TPP_CHECK(compiler.compile(data_path("codegen_combined.tpp"), second));
    const auto second_ast = ast_text(second);
    auto second_lowered = tpp::lower_program(
        *second.program(),
        tpp::LoweringContext{
            .types = second.types(),
            .symbols = second.symbols(),
            .declarations = second.declarations(),
            .resolutions = second.resolutions(),
            .type_info = second.type_info(),
        },
        second.diagnostics());
    TPP_CHECK(second_lowered.has_value());
    const auto second_temporaries = temporary_records(*second_lowered);
    const auto second_cpp = tpp::generate_cpp(
        *second_lowered,
        second.types(),
        second.diagnostics());
    TPP_CHECK(second_cpp.has_value());
    TPP_CHECK_EQ(second_ast, first_ast);
    TPP_CHECK_EQ(second_temporaries, first_temporaries);
    TPP_CHECK_EQ(*second_cpp, *first_cpp);
    TPP_CHECK(second.diagnostics().diagnostics().empty());

    const std::vector<tpp::TempRole> expected_roles{
        tpp::TempRole::range_begin,
        tpp::TempRole::range_end,
        tpp::TempRole::range_cursor,
        tpp::TempRole::range_active,
        tpp::TempRole::range_begin,
        tpp::TempRole::range_end,
        tpp::TempRole::range_cursor,
        tpp::TempRole::range_begin,
        tpp::TempRole::range_end,
        tpp::TempRole::range_cursor,
        tpp::TempRole::range_active,
        tpp::TempRole::iterable_snapshot,
        tpp::TempRole::iterable_snapshot,
        tpp::TempRole::iterable_snapshot,
    };
    TPP_CHECK_EQ(first_temporaries.size(), expected_roles.size());
    for (std::size_t index = 0; index < expected_roles.size(); ++index) {
        TPP_CHECK_EQ(first_temporaries[index].id, index);
        TPP_CHECK_EQ(first_temporaries[index].role, expected_roles[index]);
    }

    TPP_CHECK(compiler.compile(data_path("codegen_combined.tpp"), first));
    const auto reused_ast = ast_text(first);
    auto reused_lowered = tpp::lower_program(
        *first.program(),
        tpp::LoweringContext{
            .types = first.types(),
            .symbols = first.symbols(),
            .declarations = first.declarations(),
            .resolutions = first.resolutions(),
            .type_info = first.type_info(),
        },
        first.diagnostics());
    TPP_CHECK(reused_lowered.has_value());
    const auto reused_temporaries = temporary_records(*reused_lowered);
    const auto reused_cpp = tpp::generate_cpp(
        *reused_lowered,
        first.types(),
        first.diagnostics());
    TPP_CHECK(reused_cpp.has_value());
    TPP_CHECK_EQ(reused_ast, first_ast);
    TPP_CHECK_EQ(reused_temporaries, first_temporaries);
    TPP_CHECK_EQ(*reused_cpp, *first_cpp);
    TPP_CHECK(first.diagnostics().diagnostics().empty());
}

void multiple_lexical_errors_are_ordered_and_gate_the_parser()
{
    tpp::CompilationSession session;
    const tpp::Compiler compiler;

    TPP_CHECK(!compiler.compile(
        data_path("multiple_lexical_errors.tpp"),
        session));
    TPP_CHECK_EQ(session.diagnostics().error_count(), std::size_t{4});
    TPP_CHECK(!session.program().has_value());
    TPP_CHECK(!session.tokens().empty());
    TPP_CHECK(session.declarations().empty());
    TPP_CHECK(session.resolutions().empty());
    TPP_CHECK(session.type_info().empty());

    const auto diagnostics = session.diagnostics().diagnostics();
    TPP_CHECK_EQ(
        diagnostics[0].message,
        std::string{"unexpected '&'; use '&&' for logical and"});
    TPP_CHECK_EQ(
        diagnostics[1].message,
        std::string{"unexpected '|'; use '||' for logical or"});
    TPP_CHECK_EQ(
        diagnostics[2].message,
        std::string{"unknown escape sequence '\\q'"});
    TPP_CHECK_EQ(diagnostics[3].message, diagnostics[2].message);
    for (std::size_t index = 1; index < diagnostics.size(); ++index) {
        TPP_CHECK(diagnostics[index - 1].primary_span.has_value());
        TPP_CHECK(diagnostics[index].primary_span.has_value());
        TPP_CHECK(
            diagnostics[index - 1].primary_span->begin
            < diagnostics[index].primary_span->begin);
    }
}

void multiple_syntax_errors_keep_a_recovered_program_and_gate_semantics()
{
    tpp::CompilationSession session;
    const tpp::Compiler compiler;

    TPP_CHECK(!compiler.compile(
        data_path("multiple_syntax_errors.tpp"),
        session));
    TPP_CHECK_EQ(session.diagnostics().error_count(), std::size_t{5});
    TPP_CHECK(session.program().has_value());
    TPP_CHECK_EQ(session.program()->declarations.size(), std::size_t{3});
    TPP_CHECK(session.declarations().empty());
    TPP_CHECK(session.resolutions().empty());
    TPP_CHECK(session.type_info().empty());

    const auto diagnostics = session.diagnostics().diagnostics();
    TPP_CHECK_EQ(diagnostics[0].message, std::string{"expected expression"});
    TPP_CHECK_EQ(diagnostics[1].message, diagnostics[0].message);
    TPP_CHECK_EQ(
        diagnostics[2].message,
        std::string{"expected ';' after break statement"});
    TPP_CHECK_EQ(
        diagnostics[3].message,
        std::string{"expected ';' after continue statement"});
    TPP_CHECK_EQ(diagnostics[4].message, diagnostics[0].message);
}

void semantic_failures_gate_later_passes_but_keep_prior_state()
{
    const tpp::Compiler compiler;
    tpp::CompilationSession session;

    TPP_CHECK(!compiler.compile(data_path("duplicate_declaration.tpp"), session));
    TPP_CHECK(!session.declarations().empty());
    TPP_CHECK(session.resolutions().empty());
    TPP_CHECK(session.type_info().empty());

    TPP_CHECK(!compiler.compile(data_path("unknown_name.tpp"), session));
    TPP_CHECK(!session.declarations().empty());
    TPP_CHECK(!session.resolutions().empty());
    TPP_CHECK(session.type_info().empty());

    TPP_CHECK(!compiler.compile(data_path("type_error.tpp"), session));
    TPP_CHECK(!session.declarations().empty());
    TPP_CHECK(!session.type_info().empty());

    TPP_CHECK(!compiler.compile(data_path("control_flow_error.tpp"), session));
    TPP_CHECK(!session.type_info().empty());
    TPP_CHECK(has_diagnostic(session, "only allowed inside a loop"));
}

void independent_semantic_error_corpus_has_stable_order()
{
    const tpp::Compiler compiler;
    tpp::CompilationSession session;

    TPP_CHECK(!compiler.compile(
        data_path("corpus/invalid/declaration/multiple_independent.tpp"),
        session));
    TPP_CHECK_EQ(session.diagnostics().error_count(), std::size_t{2});
    auto diagnostics = session.diagnostics().diagnostics();
    TPP_CHECK_EQ(diagnostics.size(), std::size_t{4});
    TPP_CHECK_EQ(diagnostics[0].severity, tpp::DiagnosticSeverity::error);
    TPP_CHECK_EQ(
        diagnostics[0].message,
        std::string{"duplicate declaration of 'first'"});
    TPP_CHECK_EQ(diagnostics[1].severity, tpp::DiagnosticSeverity::note);
    TPP_CHECK_EQ(
        diagnostics[1].message,
        std::string{"previous declaration is here"});
    TPP_CHECK_EQ(diagnostics[2].severity, tpp::DiagnosticSeverity::error);
    TPP_CHECK_EQ(
        diagnostics[2].message,
        std::string{"duplicate declaration of 'second'"});
    TPP_CHECK_EQ(diagnostics[3].severity, tpp::DiagnosticSeverity::note);
    TPP_CHECK_EQ(diagnostics[3].message, diagnostics[1].message);

    TPP_CHECK(!compiler.compile(
        data_path("corpus/invalid/name/multiple_independent.tpp"),
        session));
    TPP_CHECK_EQ(session.diagnostics().error_count(), std::size_t{3});
    diagnostics = session.diagnostics().diagnostics();
    TPP_CHECK_EQ(diagnostics.size(), std::size_t{3});
    TPP_CHECK_EQ(
        diagnostics[0].message,
        std::string{"unknown name 'missing_first'"});
    TPP_CHECK_EQ(
        diagnostics[1].message,
        std::string{"unknown name 'missing_second'"});
    TPP_CHECK_EQ(
        diagnostics[2].message,
        std::string{"unknown name 'missing_third'"});

    TPP_CHECK(!compiler.compile(
        data_path("corpus/invalid/type/multiple_independent.tpp"),
        session));
    TPP_CHECK_EQ(session.diagnostics().error_count(), std::size_t{4});
    diagnostics = session.diagnostics().diagnostics();
    TPP_CHECK_EQ(diagnostics.size(), std::size_t{4});
    TPP_CHECK_EQ(
        diagnostics[0].message,
        std::string{
            "cannot initialize 'int' with value of type 'string'"});
    TPP_CHECK_EQ(
        diagnostics[1].message,
        std::string{"cannot return value of type 'bool'; expected 'int'"});
    TPP_CHECK_EQ(
        diagnostics[2].message,
        std::string{"condition must have type 'bool', got 'int'"});
    TPP_CHECK_EQ(
        diagnostics[3].message,
        std::string{
            "operator '+' requires two matching 'int' or 'string' operands, "
            "got 'int' and 'string'"});

    TPP_CHECK(!compiler.compile(
        data_path("corpus/invalid/flow/multiple_independent.tpp"),
        session));
    TPP_CHECK_EQ(session.diagnostics().error_count(), std::size_t{3});
    diagnostics = session.diagnostics().diagnostics();
    TPP_CHECK_EQ(diagnostics.size(), std::size_t{3});
    TPP_CHECK_EQ(
        diagnostics[0].message,
        std::string{"'break' is only allowed inside a loop"});
    TPP_CHECK_EQ(
        diagnostics[1].message,
        std::string{"'continue' is only allowed inside a loop"});
    TPP_CHECK_EQ(
        diagnostics[2].message,
        std::string{
            "non-void function 'third' may reach the end without returning a "
            "value"});
}

void semantically_valid_unsupported_backend_fails_without_partial_cpp()
{
    tpp::CompilationSession session;
    const tpp::Compiler compiler;

    TPP_CHECK(compiler.compile(
        data_path("semantic_valid_backend_unsupported.tpp"),
        session));
    TPP_CHECK(session.diagnostics().diagnostics().empty());

    const auto generated = lower_and_generate(session);
    TPP_CHECK(!generated.has_value());
    TPP_CHECK(session.diagnostics().has_errors());
    TPP_CHECK(has_diagnostic(session, "nested functions"));
    TPP_CHECK(has_diagnostic(session, "while statements"));
}

void session_reuse_after_recovery_is_clean_and_repeatable()
{
    const tpp::Compiler compiler;
    tpp::CompilationSession session;

    TPP_CHECK(!compiler.compile(
        data_path("multiple_lexical_errors.tpp"),
        session));
    TPP_CHECK_EQ(session.diagnostics().error_count(), std::size_t{4});
    TPP_CHECK(!compiler.compile(
        data_path("multiple_syntax_errors.tpp"),
        session));
    TPP_CHECK_EQ(session.diagnostics().error_count(), std::size_t{5});

    TPP_CHECK(compiler.compile(data_path("codegen_combined.tpp"), session));
    const auto first = lower_and_generate(session);
    TPP_CHECK(first.has_value());
    TPP_CHECK(session.diagnostics().diagnostics().empty());

    TPP_CHECK(compiler.compile(data_path("codegen_combined.tpp"), session));
    const auto second = lower_and_generate(session);
    TPP_CHECK(second.has_value());
    TPP_CHECK_EQ(*second, *first);
    TPP_CHECK(session.diagnostics().diagnostics().empty());
}

} // namespace

int main()
{
    return tpp::test::run({
        {"combined supported program reaches deterministic C++",
         combined_supported_program_reaches_deterministic_cpp},
        {"multiple lexical errors are ordered and gate parser",
         multiple_lexical_errors_are_ordered_and_gate_the_parser},
        {"multiple syntax errors keep recovered program and gate semantics",
         multiple_syntax_errors_keep_a_recovered_program_and_gate_semantics},
        {"semantic failures preserve prior pass state",
         semantic_failures_gate_later_passes_but_keep_prior_state},
        {"independent semantic error corpus has stable order",
         independent_semantic_error_corpus_has_stable_order},
        {"semantic valid unsupported backend has no partial output",
         semantically_valid_unsupported_backend_fails_without_partial_cpp},
        {"session reuse after recovery is clean and repeatable",
         session_reuse_after_recovery_is_clean_and_repeatable},
    });
}
