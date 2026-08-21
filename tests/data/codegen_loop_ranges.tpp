int begin_bound() {
    print("begin");
    return 2;
}

int end_bound() {
    print("end");
    return 4;
}

int main() {
    for value in begin_bound()..end_bound() {
        print(value);
    }

    for value in 5..3 {
        print(value);
    }

    for value in 3..3 {
        print(value);
    }

    for value in 5..=3 {
        print(value);
    }

    for value in -2..=-1 {
        print(value);
    }

    for value in -9223372036854775808..=-9223372036854775808 {
        print(value);
    }

    for value in 9223372036854775806..9223372036854775807 {
        print(value);
    }

    for value in 9223372036854775806..=9223372036854775807 {
        print(value);
        continue;
    }

    return 0;
}
