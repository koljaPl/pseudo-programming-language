#include "test_support.hpp"

#include "pseudo/ast/program.hpp"
#include "pseudo/diagnostics/diagnostic_engine.hpp"
#include "pseudo/lexer/lexer.hpp"
#include "pseudo/lowering/lowered_ir.hpp"
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
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace {

static_assert(!std::is_copy_constructible_v<tpp::LoweredProgram>);
static_assert(std::is_move_constructible_v<tpp::LoweredProgram>);

class CheckedProgram {
public:
    explicit CheckedProgram(std::string source)
        : program_{tpp::SourceSpan{tpp::SourceId{0}, 0, 0}, {}}
    {
        const auto source_id = sources_.add_source(
            "lowering.tpp",
            std::move(source));
        tpp::Lexer lexer{source_id, sources_, semantic_diagnostics_};
        tokens_ = lexer.lex();
        TPP_CHECK(!semantic_diagnostics_.has_errors());

        tpp::Parser parser{tokens_, sources_, semantic_diagnostics_};
        program_ = parser.parse_program();
        TPP_CHECK(!semantic_diagnostics_.has_errors());

        tpp::DeclarationCollector collector{
            types_,
            symbols_,
            declarations_,
            semantic_diagnostics_,
        };
        TPP_CHECK(collector.collect(program_));

        tpp::NameResolver resolver{
            symbols_,
            declarations_,
            resolutions_,
            semantic_diagnostics_,
        };
        TPP_CHECK(resolver.resolve(program_));

        tpp::TypeChecker checker{
            types_,
            symbols_,
            declarations_,
            resolutions_,
            type_info_,
            semantic_diagnostics_,
        };
        TPP_CHECK(checker.check(program_));

        tpp::ControlFlowChecker control_flow_checker{
            semantic_diagnostics_};
        TPP_CHECK(control_flow_checker.check(program_));
        TPP_CHECK(!semantic_diagnostics_.has_errors());
    }

    [[nodiscard]] tpp::LoweringContext context() const noexcept
    {
        return tpp::LoweringContext{
            .types = types_,
            .symbols = symbols_,
            .declarations = declarations_,
            .resolutions = resolutions_,
            .type_info = type_info_,
        };
    }

    [[nodiscard]] std::optional<tpp::LoweredProgram> lower(
        tpp::DiagnosticEngine& diagnostics) const
    {
        return tpp::lower_program(program_, context(), diagnostics);
    }

    [[nodiscard]] tpp::Program& program() noexcept { return program_; }
    [[nodiscard]] const tpp::Program& program() const noexcept
    {
        return program_;
    }
    [[nodiscard]] const tpp::TypeContext& types() const noexcept
    {
        return types_;
    }
    [[nodiscard]] const tpp::SymbolTable& symbols() const noexcept
    {
        return symbols_;
    }
    [[nodiscard]] const tpp::DeclarationInfo& declarations() const noexcept
    {
        return declarations_;
    }
    [[nodiscard]] const tpp::ResolutionInfo& resolutions() const noexcept
    {
        return resolutions_;
    }
    [[nodiscard]] const tpp::TypeInfo& type_info() const noexcept
    {
        return type_info_;
    }

private:
    tpp::SourceManager sources_;
    tpp::DiagnosticEngine semantic_diagnostics_;
    std::vector<tpp::Token> tokens_;
    tpp::Program program_;
    tpp::TypeContext types_;
    tpp::SymbolTable symbols_;
    tpp::DeclarationInfo declarations_;
    tpp::ResolutionInfo resolutions_;
    tpp::TypeInfo type_info_;
};

template <typename Node, typename Variant>
const Node& require_variant(const Variant& variant)
{
    const auto* node = std::get_if<Node>(&variant);
    TPP_CHECK(node != nullptr);
    return *node;
}

template <typename Node, typename Variant>
Node& require_variant(Variant& variant)
{
    auto* node = std::get_if<Node>(&variant);
    TPP_CHECK(node != nullptr);
    return *node;
}

const tpp::LoweredFunction& require_main(
    const tpp::LoweredProgram& program)
{
    for (const auto& function : program.functions) {
        if (function.is_main) {
            return function;
        }
    }
    TPP_CHECK(false);
    return program.functions.front();
}

tpp::FunctionDeclaration& require_ast_main(tpp::Program& program)
{
    for (auto& declaration : program.declarations) {
        auto* function = std::get_if<tpp::FunctionDeclaration>(&declaration);
        if (function != nullptr && function->name == "main") {
            return *function;
        }
    }
    TPP_CHECK(false);
    return require_variant<tpp::FunctionDeclaration>(
        program.declarations.front());
}

const tpp::LoweredStatement& require_statement(
    const tpp::LoweredBlock& block,
    const std::size_t index)
{
    TPP_CHECK(index < block.statements.size());
    return block.statements[index];
}

template <typename Node>
const Node& require_statement_node(
    const tpp::LoweredBlock& block,
    const std::size_t index)
{
    return require_variant<Node>(require_statement(block, index).node);
}

template <typename Node>
const Node& require_expression_node(const tpp::LoweredExpressionPtr& expression)
{
    TPP_CHECK(expression != nullptr);
    return require_variant<Node>(expression->node);
}

void check_span(
    const tpp::SourceSpan actual,
    const tpp::SourceSpan expected)
{
    TPP_CHECK_EQ(actual.source, expected.source);
    TPP_CHECK_EQ(actual.begin, expected.begin);
    TPP_CHECK_EQ(actual.end, expected.end);
}

void check_has_diagnostic(
    const tpp::DiagnosticEngine& diagnostics,
    const std::string_view expected)
{
    for (const auto& diagnostic : diagnostics.diagnostics()) {
        if (diagnostic.message.find(expected) != std::string::npos) {
            return;
        }
    }
    throw tpp::test::Failure{
        "expected lowering diagnostic containing '" + std::string{expected}
        + "'"};
}

void expressions_carry_semantic_identity_types_and_spans()
{
    CheckedProgram checked{R"(int add_one(int value) {
    return (value + 1);
}
int main() {
    int result = add_one(-1);
    print(result);
    print(read_int());
    print(true);
    print('x');
    print("ok");
    print(false && read_int() == 1 || true);
    return result;
}
)"};
    tpp::DiagnosticEngine diagnostics;
    const auto lowered = checked.lower(diagnostics);

    TPP_CHECK(lowered.has_value());
    TPP_CHECK(diagnostics.diagnostics().empty());
    TPP_CHECK_EQ(lowered->functions.size(), std::size_t{2});

    const auto& ast_add = require_variant<tpp::FunctionDeclaration>(
        checked.program().declarations[0]);
    const auto& add = lowered->functions[0];
    TPP_CHECK_EQ(
        add.symbol,
        checked.declarations().symbol_for(ast_add).value());
    TPP_CHECK_EQ(add.return_type, checked.types().integer_type());
    TPP_CHECK_EQ(add.parameters.size(), std::size_t{1});
    TPP_CHECK_EQ(
        add.parameters[0].symbol,
        checked.declarations().symbol_for(ast_add.parameters[0]).value());
    TPP_CHECK_EQ(add.parameters[0].type, checked.types().integer_type());
    check_span(add.span, ast_add.span);
    check_span(add.parameters[0].span, ast_add.parameters[0].span);

    TPP_CHECK(add.body != nullptr);
    const auto& return_statement =
        require_statement_node<tpp::LoweredReturnStatement>(*add.body, 0);
    const auto& grouped =
        require_expression_node<tpp::LoweredGroupedExpression>(
            return_statement.value);
    const auto& binary =
        require_expression_node<tpp::LoweredBinaryExpression>(
            grouped.expression);
    TPP_CHECK_EQ(
        binary.operator_kind,
        tpp::LoweredBinaryOperator::add);
    const auto& parameter =
        require_expression_node<tpp::LoweredStorageExpression>(binary.left);
    TPP_CHECK_EQ(
        require_variant<tpp::SymbolId>(parameter.storage),
        add.parameters[0].symbol);
    const auto& integer =
        require_expression_node<tpp::LoweredIntegerLiteralExpression>(
            binary.right);
    TPP_CHECK_EQ(integer.lexeme, std::string{"1"});

    const auto& main = require_main(*lowered);
    TPP_CHECK(main.body != nullptr);
    const auto& variable =
        require_statement_node<tpp::LoweredVariableStatement>(*main.body, 0);
    TPP_CHECK_EQ(variable.type, checked.types().integer_type());
    const auto& call =
        require_expression_node<tpp::LoweredUserCallExpression>(
            variable.initializer);
    TPP_CHECK_EQ(call.function, add.symbol);
    TPP_CHECK_EQ(call.arguments.size(), std::size_t{1});
    const auto& unary =
        require_expression_node<tpp::LoweredUnaryExpression>(
            call.arguments[0]);
    TPP_CHECK_EQ(unary.operator_kind, tpp::LoweredUnaryOperator::minus);

    const auto& print_statement =
        require_statement_node<tpp::LoweredExpressionStatement>(
            *main.body,
            1);
    const auto& print =
        require_expression_node<tpp::LoweredBuiltinCallExpression>(
            print_statement.expression);
    TPP_CHECK_EQ(print.builtin, tpp::BuiltinFunctionKind::print);
    const auto& result_reference =
        require_expression_node<tpp::LoweredStorageExpression>(
            print.arguments[0]);
    TPP_CHECK_EQ(
        require_variant<tpp::SymbolId>(result_reference.storage),
        variable.symbol);

    const auto& nested_print_statement =
        require_statement_node<tpp::LoweredExpressionStatement>(
            *main.body,
            2);
    const auto& nested_print =
        require_expression_node<tpp::LoweredBuiltinCallExpression>(
            nested_print_statement.expression);
    const auto& read =
        require_expression_node<tpp::LoweredBuiltinCallExpression>(
            nested_print.arguments[0]);
    TPP_CHECK_EQ(read.builtin, tpp::BuiltinFunctionKind::read_int);
    TPP_CHECK_EQ(read.arguments.size(), std::size_t{0});

    TPP_CHECK(std::holds_alternative<tpp::LoweredBooleanLiteralExpression>(
        require_expression_node<tpp::LoweredBuiltinCallExpression>(
            require_statement_node<tpp::LoweredExpressionStatement>(
                *main.body,
                3).expression).arguments[0]->node));
    TPP_CHECK(std::holds_alternative<tpp::LoweredCharacterLiteralExpression>(
        require_expression_node<tpp::LoweredBuiltinCallExpression>(
            require_statement_node<tpp::LoweredExpressionStatement>(
                *main.body,
                4).expression).arguments[0]->node));
    TPP_CHECK(std::holds_alternative<tpp::LoweredStringLiteralExpression>(
        require_expression_node<tpp::LoweredBuiltinCallExpression>(
            require_statement_node<tpp::LoweredExpressionStatement>(
                *main.body,
                5).expression).arguments[0]->node));

    const auto& short_circuit_print =
        require_expression_node<tpp::LoweredBuiltinCallExpression>(
            require_statement_node<tpp::LoweredExpressionStatement>(
                *main.body,
                6).expression);
    const auto& logical_or =
        require_expression_node<tpp::LoweredBinaryExpression>(
            short_circuit_print.arguments[0]);
    TPP_CHECK_EQ(
        logical_or.operator_kind,
        tpp::LoweredBinaryOperator::logical_or);
    const auto& logical_and =
        require_expression_node<tpp::LoweredBinaryExpression>(
            logical_or.left);
    TPP_CHECK_EQ(
        logical_and.operator_kind,
        tpp::LoweredBinaryOperator::logical_and);
}

