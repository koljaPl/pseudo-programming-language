Version 0.01 Alpha Goals

Since this is the very first version, don’t expect too much, but I plan to implement all standart
features of a transpiler programming language in C++, as well as the following features:

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
- for-each
- Comments
- Statement termination

The main goal is to implement a complete minimal transpilation pipeline:

Source code
-> Lexer
-> Tokens
-> Parser
-> AST
-> Name resolution
-> Type checking
-> C++20 code generation
-> Compilation with g++

A detailed description of what will be included in the first version:

1. Basic Types:
    Only 5 basic types:
    int    = signed 64-bit integer          -> std::int64_t
    bool   = true or false                  -> bool
    char   = one byte or an ASCII character -> chat
    string = a sequence of bytes            -> std::string
    void   = no return value                -> void

2. Literals:
    It's as simple as it gets:
    123
    0
    -42
    true
    false
    “hello” - string
    ‘a’ - char

3. Variables:
    For now, only explicit declarations:
    int x = 10
    string name = “Nicklas”

    In future versions, we’ll add support for type inference:
    x := 10
    name := “Nicklas”

    Also, assignment:
    x = 20

    And compound assignments:
    x += 5
    x -= 2
    x *= 3
    x /= 2
    x %= 10

4. Arithmetic Operations:
    Base:
    +
    -
    *
    /
    %

    Also unary:
    -x
    +x

5. Comparisons and Logic:
    Operators:
    ==
    !=
    <
    <=
    >
    >=

    Logical operators:
    && (and)
    || (or)
    ! (not)

    and of course:
    ()

6. Instructions:
    The first version must include:
    variable declaration
    assignment
    expression statement
    block
    if / else
    while
    for-range
    return
    break
    continue

    if:
        if x > 0 {
            print(x)
        } else {
            print(0)
        }
    
    while:
        while x < 10 {
            x += 1
        }
    
    for-range:
        for i in 0..n {
            print(i)
        }

        I'm not a fan of this approach, but it will be a turning point. 
        In future versions, I'll add everything as it's used in C++ and Python, 
        and I'll most likely recommend using those specifically.

    break and continue:
        while true {
            if condition {
                break
            }

            continue
        }

7. Functions:
    Standard functions and functions within functions, following the Python model,
    without the complex systems found in C++

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

        if is_greater(a, b) {
            return a
        } else {
            return b
        }
    } // i know its really dumb

8. Entry Point
    For a language used in competitive programming, it's best to use a C++-style approach,
    with the entry point being the `main` function in the file

9. Input and Output
    print(x)
    read_int()
    read_string()
    read_char()

10. Arrays
    For now, it will only support `vector<type>` (just like in C++),
    but only for testing purposes! Later (in future versions, or perhaps even in this one),
    we'll also support these types of arrays, something like that:

    Two options:
    Standard C++:
    `vector<type> bla_bla(x, x)` and `array<type, x>`
    And in Python:
    `[type]`

11. Indexing
    nums[i]
    text[i]

    And assignment:
    nums[i] = 10

12. Strings
    string s = “hello”

    s[i]
    len(s)
    s += “ world”
    s == “hello”

    Useful built-in operations:
    s.length()
    s.push(ch)
    substring(s, left, right)

    for ch in text {
        print(ch)
    }

13. for-each
    for value in values {
        print(value)
    }

    for ch in text {
        print(ch)
    }

    This is a copy; starting with version 0.02, there will be references like in C++

14. Comments
    For now, // is a comment; later, when language settings become available,
    this can be changed. It’s like this for now because the C++ syntax is similar,
    and so far the // operator isn’t used anywhere.

15. Line Break
    For now, there will only be a semicolon ( ; ), but just like with arrays,
    this is only for testing purposes; in the near future (in the next update),
    we'll have the following:

    A line break ( Enter ) terminates a statement.
    A semicolon ( ; ) can also terminate a statement.
    Line breaks inside (), [], and after statements are ignored.


Additional Information:
Backend:
    For now, only C++20 and g++. You'll be able to change this in the settings later.


Completion Criteria:

Version 0.01 Alpha is complete when the transpiler can correctly process programs containing:

Literals and arithmetic expressions
Variables and assignments
Conditions and loops
Top-level functions and function calls
A valid main entry point
Basic input and output
Basic string operations
Clear lexical, syntax, name-resolution, and type errors

For every required feature, the project should include:

lexer tests
parser and AST tests
semantic-analysis tests
code-generation tests
end-to-end compilation and execution tests

Stretch goals are not required for Version 0.01 to be considered complete.


I also plan to describe how my language will work in principle and its main concepts.

Additionally, some things may be added during development, so here is a list of everything that might be added along with the dates:

DD.MM.YYYY - Change title

New ideas should not automatically become requirements for Version 0.01.
