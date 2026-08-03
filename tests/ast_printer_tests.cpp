#include "test_support.hpp"

#include "pseudo/ast/printer.hpp"
#include "pseudo/ast/program.hpp"
#include "pseudo/diagnostics/diagnostic_engine.hpp"
#include "pseudo/lexer/lexer.hpp"
#include "pseudo/parser/parser.hpp"
#include "pseudo/source/source_manager.hpp"

#include <array>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace {

class ParsedProgram {
public:
    explicit ParsedProgram(std::string source)
        : program_{tpp::SourceSpan{tpp::SourceId{0}, 0, 0}, {}}
    {
        const auto source_id = sources_.add_source(
            "printer.tpp",
            std::move(source));
        tpp::Lexer lexer{source_id, sources_, diagnostics_};
        tokens_ = lexer.lex();
        TPP_CHECK(!diagnostics_.has_errors());

        tpp::Parser parser{tokens_, sources_, diagnostics_};
        program_ = parser.parse_program();
        TPP_CHECK(!diagnostics_.has_errors());
    }

    [[nodiscard]] std::string print() const
    {
        std::ostringstream output;
        tpp::print_ast(output, program_);
        return output.str();
    }

    [[nodiscard]] tpp::Program& program() noexcept
    {
        return program_;
    }

private:
    tpp::SourceManager sources_;
    tpp::DiagnosticEngine diagnostics_;
    std::vector<tpp::Token> tokens_;
    tpp::Program program_;
};

std::string print_source(const std::string_view source)
{
    return ParsedProgram{std::string{source}}.print();
}

void empty_program_has_stable_output()
{
    TPP_CHECK_EQ(print_source(""), std::string{"Program\n"});
}

void simple_program_format_is_stable_and_repeatable()
{
    const ParsedProgram program{"int main() { return 0; }"};
    constexpr std::string_view expected =
        "Program\n"
        "  Function name=\"main\" return=int\n"
        "    Parameters\n"
        "    Block\n"
        "      Return\n"
        "        IntegerLiteral value=\"0\"\n";

    const auto first = program.print();
    const auto second = program.print();
    TPP_CHECK_EQ(first, expected);
    TPP_CHECK_EQ(second, expected);
    TPP_CHECK_EQ(first, second);
}

void all_types_parameters_and_declarations_are_printed()
{
    constexpr std::string_view source =
        "int integer_value;"
        "bool boolean_value;"
        "char character_value;"
        "string string_value;"
        "vector<vector<string>> vectors;"
        "void consume("
        "int integer_parameter,"
        "bool boolean_parameter,"
        "char character_parameter,"
        "string string_parameter,"
        "vector<int> vector_parameter"
        ") {}";

    constexpr std::string_view expected =
        "Program\n"
        "  Variable name=\"integer_value\" type=int\n"
        "  Variable name=\"boolean_value\" type=bool\n"
        "  Variable name=\"character_value\" type=char\n"
        "  Variable name=\"string_value\" type=string\n"
        "  Variable name=\"vectors\" type=vector<vector<string>>\n"
        "  Function name=\"consume\" return=void\n"
        "    Parameters\n"
        "      Parameter name=\"integer_parameter\" type=int\n"
        "      Parameter name=\"boolean_parameter\" type=bool\n"
        "      Parameter name=\"character_parameter\" type=char\n"
        "      Parameter name=\"string_parameter\" type=string\n"
        "      Parameter name=\"vector_parameter\" type=vector<int>\n"
        "    Block\n";

    TPP_CHECK_EQ(print_source(source), expected);
}