void strings_vectors_members_indexing_and_assignments_are_typed()
{
    CheckedProgram checked{R"(int main() {
    string text = "abc";
    text.push('!');
    char first = text[0];
    vector<string> values = vector<string>(2, text);
    values[0][0] = 'X';
    print(values[0].length());
    return 0;
}
)"};
    tpp::DiagnosticEngine diagnostics;
    const auto lowered = checked.lower(diagnostics);

    TPP_CHECK(lowered.has_value());
    TPP_CHECK(diagnostics.diagnostics().empty());
    const auto& main = require_main(*lowered);
    TPP_CHECK(main.body != nullptr);

    const auto& push_statement =
        require_statement_node<tpp::LoweredExpressionStatement>(
            *main.body,
            1);
    const auto& push =
        require_expression_node<tpp::LoweredMemberCallExpression>(
            push_statement.expression);
    TPP_CHECK_EQ(push.member, tpp::MemberKind::string_push);
    TPP_CHECK_EQ(push.arguments.size(), std::size_t{1});

    const auto& first =
        require_statement_node<tpp::LoweredVariableStatement>(*main.body, 2);
    const auto& string_index =
        require_expression_node<tpp::LoweredIndexExpression>(
            first.initializer);
    TPP_CHECK_EQ(string_index.container_type, checked.types().string_type());
    TPP_CHECK_EQ(first.type, checked.types().character_type());

    const auto& values =
        require_statement_node<tpp::LoweredVariableStatement>(*main.body, 3);
    const auto& construction =
        require_expression_node<tpp::LoweredVectorConstructionExpression>(
            values.initializer);
    TPP_CHECK_EQ(construction.element_type, checked.types().string_type());
    TPP_CHECK_EQ(construction.arguments.size(), std::size_t{2});

    const auto& assignment =
        require_statement_node<tpp::LoweredAssignmentStatement>(
            *main.body,
            4);
    TPP_CHECK_EQ(
        assignment.operator_kind,
        tpp::LoweredAssignmentOperator::assign);
    TPP_CHECK_EQ(assignment.target.indices.size(), std::size_t{2});
    TPP_CHECK_EQ(assignment.target.container_types.size(), std::size_t{2});
    TPP_CHECK_EQ(
        assignment.target.container_types[1],
        checked.types().string_type());
    TPP_CHECK_EQ(assignment.target.type, checked.types().character_type());

    const auto& print_statement =
        require_statement_node<tpp::LoweredExpressionStatement>(
            *main.body,
            5);
    const auto& print =
        require_expression_node<tpp::LoweredBuiltinCallExpression>(
            print_statement.expression);
    const auto& length =
        require_expression_node<tpp::LoweredMemberCallExpression>(
            print.arguments[0]);
    TPP_CHECK_EQ(length.member, tpp::MemberKind::string_length);
    TPP_CHECK(std::holds_alternative<tpp::LoweredIndexExpression>(
        length.receiver->node));
}

