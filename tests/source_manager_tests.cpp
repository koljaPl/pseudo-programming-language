#include "test_support.hpp"

#include "pseudo/source/source_manager.hpp"

#include <cstddef>
#include <string_view>

namespace {

using tpp::SourceLocation;
using tpp::SourceManager;
using tpp::SourceSpan;

void empty_source()
{
    SourceManager sources;
    const auto source = sources.add_source("empty.tpp", "");

    TPP_CHECK_EQ(sources.display_name(source), std::string_view("empty.tpp"));
    TPP_CHECK(sources.contents(source).empty());
    TPP_CHECK(sources.line_containing(SourceLocation{source, 0}).empty());

    const auto position = sources.position(SourceLocation{source, 0});
    TPP_CHECK_EQ(position.line, std::size_t{1});
    TPP_CHECK_EQ(position.column, std::size_t{1});
}

void source_ids_are_distinct()
{
    SourceManager sources;
    const auto first = sources.add_source("first.tpp", "first");
    const auto second = sources.add_source("second.tpp", "second");

    TPP_CHECK(first != second);
    TPP_CHECK_EQ(sources.contents(first), std::string_view("first"));
    TPP_CHECK_EQ(sources.contents(second), std::string_view("second"));
    TPP_CHECK_EQ(sources.display_name(first), std::string_view("first.tpp"));
    TPP_CHECK_EQ(sources.display_name(second), std::string_view("second.tpp"));
}

void positions_are_one_based()
{
    SourceManager sources;
    const auto source = sources.add_source("lines.tpp", "a\nbc\n");

    TPP_CHECK_EQ(sources.contents(source), std::string_view("a\nbc\n"));

    const auto first_line = sources.position(SourceLocation{source, 0});
    const auto second_line = sources.position(SourceLocation{source, 2});
    const auto second_character = sources.position(SourceLocation{source, 3});

    TPP_CHECK_EQ(first_line.line, std::size_t{1});
    TPP_CHECK_EQ(first_line.column, std::size_t{1});
    TPP_CHECK_EQ(second_line.line, std::size_t{2});
    TPP_CHECK_EQ(second_line.column, std::size_t{1});
    TPP_CHECK_EQ(second_character.line, std::size_t{2});
    TPP_CHECK_EQ(second_character.column, std::size_t{2});
}

void line_contents_exclude_newlines()
{
    SourceManager sources;
    const auto source = sources.add_source("lines.tpp", "a\nbc\n");

    TPP_CHECK_EQ(
        sources.line_containing(SourceLocation{source, 0}),
        std::string_view("a"));
    TPP_CHECK_EQ(
        sources.line_containing(SourceLocation{source, 2}),
        std::string_view("bc"));
}

void trailing_newline_creates_an_empty_line()
{
    SourceManager sources;
    const auto source = sources.add_source("trailing.tpp", "a\nbc\n");

    const auto eof = sources.position(SourceLocation{source, 5});
    TPP_CHECK_EQ(eof.line, std::size_t{3});
    TPP_CHECK_EQ(eof.column, std::size_t{1});
    TPP_CHECK(sources.line_containing(SourceLocation{source, 5}).empty());
}

void crlf_is_a_single_line_ending()
{
    SourceManager sources;
    const auto source = sources.add_source("windows.tpp", "a\r\nbc\r\n");

    const auto second_line = sources.position(SourceLocation{source, 3});
    const auto eof = sources.position(SourceLocation{source, 7});

    TPP_CHECK_EQ(second_line.line, std::size_t{2});
    TPP_CHECK_EQ(second_line.column, std::size_t{1});
    TPP_CHECK_EQ(eof.line, std::size_t{3});
    TPP_CHECK_EQ(eof.column, std::size_t{1});
    TPP_CHECK_EQ(
        sources.line_containing(SourceLocation{source, 0}),
        std::string_view("a"));
    TPP_CHECK_EQ(
        sources.line_containing(SourceLocation{source, 3}),
        std::string_view("bc"));
}

void eof_without_trailing_newline_has_a_position()
{
    SourceManager sources;
    const auto source = sources.add_source("source.tpp", "a\nbc");

    const auto eof = sources.position(SourceLocation{source, 4});
    TPP_CHECK_EQ(eof.line, std::size_t{2});
    TPP_CHECK_EQ(eof.column, std::size_t{3});
    TPP_CHECK_EQ(
        sources.line_containing(SourceLocation{source, 4}),
        std::string_view("bc"));
}

void slice_uses_a_half_open_range()
{
    SourceManager sources;
    const auto source = sources.add_source("source.tpp", "a\nbc\n");

    TPP_CHECK_EQ(
        sources.slice(SourceSpan{source, 2, 4}),
        std::string_view("bc"));
}

void zero_length_span_has_an_empty_slice()
{
    SourceManager sources;
    const auto source = sources.add_source("source.tpp", "a\nbc\n");

    TPP_CHECK(sources.slice(SourceSpan{source, 3, 3}).empty());
}

}

int main()
{
    return tpp::test::run({
        {"empty source", empty_source},
        {"source ids are distinct", source_ids_are_distinct},
        {"positions are one based", positions_are_one_based},
        {"line contents exclude newlines", line_contents_exclude_newlines},
        {"trailing newline creates an empty line", trailing_newline_creates_an_empty_line},
        {"CRLF is a single line ending", crlf_is_a_single_line_ending},
        {"EOF without trailing newline has a position", eof_without_trailing_newline_has_a_position},
        {"slice uses a half-open range", slice_uses_a_half_open_range},
        {"zero-length span has an empty slice", zero_length_span_has_an_empty_slice},
    });
}
