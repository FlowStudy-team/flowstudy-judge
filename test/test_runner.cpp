#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

static int g_passed = 0;
static int g_failed = 0;

void check(const std::string& name, bool condition, const std::string& detail = "") {
    if (condition) {
        g_passed++;
        std::cout << "[PASS] " << name << std::endl;
    } else {
        g_failed++;
        std::cout << "[FAIL] " << name;
        if (!detail.empty()) {
            std::cout << " — " << detail;
        }
        std::cout << std::endl;
    }
}

// Forward declarations from integration_test.cpp
void test_compare_output();
void test_parse_message();
void test_status_conversion();
void test_config_loading();

int main() {
    std::cout << "=== FlowStudy-Judge Unit Tests ===" << std::endl << std::endl;

    test_status_conversion();
    test_compare_output();
    test_parse_message();
    test_config_loading();

    std::cout << std::endl;
    std::cout << "Results: " << g_passed << " passed, "
              << g_failed << " failed" << std::endl;

    return g_failed > 0 ? 1 : 0;
}