void ranges_make_ordered_deterministic_overflow_safe_temporaries()
{
    CheckedProgram checked{R"(int begin_bound() { return 1; }
int end_bound() { return 2; }
int main() {
    for value in begin_bound()..end_bound() { continue; }
    for maximum in 9223372036854775807..=9223372036854775807 {
        break;
    }
    return 0;
}
)"};
    tpp::DiagnosticEngine diagnostics;
    const auto first = checked.lower(diagnostics);
    TPP_CHECK(first.has_value());
    TPP_CHECK(diagnostics.diagnostics().empty());
    tpp::DiagnosticEngine repeated_diagnostics;
    const auto repeated = checked.lower(repeated_diagnostics);
    TPP_CHECK(repeated.has_value());
    TPP_CHECK(repeated_diagnostics.diagnostics().empty());

    const auto& main = require_main(*first);
    const auto& repeated_main = require_main(*repeated);
    TPP_CHECK(main.body != nullptr);
    TPP_CHECK(repeated_main.body != nullptr);
    const auto& exclusive =
        require_statement_node<tpp::LoweredRangeStatement>(*main.body, 0);
    const auto& repeated_exclusive =
        require_statement_node<tpp::LoweredRangeStatement>(
            *repeated_main.body,
            0);
    TPP_CHECK_EQ(exclusive.begin_storage.id.value, std::size_t{0});
    TPP_CHECK_EQ(exclusive.end_storage.id.value, std::size_t{1});
    TPP_CHECK_EQ(exclusive.cursor_storage.id.value, std::size_t{2});
    TPP_CHECK_EQ(
        exclusive.begin_storage.role,
        tpp::TempRole::range_begin);
    TPP_CHECK_EQ(exclusive.end_storage.role, tpp::TempRole::range_end);
    TPP_CHECK_EQ(
        exclusive.cursor_storage.role,
        tpp::TempRole::range_cursor);
    TPP_CHECK_EQ(exclusive.begin_storage.owner, exclusive.binding);
    TPP_CHECK_EQ(exclusive.end_storage.owner, exclusive.binding);
    TPP_CHECK_EQ(exclusive.cursor_storage.owner, exclusive.binding);
    TPP_CHECK_EQ(exclusive.binding_type, checked.types().integer_type());
    TPP_CHECK(!exclusive.active_storage.has_value());
    TPP_CHECK_EQ(
        exclusive.condition_kind,
        tpp::LoweredRangeConditionKind::cursor_less_than_end);
    TPP_CHECK_EQ(
        exclusive.step_kind,
        tpp::LoweredRangeStepKind::increment_cursor);
    TPP_CHECK_EQ(
        require_expression_node<tpp::LoweredUserCallExpression>(
            exclusive.begin_value).function,
        first->functions[0].symbol);
    TPP_CHECK_EQ(
        require_expression_node<tpp::LoweredUserCallExpression>(
            exclusive.end_value).function,
        first->functions[1].symbol);
    TPP_CHECK(exclusive.body != nullptr);
    TPP_CHECK(std::holds_alternative<tpp::LoweredContinueStatement>(
        require_statement(*exclusive.body, 0).node));

    const auto& inclusive =
        require_statement_node<tpp::LoweredRangeStatement>(*main.body, 1);
    TPP_CHECK_EQ(inclusive.begin_storage.id.value, std::size_t{3});
    TPP_CHECK_EQ(inclusive.end_storage.id.value, std::size_t{4});
    TPP_CHECK_EQ(inclusive.cursor_storage.id.value, std::size_t{5});
    TPP_CHECK(inclusive.active_storage.has_value());
    TPP_CHECK_EQ(inclusive.active_storage->id.value, std::size_t{6});
    TPP_CHECK_EQ(
        inclusive.active_storage->role,
        tpp::TempRole::range_active);
    TPP_CHECK_EQ(
        inclusive.condition_kind,
        tpp::LoweredRangeConditionKind::active);
    TPP_CHECK_EQ(
        inclusive.step_kind,
        tpp::LoweredRangeStepKind::update_active_then_guarded_increment);
    TPP_CHECK_EQ(
        require_expression_node<tpp::LoweredIntegerLiteralExpression>(
            inclusive.begin_value).lexeme,
        std::string{"9223372036854775807"});
    TPP_CHECK_EQ(
        require_expression_node<tpp::LoweredIntegerLiteralExpression>(
            inclusive.end_value).lexeme,
        std::string{"9223372036854775807"});

    TPP_CHECK_EQ(
        repeated_exclusive.begin_storage.id,
        exclusive.begin_storage.id);
    TPP_CHECK_EQ(
        repeated_exclusive.end_storage.id,
        exclusive.end_storage.id);
    TPP_CHECK_EQ(
        repeated_exclusive.cursor_storage.id,
        exclusive.cursor_storage.id);
    check_span(exclusive.begin_storage.span, exclusive.begin_value->span);
    check_span(exclusive.end_storage.span, exclusive.end_value->span);
}

