string decorate(string word, char marker) {
    word.push(marker);
    return word;
}

int sum_pair(vector<int> values) {
    return values[0] + values[1];
}

int main() {
    int seed = read_int();
    string word = read_string();
    char marker = read_char();

    vector<int> values = vector<int>(2, seed);
    values[1] += 1;
    vector<bool> flags = vector<bool>(2, true);
    flags[1] = false;

    print(decorate(word, marker));
    print(sum_pair(values));
    print(substring(word, 1, 4));

    for index in -1..=1 {
        print(index);
    }

    for outer in 0..2 {
        for inner in 0..=outer {
            print(outer * 10 + inner);
        }
    }

    for value in values {
        print(value);
    }

    for flag in flags {
        print(flag);
    }

    for character in "AZ" {
        print(character);
    }

    return 0;
}
