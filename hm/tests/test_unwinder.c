#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <winnt.h>
#include <xmmintrin.h> 

typedef BOOL (__stdcall *TestAndUnwindFunc)();
typedef BOOL (__stdcall *TestLeafFunc)();
typedef BOOL (__stdcall *TestPushFunc)();

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

void __declspec(noinline) test_xmm_function() {
    printf("\nTesting XMM unwind...\n");
    TestAndUnwindFunc test_func;
    HMODULE dll = LoadLibraryA(".\\unwinder.dll");  
    if (!dll) {
        printf("Failed to load unwinder.dll\n");
        return;
    }
    
    test_func = (TestAndUnwindFunc)GetProcAddress(dll, "test_and_unwind");
    if (!test_func) {
        printf("Failed to get test_and_unwind function\n");
        FreeLibrary(dll);
        return;
    }

    __m128 xmm_val = _mm_set_ps(1.0f, 2.0f, 3.0f, 4.0f);
    xmm_val = _mm_add_ps(xmm_val, xmm_val);
    
    BOOL ok = test_func();
    printf("XMM unwind %s\n", ok ? "succeeded!" : "failed!");
    
    FreeLibrary(dll);
}

void __declspec(noinline) test_unwind_ops() {
    printf("\nTesting manual-push/pop unwind ops...\n");
    HMODULE dll = LoadLibraryA(".\\unwinder.dll");  
    if (!dll) {
        printf("Failed to load unwinder.dll\n");
        return;
    }
    
    TestPushFunc push_fn = (TestPushFunc)GetProcAddress(dll, "test_push_nonvol");
    if (!push_fn) {
        printf("Failed to get test_push_nonvol: %lu\n", GetLastError());
        FreeLibrary(dll);
        return;
    }

    printf("  -- about to invoke test_push_nonvol()\n");
    BOOL ok = push_fn();
    printf("  -- returned from test_push_nonvol(), ok=%d\n", ok);
    printf("Manual-ops unwind %s\n", ok ? "succeeded!" : "failed!");
    
    FreeLibrary(dll);
}

int main() {
    printf("Starting unwinder tests...\n\n");
    
    printf("Testing deep call stack unwinding...\n");
    deep_function_1();
    
    test_leaf_handling();
    test_xmm_function();  
    test_unwind_ops();    
    
    printf("\nAll tests completed.\n");
    return 0;
}
