#include <cstdio>

// When building as part of cdo_pbt (CDO_TESTING defined), test_main.c provides main().
// This standalone main is only used for the separate cdo integration test binary.
#ifndef CDO_TESTING
int main() {
    std::printf("Tests for cdo\n");
    return 0;
}
#endif