void foreach_uses_typed_owned_snapshots_and_nested_control_flow()
{
    CheckedProgram checked{R"(int main() {
    string text = "ab";
    vector<bool> flags = vector<bool>(2, true);
    for character in text {
        { print(character); }
        for index in 0..1 { continue; }
    }
    for flag in flags { print(flag); break; }
    for item in vector<int>(2, 7) { print(item); }
    return 0;
}
)"};
    tpp::DiagnosticEngine diagnostics;
    const auto lowered = checked.lower(diagnostics);

    TPP_CHECK(lowered.has_value());
    TPP_CHECK(diagnostics.diagnostics().empty());
    const auto& main = require_main(*lowered);
    TPP_CHECK(main.body != nullptr);

    const auto& characters =
        require_statement_node<tpp::LoweredForEachStatement>(*main.body, 2);
    TPP_CHECK_EQ(characters.snapshot_storage.id.value, std::size_t{0});
    TPP_CHECK_EQ(
        characters.snapshot_storage.role,
        tpp::TempRole::iterable_snapshot);
    TPP_CHECK_EQ(characters.iterable_type, checked.types().string_type());
    TPP_CHECK_EQ(characters.binding_type, checked.types().character_type());
    TPP_CHECK_EQ(characters.snapshot_storage.type, characters.iterable_type);
    TPP_CHECK_EQ(characters.snapshot_storage.owner, characters.binding);
    TPP_CHECK(characters.body != nullptr);

    const auto& nested_block =
        require_statement_node<tpp::LoweredBlockStatement>(
            *characters.body,
            0);
    TPP_CHECK(nested_block.block != nullptr);
    TPP_CHECK(std::holds_alternative<tpp::LoweredExpressionStatement>(
        require_statement(*nested_block.block, 0).node));
    const auto& nested_range =
        require_statement_node<tpp::LoweredRangeStatement>(
            *characters.body,
            1);
    TPP_CHECK_EQ(nested_range.begin_storage.id.value, std::size_t{1});
    TPP_CHECK_EQ(nested_range.end_storage.id.value, std::size_t{2});
    TPP_CHECK_EQ(nested_range.cursor_storage.id.value, std::size_t{3});
    TPP_CHECK(std::holds_alternative<tpp::LoweredContinueStatement>(
        require_statement(*nested_range.body, 0).node));

    const auto& flags =
        require_statement_node<tpp::LoweredForEachStatement>(*main.body, 3);
    TPP_CHECK_EQ(flags.snapshot_storage.id.value, std::size_t{4});
    TPP_CHECK_EQ(flags.binding_type, checked.types().boolean_type());
    const auto flags_type = checked.types().lookup(flags.iterable_type);
    TPP_CHECK(flags_type.has_value());
    const auto& vector_type =
        require_variant<tpp::SemanticVectorType>(*flags_type);
    TPP_CHECK_EQ(vector_type.element_type, checked.types().boolean_type());
    TPP_CHECK(std::holds_alternative<tpp::LoweredBreakStatement>(
        require_statement(*flags.body, 1).node));

    const auto& temporary =
        require_statement_node<tpp::LoweredForEachStatement>(*main.body, 4);
    TPP_CHECK_EQ(temporary.snapshot_storage.id.value, std::size_t{5});
    TPP_CHECK_EQ(temporary.binding_type, checked.types().integer_type());
    TPP_CHECK(std::holds_alternative<
        tpp::LoweredVectorConstructionExpression>(temporary.iterable->node));
}

