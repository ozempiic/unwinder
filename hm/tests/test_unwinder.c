#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <winnt.h>

typedef BOOL (__stdcall *TestAndUnwindFunc)();
typedef BOOL (__stdcall *TestLeafFunc)();

void __declspec(noinline) deep_function_3() {
    printf("Entering deep_function_3\n");
    TestAndUnwindFunc test_func;
    HMODULE dll = LoadLibraryA(".\\unwinder.dll");  
    if (!dll) {
        printf("Failed to load unwinder.dll: %lu\n", GetLastError());
        return;
    }
    
    test_func = (TestAndUnwindFunc)GetProcAddress(dll, "test_and_unwind");
    if (!test_func) {
        printf("Failed to get test_and_unwind function: %lu\n", GetLastError());
        FreeLibrary(dll);
        return;
    }
    
    if (test_func()) {
        printf("Unwinding succeeded!\n");
    } else {
        printf("Unwinding failed!\n");
    }
    
    FreeLibrary(dll);
}

void __declspec(noinline) deep_function_2() {
    printf("Entering deep_function_2\n");
    deep_function_3();
}

void __declspec(noinline) deep_function_1() {
    printf("Entering deep_function_1\n");
    deep_function_2();
}

void __declspec(noinline) test_leaf_handling() {
    printf("\nTesting leaf function handling...\n");
    HMODULE dll = LoadLibraryA(".\\unwinder.dll");  
    if (!dll) {
        printf("Failed to load unwinder.dll: %lu\n", GetLastError());
        return;
    }
    
    TestLeafFunc test_func = (TestLeafFunc)GetProcAddress(dll, "test_leaf");
    if (!test_func) {
        printf("Failed to get test_leaf function: %lu\n", GetLastError());
        FreeLibrary(dll);
        return;
    }
    
    if (test_func()) {
        printf("Leaf function test succeeded!\n");
    } else {
        printf("Leaf function test failed!\n");
    }
    
    FreeLibrary(dll);
}

int main() {
    printf("Starting unwinder tests...\n\n");
    
    printf("Testing deep call stack unwinding...\n");
    deep_function_1();
    
    test_leaf_handling();
    
    printf("\nAll tests completed.\n");
    return 0;
}