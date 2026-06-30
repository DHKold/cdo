extern int full_square(int x);

int main(void) {
    int pass = 1;
    if (full_square(0) != 0) pass = 0;
    if (full_square(3) != 9) pass = 0;
    if (full_square(-2) != 4) pass = 0;
    return pass ? 0 : 1;
}
