# Version 0.01 — Alpha Goals

As this is the very first version, don't expect too much. I plan to implement the standard features of a transpiled programming language in C++, as well as the following:

- Basic types
- Literals
- Variables
- Arithmetic operations
- Comparisons and logic
- Statements
- Functions
- Entry point
- Input and output
- Arrays
- Indexing
- Strings
- `for-each`
- Comments
- Statement termination

The main goal is to implement a complete, minimal transpilation pipeline:

```text
Source code
-> Lexer
-> Tokens
-> Parser
-> AST
-> Name resolution
-> Type checking
-> C++20 code generation
-> Compilation with g++
```

Below is a detailed description of what the first version will include.

## 1. Basic Types

Only five basic types:

```text
int    = signed 64-bit integer          -> std::int64_t
bool   = true or false                  -> bool
char   = one byte or an ASCII character -> char
string = a sequence of bytes            -> std::string
void   = no return value                -> void
```

## 2. Literals

It's as simple as it gets:

```text
123
0
-42
true
false
“hello”  - string
‘a’      - char
```

## 3. Variables

For now, only explicit declarations:

```text
int x = 10
string name = “Nicklas”
```

In future versions, we'll add support for type inference:

```text
x := 10
name := “Nicklas”
```

Assignment:

```text
x = 20
```

Compound assignments:

```text
x += 5
x -= 2
x *= 3
x /= 2
x %= 10
```

## 4. Arithmetic Operations

Basic operators:

```text
+
-
*
/
%
```

Unary operators:

```text
-x
+x
```

## 5. Comparisons and Logic

Comparison operators:

```text
==
!=
<
<=
>
>=
```

Logical operators:

```text
&& (and)
|| (or)
!  (not)
```

And, of course, parentheses:

```text
()
```

## 6. Statements

The first version must include:

- Variable declarations
- Assignments
- Expression statements
- Blocks
- `if` / `else`
- `while`
- `for-range`
- `return`
- `break`
- `continue`

`if`:

```text
if x > 0 {
    print(x)
} else {
    print(0)
}
```

`while`:

```text
while x < 10 {
    x += 1
}
```

`for-range`:

```text
for i in 0..n {
    print(i)
}
```

I'm not a fan of this approach, but it will be a turning point. In future versions, I'll add everything as it's used in C++ and Python, and I'll most likely recommend using those approaches instead.

`break` and `continue`:

```text
while true {
    if condition {
        break
    }

    continue
}
```

## 7. Functions

Standard functions and functions within functions, following the Python model, without the complex systems found in C++:

```text
int max_value(int a, int b) {
    if a > b {
        return a
    }

    return b
}

void greet(string name) {
    print(name)
}

int min_value(int a, int b) {
    bool is_first_greater(int x, int y) {
        return x > y
    }

    if is_first_greater(a, b) {
        return a
    } else {
        return b
    }
}
```

## 8. Entry Point

For a language used in competitive programming, a C++-style approach is best: the entry point is the `main` function in the file.

## 9. Input and Output

```text
print(x)
read_int()
read_string()
read_char()
```

## 10. Arrays

For now, only `vector<type>` (as in C++) will be supported, and only for testing purposes. Later—perhaps even in this version—we'll also support the following array forms:

```text
Standard C++:
vector<type> bla_bla(x, x)
array<type, x>

Python:
[type]
```

## 11. Indexing

```text
nums[i]
text[i]
```

Assignment by index:

```text
nums[i] = 10
```

## 12. Strings

```text
string s = “hello”

s[i]
len(s)
s += “ world”
s == “hello”
```

Useful built-in operations:

```text
s.length()
s.push(ch)
substring(s, left, right)
```

```text
for ch in text {
    print(ch)
}
```

## 13. `for-each`

```text
for value in values {
    print(value)
}

for ch in text {
    print(ch)
}
```

This is a copy; starting with version 0.02, there will be references, as in C++.

## 14. Comments

For now, `//` starts a comment. Later, when language settings become available, this can be changed. It is used for now because the syntax is similar to C++ and the `//` operator is not used anywhere yet.

## 15. Statement Termination

For now, only a semicolon (`;`) terminates a statement. As with arrays, this is only for testing purposes. In the near future (the next update), the following will be supported:

- A line break (`Enter`) terminates a statement.
- A semicolon (`;`) can also terminate a statement.
- Line breaks inside `()`, `[]`, and after statements are ignored.

## Additional Information

### Backend

For now, only C++20 and `g++` are supported. You'll be able to change this in the settings later.

### Completion Criteria

Version 0.01 Alpha is complete when the transpiler can correctly process programs containing:

- Literals and arithmetic expressions
- Variables and assignments
- Conditions and loops
- Top-level functions and function calls
- A valid `main` entry point
- Basic input and output
- Basic string operations
- Clear lexical, syntax, name-resolution, and type errors

For every required feature, the project should include:

- Lexer tests
- Parser and AST tests
- Semantic-analysis tests
- Code-generation tests
- End-to-end compilation and execution tests

Stretch goals are not required for Version 0.01 to be considered complete.

I also plan to describe, in principle, how my language will work and its main concepts.

Additionally, some things may be added during development, so here is a list of everything that might be added along with the dates:

```text
DD.MM.YYYY - Change title
```

New ideas should not automatically become requirements for Version 0.01
