// Console runner for the PA250Sim test cases.
#include <cstdio>
#include <cstring>

#include "sim_cases.hpp"

struct ConsoleReporter : pa250_tests::Reporter {
    void failure(const char* file, int line, const char* expr) override {
        std::printf("    CHECK FAILED %s:%d: %s\n", file, line, expr);
    }
    void message(const std::string& text) override { std::printf("    %s\n", text.c_str()); }
    void test_done(const char* name, bool ok) override { std::printf("[%s] %s\n", ok ? " OK " : "FAIL", name); }
};

int main(int argc, char** argv) {
    bool write_golden = argc > 1 && std::strcmp(argv[1], "--write-golden") == 0;
    ConsoleReporter reporter;
    int failed = pa250_tests::run_all(reporter, PA250_DATA_DIR, PA250_VECTOR_DIR, write_golden);
    std::printf("%d checks, %d failed\n", pa250_tests::g_checks(), failed);
    return failed == 0 ? 0 : 1;
}