tpp::LoweredProgram lower_detached_program()
{
    CheckedProgram checked{
        "int identity(int value) { return value; } "
        "int main() { return identity(7); }"};
    tpp::DiagnosticEngine diagnostics;
    auto lowered = checked.lower(diagnostics);
    TPP_CHECK(lowered.has_value());
    TPP_CHECK(diagnostics.diagnostics().empty());
    return std::move(*lowered);
}

void lowered_program_owns_data_after_ast_and_side_tables_die()
{
    auto lowered = lower_detached_program();
    TPP_CHECK_EQ(lowered.functions.size(), std::size_t{2});
    const auto& main = require_main(lowered);
    TPP_CHECK(main.body != nullptr);
    const auto& return_statement =
        require_statement_node<tpp::LoweredReturnStatement>(*main.body, 0);
    const auto& call =
        require_expression_node<tpp::LoweredUserCallExpression>(
            return_statement.value);
    TPP_CHECK_EQ(call.function, lowered.functions[0].symbol);
    const auto& argument =
        require_expression_node<tpp::LoweredIntegerLiteralExpression>(
            call.arguments[0]);
    TPP_CHECK_EQ(argument.lexeme, std::string{"7"});
}

void signed_minimum_magnitude_is_only_allowed_below_unary_minus()
{
    constexpr std::string_view minimum_magnitude = "9223372036854775808";

    {
        CheckedProgram checked{"int main() { return 1; }"};
        auto& main = require_ast_main(checked.program());
        TPP_CHECK(main.body != nullptr);
        auto& statement = require_variant<tpp::Statement>(
            main.body->items.front());
        auto& return_statement =
            require_variant<tpp::ReturnStatement>(statement.node);
        TPP_CHECK(return_statement.value != nullptr);
        auto& integer = require_variant<tpp::IntegerLiteralExpression>(
            return_statement.value->node);
        integer.lexeme = minimum_magnitude;
        tpp::DiagnosticEngine diagnostics;

        const auto lowered = checked.lower(diagnostics);

        TPP_CHECK(!lowered.has_value());
        TPP_CHECK(diagnostics.has_errors());
        check_has_diagnostic(diagnostics, "outside");
    }

    {
        CheckedProgram checked{"int main() { return +1; }"};
        auto& main = require_ast_main(checked.program());
        TPP_CHECK(main.body != nullptr);
        auto& statement = require_variant<tpp::Statement>(
            main.body->items.front());
        auto& return_statement =
            require_variant<tpp::ReturnStatement>(statement.node);
        TPP_CHECK(return_statement.value != nullptr);
        auto& unary = require_variant<tpp::UnaryExpression>(
            return_statement.value->node);
        TPP_CHECK(unary.operand != nullptr);
        auto& integer = require_variant<tpp::IntegerLiteralExpression>(
            unary.operand->node);
        integer.lexeme = minimum_magnitude;
        tpp::DiagnosticEngine diagnostics;

        const auto lowered = checked.lower(diagnostics);

        TPP_CHECK(!lowered.has_value());
        TPP_CHECK(diagnostics.has_errors());
        check_has_diagnostic(diagnostics, "outside");
    }

    {
        const CheckedProgram checked{R"(int direct() {
    return -9223372036854775808;
}
int main() {
    return -(9223372036854775808);
}
)"};
        tpp::DiagnosticEngine diagnostics;

        const auto lowered = checked.lower(diagnostics);

        TPP_CHECK(lowered.has_value());
        TPP_CHECK(diagnostics.diagnostics().empty());
    }
}

