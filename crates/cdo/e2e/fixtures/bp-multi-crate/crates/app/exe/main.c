extern int base_multiply(int a, int b);

int main(void) {
    int result = base_multiply(3, 4);
    return result == 12 ? 0 : 1;
}
