#include "test_framework.h"
#include <iostream>

// Forward declarations
void runSVOTests();
void runChunkTests();

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "  CHONK Unit Tests" << std::endl;
    std::cout << "========================================\n" << std::endl;
    
    // Run SVO tests
    runSVOTests();
    
    // Run Chunk tests
    runChunkTests();
    
    // Print final summary
    TestFramework& framework = TestFramework::instance();
    framework.printSummary();
    
    // Return exit code based on test results
    return framework.getFailed() > 0 ? 1 : 0;
}

