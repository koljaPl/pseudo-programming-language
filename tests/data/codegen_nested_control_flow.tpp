bool ready(int value) {
    print(value);
    return value < 3;
}

bool probe(int value) {
    print(value * 10);
    return true;
}

int main() {
    vector<int> values = vector<int>(3, 0);
    values[0] = 1;
    values[1] = 2;
    values[2] = 3;
    int outer = 0;

    while ready(outer) {
        int saved = outer;
        { int outer = saved; print(outer); }

        for value in values {
            if value == 1 {
                continue;
            }

            if outer == 1 && probe(value) {
                break;
            }

            print(value);
        }

        outer += 1;
    }

    return 0;
}
