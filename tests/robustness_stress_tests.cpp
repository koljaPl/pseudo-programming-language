#include "test_support.hpp"

#include "pseudo/codegen/cpp_generator.hpp"
#include "pseudo/diagnostics/diagnostic_engine.hpp"
#include "pseudo/lexer/lexer.hpp"
#include "pseudo/lowering/lowerer.hpp"
#include "pseudo/parser/parser.hpp"
#include "pseudo/semantic/control_flow_checker.hpp"
#include "pseudo/semantic/declaration_collector.hpp"
#include "pseudo/semantic/declaration_info.hpp"
#include "pseudo/semantic/name_resolver.hpp"
#include "pseudo/semantic/resolution_info.hpp"
#include "pseudo/semantic/symbol_table.hpp"
#include "pseudo/semantic/type_checker.hpp"
#include "pseudo/semantic/type_context.hpp"
#include "pseudo/semantic/type_info.hpp"
#include "pseudo/source/source_manager.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace {

struct PipelineResult {
    std::string generated_cpp;
    std::size_t token_count{};
    std::size_t declaration_count{};
    std::size_t symbol_count{};
    std::size_t main_statement_count{};
};

PipelineResult compile_to_cpp(const std::string& source_text)
{
    tpp::SourceManager sources;
    tpp::DiagnosticEngine diagnostics;
    const auto source = sources.add_source("stress.tpp", source_text);

    tpp::Lexer lexer{source, sources, diagnostics};
    auto tokens = lexer.lex();
    TPP_CHECK(!diagnostics.has_errors());

    tpp::Parser parser{tokens, sources, diagnostics};
    auto program = parser.parse_program();
    TPP_CHECK(!diagnostics.has_errors());

    tpp::TypeContext types;
    tpp::SymbolTable symbols;
    tpp::DeclarationInfo declarations;
    tpp::DeclarationCollector collector{
        types,
        symbols,
        declarations,
        diagnostics,
    };
    TPP_CHECK(collector.collect(program));

    tpp::ResolutionInfo resolutions;
    tpp::NameResolver resolver{
        symbols,
        declarations,
        resolutions,
        diagnostics,
    };
    TPP_CHECK(resolver.resolve(program));

    tpp::TypeInfo type_info;
    tpp::TypeChecker type_checker{
        types,
        symbols,
        declarations,
        resolutions,
        type_info,
        diagnostics,
    };
    TPP_CHECK(type_checker.check(program));

    tpp::ControlFlowChecker flow_checker{diagnostics};
    TPP_CHECK(flow_checker.check(program));

    auto lowered = tpp::lower_program(
        program,
        tpp::LoweringContext{
            .types = types,
            .symbols = symbols,
            .declarations = declarations,
            .resolutions = resolutions,
            .type_info = type_info,
        },
        diagnostics);
    TPP_CHECK(lowered.has_value());

    auto generated = tpp::generate_cpp(*lowered, types, diagnostics);
    TPP_CHECK(generated.has_value());
    TPP_CHECK(!diagnostics.has_errors());

    std::size_t main_statement_count = 0;
    for (const auto& declaration : program.declarations) {
        const auto* function = std::get_if<tpp::FunctionDeclaration>(
            &declaration);
        if (function != nullptr && function->name == "main") {
            TPP_CHECK(function->body != nullptr);
            main_statement_count = function->body->items.size();
        }
    }

    return PipelineResult{
        .generated_cpp = std::move(*generated),
        .token_count = tokens.size(),
        .declaration_count = program.declarations.size(),
        .symbol_count = symbols.symbol_count(),
        .main_statement_count = main_statement_count,
    };
}

std::string make_wide_function_program(const std::size_t total_function_count)
{
    const auto helper_count = total_function_count - 1;
    std::ostringstream source;
    for (std::size_t index = 0; index < helper_count; ++index) {
        source << "int function_" << index
               << "(int value) { return value + " << index << "; }\n";
    }
    source << "int main() { print(function_" << (helper_count - 1)
           << "(1)); return 0; }\n";
    return source.str();
}