void declarations_and_control_flow_have_a_stable_full_format()
{
    constexpr std::string_view source =
        "vector<vector<string>> names;"
        "int main(int n){"
        "int value=0;"
        "void nested(){{return;}}"
        "matrix[i][j]+=value;"
        "if n>0{while true{break;}}else{continue;}"
        "if false{}"
        "for i in 0..=n{value;}"
        "for j in 0..n{}"
        "for item in names{return item;}"
        "return 0;"
        "}";

    constexpr std::string_view expected = R"(Program
  Variable name="names" type=vector<vector<string>>
  Function name="main" return=int
    Parameters
      Parameter name="n" type=int
    Block
      Variable name="value" type=int
        Initializer
          IntegerLiteral value="0"
      Function name="nested" return=void
        Parameters
        Block
          BlockStatement
            Block
              Return
      Assignment operator="+="
        AssignmentTarget name="matrix"
          Indices
            Identifier name="i"
            Identifier name="j"
        Value
          Identifier name="value"
      If
        Condition
          Binary operator=">"
            Left
              Identifier name="n"
            Right
              IntegerLiteral value="0"
        Then
          Block
            While
              Condition
                BooleanLiteral value=true
              Body
                Block
                  Break
        Else
          Block
            Continue
      If
        Condition
          BooleanLiteral value=false
        Then
          Block
      ForRange variable="i" operator="..="
        Begin
          IntegerLiteral value="0"
        End
          Identifier name="n"
        Body
          Block
            ExpressionStatement
              Identifier name="value"
      ForRange variable="j" operator=".."
        Begin
          IntegerLiteral value="0"
        End
          Identifier name="n"
        Body
          Block
      ForEach variable="item"
        Iterable
          Identifier name="names"
        Body
          Block
            Return
              Identifier name="item"
      Return
        IntegerLiteral value="0"
)";

    TPP_CHECK_EQ(print_source(source), expected);
}

void every_expression_node_and_precedence_are_visible()
{
    constexpr std::string_view source =
        "void expressions(){"
        "true;'x';\"text\";+a;-b;!c;"
        "a||b&&c==d<e+f*g;"
        "foo(a)[i].length();"
        "vector<int>(n,0);"
        "vector<string>();"
        "(value);"
        "}";

    constexpr std::string_view expected = R"(Program
  Function name="expressions" return=void
    Parameters
    Block
      ExpressionStatement
        BooleanLiteral value=true
      ExpressionStatement
        CharacterLiteral value='x'
      ExpressionStatement
        StringLiteral value="text"
      ExpressionStatement
        Unary operator="+"
          Operand
            Identifier name="a"
      ExpressionStatement
        Unary operator="-"
          Operand
            Identifier name="b"
      ExpressionStatement
        Unary operator="!"
          Operand
            Identifier name="c"
      ExpressionStatement
        Binary operator="||"
          Left
            Identifier name="a"
          Right
            Binary operator="&&"
              Left
                Identifier name="b"
              Right
                Binary operator="=="
                  Left
                    Identifier name="c"
                  Right
                    Binary operator="<"
                      Left
                        Identifier name="d"
                      Right
                        Binary operator="+"
                          Left
                            Identifier name="e"
                          Right
                            Binary operator="*"
                              Left
                                Identifier name="f"
                              Right
                                Identifier name="g"
      ExpressionStatement
        Call
          Callee
            MemberAccess member="length"
              Base
                Index
                  Base
                    Call
                      Callee
                        Identifier name="foo"
                      Arguments
                        Identifier name="a"
                  Subscript
                    Identifier name="i"
          Arguments
      ExpressionStatement
        VectorConstruction type=vector<int>
          Arguments
            Identifier name="n"
            IntegerLiteral value="0"
      ExpressionStatement
        VectorConstruction type=vector<string>
          Arguments
      ExpressionStatement
        Parenthesized
          Expression
            Identifier name="value"
)";

    TPP_CHECK_EQ(print_source(source), expected);
}

std::string unary_output(const std::string_view expression)
{
    return print_source(
        std::string{"void f(){"} + std::string{expression} + ";}");
}

std::string expected_unary_output(const std::string_view spelling)
{
    return std::string{
               "Program\n"
               "  Function name=\"f\" return=void\n"
               "    Parameters\n"
               "    Block\n"
               "      ExpressionStatement\n"
               "        Unary operator=\""}
        + std::string{spelling}
        + "\"\n"
          "          Operand\n"
          "            Identifier name=\"a\"\n";
}

