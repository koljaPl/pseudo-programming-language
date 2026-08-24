string next_word() {
    return read_string();
}

char next_symbol() {
    return read_char();
}

int main() {
    string word = next_word();
    char symbol = next_symbol();
    print(word);
    print(symbol);
    return 0;
}
