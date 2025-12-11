#pragma once

#include <iostream>
#include <string>
#include <vector>
#include <cassert>
#include <cmath>
#include <functional>

// Simple test framework for unit tests
class TestFramework {
public:
    static TestFramework& instance() {
        static TestFramework inst;
        return inst;
    }
    
    void runTest(const std::string& name, std::function<bool()> test) {
        std::cout << "  Running: " << name << "... ";
        try {
            if (test()) {
                std::cout << "PASSED" << std::endl;
                passed++;
            } else {
                std::cout << "FAILED" << std::endl;
                failed++;
                failedTests.push_back(name);
            }
        } catch (const std::exception& e) {
            std::cout << "FAILED (exception: " << e.what() << ")" << std::endl;
            failed++;
            failedTests.push_back(name);
        } catch (...) {
            std::cout << "FAILED (unknown exception)" << std::endl;
            failed++;
            failedTests.push_back(name);
        }
        total++;
    }
    
    void printSummary() {
        std::cout << "\n========================================\n";
        std::cout << "Test Summary:\n";
        std::cout << "  Total:  " << total << "\n";
        std::cout << "  Passed: " << passed << "\n";
        std::cout << "  Failed: " << failed << "\n";
        std::cout << "========================================\n";
        
        if (!failedTests.empty()) {
            std::cout << "\nFailed tests:\n";
            for (const auto& test : failedTests) {
                std::cout << "  - " << test << "\n";
            }
        }
    }
    
    int getTotal() const { return total; }
    int getPassed() const { return passed; }
    int getFailed() const { return failed; }
    
    void reset() {
        total = 0;
        passed = 0;
        failed = 0;
        failedTests.clear();
    }
    
private:
    TestFramework() : total(0), passed(0), failed(0) {}
    int total;
    int passed;
    int failed;
    std::vector<std::string> failedTests;
};

// Helper macros
#define ASSERT_TRUE(condition) \
    do { \
        if (!(condition)) { \
            std::cerr << "\n    Assertion failed: " << #condition << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
            return false; \
        } \
    } while(0)

#define ASSERT_FALSE(condition) ASSERT_TRUE(!(condition))

#define ASSERT_EQ(a, b) \
    do { \
        auto _a_val = (a); \
        auto _b_val = (b); \
        if (_a_val != _b_val) { \
            std::cerr << "\n    Assertion failed: " << #a << " == " << #b << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
            return false; \
        } \
    } while(0)

#define ASSERT_NE(a, b) \
    do { \
        if ((a) == (b)) { \
            std::cerr << "\n    Assertion failed: " << #a << " != " << #b << " (both are " << (a) << ") at " << __FILE__ << ":" << __LINE__ << std::endl; \
            return false; \
        } \
    } while(0)

#define ASSERT_FLOAT_EQ(a, b, epsilon) \
    do { \
        float diff = std::abs((a) - (b)); \
        if (diff > (epsilon)) { \
            std::cerr << "\n    Assertion failed: " << #a << " == " << #b << " (got " << (a) << " != " << (b) << ", diff=" << diff << ") at " << __FILE__ << ":" << __LINE__ << std::endl; \
            return false; \
        } \
    } while(0)

#define TEST(name) TestFramework::instance().runTest(name, [&]() -> bool
