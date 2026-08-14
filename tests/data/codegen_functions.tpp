int add(int left, int right) {
    return left + right;
}

void greet(string name) {
    print(name);
}

bool is_even(int value) {
    return is_odd(value - 1);
}

bool is_odd(int value) {
    return is_even(value - 1);
}

char identity_char(char value) {
    return value;
}

string identity_string(string value) {
    return value;
}

int main() {
    greet("sum");
    print(add(2, 3));
    print(is_even(4));
    print(identity_char('x'));
    print(identity_string("done"));
    return 0;
}