std::string binary_output(const std::string_view expression)
{
    return print_source(
        std::string{"void f(){"} + std::string{expression} + ";}");
}

std::string expected_binary_output(const std::string_view spelling)
{
    return std::string{
               "Program\n"
               "  Function name=\"f\" return=void\n"
               "    Parameters\n"
               "    Block\n"
               "      ExpressionStatement\n"
               "        Binary operator=\""}
        + std::string{spelling}
        + "\"\n"
          "          Left\n"
          "            Identifier name=\"a\"\n"
          "          Right\n"
          "            Identifier name=\"b\"\n";
}

std::string assignment_output(const std::string_view spelling)
{
    return print_source(
        std::string{"void f(){x"} + std::string{spelling} + "y;}");
}

std::string expected_assignment_output(const std::string_view spelling)
{
    return std::string{
               "Program\n"
               "  Function name=\"f\" return=void\n"
               "    Parameters\n"
               "    Block\n"
               "      Assignment operator=\""}
        + std::string{spelling}
        + "\"\n"
          "        AssignmentTarget name=\"x\"\n"
          "          Indices\n"
          "        Value\n"
          "          Identifier name=\"y\"\n";
}

std::string range_output(const std::string_view spelling)
{
    return print_source(
        std::string{"void f(){for i in a"} + std::string{spelling}
        + "b{}}");
}

std::string expected_range_output(const std::string_view spelling)
{
    return std::string{
               "Program\n"
               "  Function name=\"f\" return=void\n"
               "    Parameters\n"
               "    Block\n"
               "      ForRange variable=\"i\" operator=\""}
        + std::string{spelling}
        + "\"\n"
          "        Begin\n"
          "          Identifier name=\"a\"\n"
          "        End\n"
          "          Identifier name=\"b\"\n"
          "        Body\n"
          "          Block\n";
}

void all_operators_use_canonical_spellings()
{
    struct OperatorCase {
        std::string_view expression;
        std::string_view spelling;
    };

    constexpr std::array unary_cases{
        OperatorCase{"+a", "+"},
        OperatorCase{"-a", "-"},
        OperatorCase{"!a", "!"},
        OperatorCase{"not a", "!"},
    };
    for (const auto& test_case : unary_cases) {
        TPP_CHECK_EQ(
            unary_output(test_case.expression),
            expected_unary_output(test_case.spelling));
    }

    constexpr std::array binary_cases{
        OperatorCase{"a||b", "||"},
        OperatorCase{"a or b", "||"},
        OperatorCase{"a&&b", "&&"},
        OperatorCase{"a and b", "&&"},
        OperatorCase{"a==b", "=="},
        OperatorCase{"a!=b", "!="},
        OperatorCase{"a<b", "<"},
        OperatorCase{"a<=b", "<="},
        OperatorCase{"a>b", ">"},
        OperatorCase{"a>=b", ">="},
        OperatorCase{"a+b", "+"},
        OperatorCase{"a-b", "-"},
        OperatorCase{"a*b", "*"},
        OperatorCase{"a/b", "/"},
        OperatorCase{"a%b", "%"},
    };
    for (const auto& test_case : binary_cases) {
        TPP_CHECK_EQ(
            binary_output(test_case.expression),
            expected_binary_output(test_case.spelling));
    }

    constexpr std::array assignment_spellings{
        std::string_view{"="},
        std::string_view{"+="},
        std::string_view{"-="},
        std::string_view{"*="},
        std::string_view{"/="},
        std::string_view{"%="},
    };
    for (const auto spelling : assignment_spellings) {
        TPP_CHECK_EQ(
            assignment_output(spelling),
            expected_assignment_output(spelling));
    }

    constexpr std::array range_spellings{
        std::string_view{".."},
        std::string_view{"..="},
    };
    for (const auto spelling : range_spellings) {
        TPP_CHECK_EQ(
            range_output(spelling),
            expected_range_output(spelling));
    }

    TPP_CHECK_EQ(
        print_source("void f(){a&&!b||c;}"),
        print_source("void f(){a and not b or c;}"));
}

