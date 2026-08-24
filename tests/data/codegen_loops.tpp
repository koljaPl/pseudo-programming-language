int main() {
    for value in 0..2 {
        print(value);
    }

    for value in 2..=3 {
        print(value);
        continue;
    }

    vector<bool> flags = vector<bool>(2, true);
    for flag in flags {
        print(flag);
        break;
    }

    for character in "ok" {
        print(character);
    }

    return 0;
}