std::string make_wide_statement_program(const std::size_t statement_count)
{
    const auto local_count = statement_count - 2;
    std::ostringstream source;
    source << "int main() {\n";
    for (std::size_t index = 0; index < local_count; ++index) {
        source << "int value_" << index << " = " << index << ";\n";
    }
    source << "print(value_" << (local_count - 1) << ");\nreturn 0;\n}\n";
    return source.str();
}

std::string make_operand_program(const std::size_t operand_count)
{
    std::ostringstream source;
    source << "int main() { print(";
    for (std::size_t index = 0; index < operand_count; ++index) {
        if (index != 0) {
            source << " + ";
        }
        source << '1';
    }
    source << "); return 0; }\n";
    return source.str();
}

std::string make_nested_block_loop_program(const std::size_t depth)
{
    std::ostringstream source;
    source << "int main() {\n";
    for (std::size_t index = 0; index < depth; ++index) {
        source << "for depth_" << index << " in 0..1 {\n{\n";
    }
    source << "print(1);\n";
    for (std::size_t index = 0; index < depth; ++index) {
        source << "}\n}\n";
    }
    source << "return 0;\n}\n";
    return source.str();
}

void wide_function_pipeline_is_stable()
{
    constexpr std::size_t function_count = 256;
    const auto source = make_wide_function_program(function_count);

    const auto first = compile_to_cpp(source);
    const auto second = compile_to_cpp(source);

    TPP_CHECK_EQ(first.generated_cpp, second.generated_cpp);
    TPP_CHECK_EQ(first.declaration_count, function_count);
    TPP_CHECK_EQ(first.symbol_count, (function_count * 2) - 1);
    TPP_CHECK(first.token_count > function_count * 10);
    TPP_CHECK(first.generated_cpp.size() > 25'000);
}

void one_thousand_twenty_four_statements_preserve_every_storage()
{
    constexpr std::size_t statement_count = 1'024;
    constexpr std::size_t local_count = statement_count - 2;
    const auto result = compile_to_cpp(
        make_wide_statement_program(statement_count));

    TPP_CHECK_EQ(result.declaration_count, std::size_t{1});
    TPP_CHECK_EQ(result.symbol_count, local_count + 1);
    TPP_CHECK_EQ(result.main_statement_count, statement_count);
    TPP_CHECK(result.token_count > local_count * 5);
    tpp::test::check_contains(result.generated_cpp, "tpp_variable_1022");
    tpp::test::check_contains(result.generated_cpp, "std::int64_t{1021}");
}

void five_hundred_twelve_operand_expression_survives_the_full_pipeline()
{
    constexpr std::size_t operand_count = 512;
    const auto result = compile_to_cpp(make_operand_program(operand_count));
    TPP_CHECK(result.token_count > operand_count * 2);
    TPP_CHECK(result.generated_cpp.size() > operand_count * 20);
    tpp::test::check_contains(result.generated_cpp, "std::int64_t{1}");
}

void depth_sixty_four_nested_blocks_and_loops_lower_deterministically()
{
    constexpr std::size_t depth = 64;
    const auto source = make_nested_block_loop_program(depth);
    const auto first = compile_to_cpp(source);
    const auto second = compile_to_cpp(source);

    TPP_CHECK_EQ(first.generated_cpp, second.generated_cpp);
    TPP_CHECK(first.token_count > depth * 10);
    TPP_CHECK(first.generated_cpp.size() > depth * 500);
}

void sixty_four_kib_identifier_and_string_remain_owned()
{
    constexpr std::size_t byte_count = 64U * 1'024U;
    const std::string identifier(byte_count, 'a');
    const std::string literal(byte_count, 'x');
    const std::string source = "int main() { string " + identifier
        + " = \"" + literal + "\"; print(" + identifier
        + ".length()); return 0; }";

    const auto result = compile_to_cpp(source);
    TPP_CHECK(result.token_count > 10);
    TPP_CHECK(result.generated_cpp.size() > byte_count);
    tpp::test::check_contains(
        result.generated_cpp,
        "std::string{\"xxxxxxxxxxxxxxxx");
    TPP_CHECK(
        result.generated_cpp.find(identifier)
        == std::string::npos);
}

void many_syntax_errors_recover_to_a_valid_suffix()
{
    constexpr std::size_t error_count = 128;
    std::ostringstream source_text;
    for (std::size_t index = 0; index < error_count; ++index) {
        source_text << "int broken_" << index << " = ;\n";
    }
    source_text << "int main() { return 0; }\n";

    tpp::SourceManager sources;
    tpp::DiagnosticEngine diagnostics;
    const auto source = sources.add_source("many-errors.tpp", source_text.str());
    tpp::Lexer lexer{source, sources, diagnostics};
    const auto tokens = lexer.lex();
    TPP_CHECK(!diagnostics.has_errors());

    tpp::Parser parser{tokens, sources, diagnostics};
    const auto program = parser.parse_program();

    TPP_CHECK_EQ(diagnostics.error_count(), error_count);
    TPP_CHECK_EQ(diagnostics.diagnostics().size(), error_count);
    TPP_CHECK_EQ(program.declarations.size(), std::size_t{1});
    TPP_CHECK(diagnostics.diagnostics().front().primary_span.has_value());
    TPP_CHECK(diagnostics.diagnostics().back().primary_span.has_value());
    TPP_CHECK(
        diagnostics.diagnostics().front().primary_span->begin
        < diagnostics.diagnostics().back().primary_span->begin);
}

void fixed_seed_malformed_inputs_always_make_progress()
{
    constexpr std::size_t input_count = 512;
    constexpr std::string_view alphabet =
        "abcdefghijklmnopqrstuvwxyz0123456789 +-*/%(){}[];,.'\"\\\n";
    std::uint64_t state = 0x6A09E667F3BCC909ULL;

    for (std::size_t input_index = 0; input_index < input_count;
         ++input_index) {
        state = (state * 6364136223846793005ULL) + 1ULL;
        const auto length = std::size_t{1}
            + static_cast<std::size_t>(state % 128ULL);
        std::string input(length, '}');
        for (std::size_t byte_index = 1; byte_index < length; ++byte_index) {
            state = (state * 6364136223846793005ULL) + 1ULL;
            input[byte_index] = alphabet[static_cast<std::size_t>(
                state % static_cast<std::uint64_t>(alphabet.size()))];
        }

        tpp::SourceManager sources;
        tpp::DiagnosticEngine diagnostics;
        const auto source = sources.add_source("malformed.tpp", input);
        tpp::Lexer lexer{source, sources, diagnostics};
        const auto tokens = lexer.lex();
        TPP_CHECK(!tokens.empty());
        TPP_CHECK_EQ(tokens.back().kind, tpp::TokenKind::end_of_file);

        tpp::Parser parser{tokens, sources, diagnostics};
        const auto program = parser.parse_program();
        static_cast<void>(program);
        TPP_CHECK(diagnostics.has_errors());
        TPP_CHECK(input.size() <= std::size_t{128});
    }
}

} // namespace

int main()
{
    return tpp::test::run({
        {"wide function pipeline is stable", wide_function_pipeline_is_stable},
        {"1024 statements preserve every storage",
         one_thousand_twenty_four_statements_preserve_every_storage},
        {"512 operand expression survives full pipeline",
         five_hundred_twelve_operand_expression_survives_the_full_pipeline},
        {"depth 64 nested blocks and loops lower deterministically",
         depth_sixty_four_nested_blocks_and_loops_lower_deterministically},
        {"64 KiB identifier and string remain owned",
         sixty_four_kib_identifier_and_string_remain_owned},
        {"many syntax errors recover to a valid suffix",
         many_syntax_errors_recover_to_a_valid_suffix},
        {"fixed seed malformed inputs always make progress",
         fixed_seed_malformed_inputs_always_make_progress},
    });
}
