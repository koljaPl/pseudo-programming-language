int global;

int identity(int value) {
    return value;
}

int main() {
    int local = global;
    local = identity(local);
    print(local);
    return 0;
}
