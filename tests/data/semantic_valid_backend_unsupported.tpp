int outer(int limit) {
    int increment(int value) {
        return value + 1;
    }

    int current = 0;
    while current < limit {
        if current == 2 {
            return current;
        }

        current = increment(current);
    }

    return current;
}

int main() {
    return outer(4);
}
