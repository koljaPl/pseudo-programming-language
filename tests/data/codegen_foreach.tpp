vector<bool> make_flags() {
    return vector<bool>(2, true);
}

vector<vector<int>> make_rows() {
    return vector<vector<int>>(2, vector<int>(2, 6));
}

int main() {
    vector<int> values = vector<int>(3, 7);
    values[1] = 8;
    for value in values {
        print(value);
    }

    vector<bool> flags = vector<bool>(2, false);
    flags[1] = true;
    for flag in flags {
        print(flag);
    }

    vector<char> letters = vector<char>(2, 'x');
    for letter in letters {
        print(letter);
    }

    vector<string> words = vector<string>(2, "word");
    for word in words {
        word.push('!');
        print(word);
    }
    print(words[0]);

    vector<vector<int>> rows =
        vector<vector<int>>(2, vector<int>(1, 1));
    for row in rows {
        row[0] = 9;
        print(row[0]);
        for row in row {
            print(row);
        }
    }
    print(rows[0][0]);

    for character in "az" {
        print(character);
    }

    for number in vector<int>(2, 4) {
        print(number);
    }

    for flag in make_flags() {
        print(flag);
    }

    for value in make_rows()[0] {
        print(value);
    }

    vector<int> snapshot_values = vector<int>(3, 5);
    for value in snapshot_values {
        print(value);
        snapshot_values = vector<int>(1, 9);
    }
    print(snapshot_values[0]);

    return 0;
}
