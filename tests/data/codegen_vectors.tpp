vector<int> copy(vector<int> values) {
    return values;
}

bool second(vector<bool> flags) {
    return flags[1];
}

int main() {
    vector<int> values = vector<int>(3, 7);
    vector<int> other = vector<int>();
    other = values;
    values[1] = 42;
    values[2] += 1;

    vector<bool> flags = vector<bool>(2, false);
    flags[1] = true;

    print(copy(values)[0]);
    print(other[1]);
    print(values[1]);
    print(values[2]);
    print(second(flags));
    return 0;
}
