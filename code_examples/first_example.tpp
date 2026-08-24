int global = 10;

int add(int left, int right) {
    return left + right;
}

int main() {
    int result = add(global, 32);

    if (result > 40) {
        return result;
    } else {
        return 0;
    }
}