void grouped_expression_requires_matching_recorded_operand_type()
{
    CheckedProgram checked{R"(int main() {
    print((1));
    print(true);
    return 0;
}
)"};
    auto& main = require_ast_main(checked.program());
    TPP_CHECK(main.body != nullptr);

    auto& integer_statement = require_variant<tpp::Statement>(
        main.body->items[0]);
    auto& integer_expression_statement =
        require_variant<tpp::ExpressionStatement>(integer_statement.node);
    TPP_CHECK(integer_expression_statement.expression != nullptr);
    auto& integer_call = require_variant<tpp::CallExpression>(
        integer_expression_statement.expression->node);
    TPP_CHECK_EQ(integer_call.arguments.size(), std::size_t{1});
    TPP_CHECK(integer_call.arguments[0] != nullptr);
    auto& grouped = require_variant<tpp::ParenthesizedExpression>(
        integer_call.arguments[0]->node);
    TPP_CHECK(grouped.expression != nullptr);

    auto& boolean_statement = require_variant<tpp::Statement>(
        main.body->items[1]);
    auto& boolean_expression_statement =
        require_variant<tpp::ExpressionStatement>(boolean_statement.node);
    TPP_CHECK(boolean_expression_statement.expression != nullptr);
    auto& boolean_call = require_variant<tpp::CallExpression>(
        boolean_expression_statement.expression->node);
    TPP_CHECK_EQ(boolean_call.arguments.size(), std::size_t{1});
    TPP_CHECK(boolean_call.arguments[0] != nullptr);

    std::swap(grouped.expression, boolean_call.arguments[0]);
    tpp::DiagnosticEngine diagnostics;

    const auto lowered = checked.lower(diagnostics);

    TPP_CHECK(!lowered.has_value());
    TPP_CHECK(diagnostics.has_errors());
    check_has_diagnostic(diagnostics, "grouped expression type");
}

