int main() {
    int n = read_int();
    int i = 0;

    while i < n {
        if i % 2 == 0 {
            print(i);
        }

        i += 1;
    }

    return 0;
}