void missing_optional_children_are_omitted()
{
    constexpr std::string_view source =
        "vector<int> values;"
        "void f(){if true{}return;foo();vector<int>();}";

    constexpr std::string_view expected = R"(Program
  Variable name="values" type=vector<int>
  Function name="f" return=void
    Parameters
    Block
      If
        Condition
          BooleanLiteral value=true
        Then
          Block
      Return
      ExpressionStatement
        Call
          Callee
            Identifier name="foo"
          Arguments
      ExpressionStatement
        VectorConstruction type=vector<int>
          Arguments
)";

    TPP_CHECK_EQ(print_source(source), expected);
}

tpp::VariableDeclaration& variable_at(
    tpp::Program& program,
    const std::size_t index)
{
    auto* variable = std::get_if<tpp::VariableDeclaration>(
        &program.declarations[index]);
    TPP_CHECK(variable != nullptr);
    TPP_CHECK(variable->initializer != nullptr);
    return *variable;
}

void literal_bytes_are_escaped_canonically()
{
    ParsedProgram program{
        "string escaped=\"\";"
        "char nul='a';"
        "char newline='a';"
        "char carriage='a';"
        "char tab='a';"
        "char backslash='a';"
        "char double_quote='a';"
        "char single_quote='a';"};

    auto& string_literal = std::get<tpp::StringLiteralExpression>(
        variable_at(program.program(), 0).initializer->node);
    string_literal.value.clear();
    constexpr std::array bytes{
        '\n',
        '\r',
        '\t',
        '\0',
        '\\',
        '"',
        '\'',
        static_cast<char>(0x01),
        static_cast<char>(0x1F),
        static_cast<char>(0x7F),
        static_cast<char>(0x80),
        static_cast<char>(0xFF),
    };
    string_literal.value.assign(bytes.begin(), bytes.end());

    constexpr std::array character_values{
        '\0',
        '\n',
        '\r',
        '\t',
        '\\',
        '"',
        '\'',
    };
    for (std::size_t index = 0; index < character_values.size(); ++index) {
        auto& character = std::get<tpp::CharacterLiteralExpression>(
            variable_at(program.program(), index + 1).initializer->node);
        character.value = character_values[index];
    }

    constexpr std::string_view expected = R"(Program
  Variable name="escaped" type=string
    Initializer
      StringLiteral value="\n\r\t\0\\\"\'\x01\x1F\x7F\x80\xFF"
  Variable name="nul" type=char
    Initializer
      CharacterLiteral value='\0'
  Variable name="newline" type=char
    Initializer
      CharacterLiteral value='\n'
  Variable name="carriage" type=char
    Initializer
      CharacterLiteral value='\r'
  Variable name="tab" type=char
    Initializer
      CharacterLiteral value='\t'
  Variable name="backslash" type=char
    Initializer
      CharacterLiteral value='\\'
  Variable name="double_quote" type=char
    Initializer
      CharacterLiteral value='\"'
  Variable name="single_quote" type=char
    Initializer
      CharacterLiteral value='\''
)";

    const auto output = program.print();
    TPP_CHECK_EQ(output, expected);
    TPP_CHECK(output.find('\0') == std::string::npos);
    TPP_CHECK(output.find('\r') == std::string::npos);
    TPP_CHECK(output.find('\t') == std::string::npos);
}

}

int main()
{
    return tpp::test::run({
        {"empty Program", empty_program_has_stable_output},
        {"stable repeatable format",
         simple_program_format_is_stable_and_repeatable},
        {"types parameters declarations",
         all_types_parameters_and_declarations_are_printed},
        {"declarations and control flow",
         declarations_and_control_flow_have_a_stable_full_format},
        {"expression nodes precedence postfix",
         every_expression_node_and_precedence_are_visible},
        {"canonical operators", all_operators_use_canonical_spellings},
        {"missing optional children", missing_optional_children_are_omitted},
        {"literal byte escaping", literal_bytes_are_escaped_canonically},
    });
}