void malformed_ast_and_semantic_state_fail_without_partial_program()
{
    {
        const CheckedProgram checked{"int main() { return 0; }"};
        const tpp::DeclarationInfo empty_declarations;
        const auto context = tpp::LoweringContext{
            .types = checked.types(),
            .symbols = checked.symbols(),
            .declarations = empty_declarations,
            .resolutions = checked.resolutions(),
            .type_info = checked.type_info(),
        };
        tpp::DiagnosticEngine diagnostics;
        const auto lowered = tpp::lower_program(
            checked.program(),
            context,
            diagnostics);
        TPP_CHECK(!lowered.has_value());
        TPP_CHECK(diagnostics.has_errors());
        check_has_diagnostic(diagnostics, "function declaration has no symbol");
    }

    {
        const CheckedProgram checked{"int main() { print(1); return 0; }"};
        const tpp::ResolutionInfo empty_resolutions;
        const auto context = tpp::LoweringContext{
            .types = checked.types(),
            .symbols = checked.symbols(),
            .declarations = checked.declarations(),
            .resolutions = empty_resolutions,
            .type_info = checked.type_info(),
        };
        tpp::DiagnosticEngine diagnostics;
        const auto lowered = tpp::lower_program(
            checked.program(),
            context,
            diagnostics);
        TPP_CHECK(!lowered.has_value());
        TPP_CHECK(diagnostics.has_errors());
        check_has_diagnostic(diagnostics, "semantic");
    }

    {
        const CheckedProgram checked{"int main() { return 0; }"};
        const tpp::TypeInfo empty_type_info;
        const auto context = tpp::LoweringContext{
            .types = checked.types(),
            .symbols = checked.symbols(),
            .declarations = checked.declarations(),
            .resolutions = checked.resolutions(),
            .type_info = empty_type_info,
        };
        tpp::DiagnosticEngine diagnostics;
        const auto lowered = tpp::lower_program(
            checked.program(),
            context,
            diagnostics);
        TPP_CHECK(!lowered.has_value());
        TPP_CHECK(diagnostics.has_errors());
        check_has_diagnostic(diagnostics, "type");
    }

    {
        CheckedProgram checked{
            "int main() { for value in 0..1 { print(value); } return 0; }"};
        auto& main = require_ast_main(checked.program());
        TPP_CHECK(main.body != nullptr);
        auto& statement = require_variant<tpp::Statement>(
            main.body->items.front());
        auto& range = require_variant<tpp::ForRangeStatement>(statement.node);
        range.end.reset();
        tpp::DiagnosticEngine diagnostics;
        const auto lowered = checked.lower(diagnostics);
        TPP_CHECK(!lowered.has_value());
        TPP_CHECK(diagnostics.has_errors());
        check_has_diagnostic(diagnostics, "malformed AST");
    }

    {
        CheckedProgram checked{"int main() { return 0; }"};
        require_ast_main(checked.program()).body.reset();
        tpp::DiagnosticEngine diagnostics;

        const auto lowered = checked.lower(diagnostics);

        TPP_CHECK(!lowered.has_value());
        TPP_CHECK_EQ(diagnostics.error_count(), std::size_t{1});
        check_has_diagnostic(diagnostics, "function 'main' has no body");
    }

    {
        CheckedProgram checked{R"(int main() {
    print(1);
    print(2);
    return 0;
}
)"};
        auto& main = require_ast_main(checked.program());
        TPP_CHECK(main.body != nullptr);
        for (std::size_t index = 0; index < 2; ++index) {
            auto& statement = require_variant<tpp::Statement>(
                main.body->items[index]);
            auto& expression_statement =
                require_variant<tpp::ExpressionStatement>(statement.node);
            TPP_CHECK(expression_statement.expression != nullptr);
            auto& call = require_variant<tpp::CallExpression>(
                expression_statement.expression->node);
            TPP_CHECK_EQ(call.arguments.size(), std::size_t{1});
            call.arguments.front().reset();
        }
        tpp::DiagnosticEngine diagnostics;

        const auto lowered = checked.lower(diagnostics);

        TPP_CHECK(!lowered.has_value());
        TPP_CHECK_EQ(diagnostics.error_count(), std::size_t{2});
        const auto reported = diagnostics.diagnostics();
        TPP_CHECK(reported[0].primary_span.has_value());
        TPP_CHECK(reported[1].primary_span.has_value());
        TPP_CHECK(reported[0].primary_span->begin
                  < reported[1].primary_span->begin);
        check_has_diagnostic(diagnostics, "call argument is missing");
    }
}

}

int main()
{
    return tpp::test::run({
        {"expressions carry semantic identity, types, and spans",
         expressions_carry_semantic_identity_types_and_spans},
        {"strings vectors members indexing and assignments are typed",
         strings_vectors_members_indexing_and_assignments_are_typed},
        {"range temporaries are ordered deterministic and overflow-safe",
         ranges_make_ordered_deterministic_overflow_safe_temporaries},
        {"foreach snapshots and nested control flow",
         foreach_uses_typed_owned_snapshots_and_nested_control_flow},
        {"lowered program owns its data",
         lowered_program_owns_data_after_ast_and_side_tables_die},
        {"signed minimum magnitude context",
         signed_minimum_magnitude_is_only_allowed_below_unary_minus},
        {"grouped expression operand type consistency",
         grouped_expression_requires_matching_recorded_operand_type},
        {"malformed AST and semantic state",
         malformed_ast_and_semantic_state_fail_without_partial_program},
    });
}
