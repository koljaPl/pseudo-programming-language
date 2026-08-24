string choose(vector<string> values, int index) {
    return values[index];
}

int main() {
    vector<vector<int>> matrix =
        vector<vector<int>>(2, vector<int>(3, 0));
    matrix[1][2] = 5;

    vector<string> words = vector<string>(2, "hi");
    words[0][1] = 'o';
    words[0].push('!');

    vector<vector<string>> labels =
        vector<vector<string>>(1, vector<string>(1, "xy"));
    labels[0][0][0] = 'Z';
    labels[0][0].push('!');

    print(matrix[1][2]);
    print(words[0]);
    print(labels[0][0]);
    print(choose(words, 1).length());
    print(choose(words, 1)[0]);
    return 0;
}
