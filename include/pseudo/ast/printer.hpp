#pragma once

#include <iosfwd>

namespace tpp {

struct Program;

void print_ast(std::ostream& output, const Program& program);

}
