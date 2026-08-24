int main() {
    for outer in 0..3 {
        {
            print(outer);
        }

        for inner in 10..13 {
            print(inner);
            break;
        }

        for repeated in 0..2 {
            print(repeated);
            continue;
        }

        continue;
    }

    return 0;
}
