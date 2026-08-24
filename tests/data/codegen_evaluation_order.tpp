void observe(int first, int second) {
    print(first);
    print(second);
}

bool mark(bool value) {
    print(value);
    return value;
}

int main() {
    observe(read_int(), read_int());
    print(substring(read_string(), read_int(), read_int()));

    vector<int> values = vector<int>(1, 0);
    values[read_int()] = read_int();
    print(values[0]);

    vector<int> constructed = vector<int>(read_int(), read_int());
    print(constructed[0]);
    print(vector<int>(read_int(), read_int())[read_int()]);

    vector<string> texts = vector<string>(1, "a");
    texts[read_int()].push(read_char());
    print(texts[0]);

    print(read_int() - read_int());
    print(mark(false) && mark(true));
    print(mark(true) || mark(false));
    return 0;
}
