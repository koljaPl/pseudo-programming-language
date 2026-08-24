string decorate(string text, char first) {
    text[0] = first;
    text += "!";
    text.push('?');
    return text;
}

char second(string text) {
    return text[1];
}

int main() {
    string value = "cat";
    char first = 'B';
    print(decorate(value, first));
    print("ab" + "cd");
    print("same" == "same");
    print("a" != "b");
    print("a" < "b");
    print("a" <= "a");
    print("b" > "a");
    print("b" >= "b");
    print(len("A\0B"));
    print(substring("abcdef", 1, 4));
    print(value.length());
    value.push('!');
    value[0] = 'C';
    print(value);
    print(second("xy"));
    return 0;
}
