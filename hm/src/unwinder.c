#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <winnt.h>  
#include <stdarg.h>

#define UNWINDER_API __declspec(dllexport)

typedef enum _UNWINDER_DEBUG_LEVEL {
    UW_DEBUG_NONE = 0,
    UW_DEBUG_ERROR = 1,
    UW_DEBUG_WARN = 2,
    UW_DEBUG_INFO = 3,
    UW_DEBUG_VERBOSE = 4
} UNWINDER_DEBUG_LEVEL;

typedef struct _UNWINDER_DEBUG_CONFIG {
    UNWINDER_DEBUG_LEVEL level;
    FILE* output;
    BOOL includeTimestamp;
} UNWINDER_DEBUG_CONFIG;

typedef struct _UNWINDER_ERROR {
    DWORD code;
    char message[256];
    DWORD64 address;
} UNWINDER_ERROR;

static UNWINDER_DEBUG_CONFIG g_debugConfig = {0};
static UNWINDER_ERROR g_lastError = {0};

typedef union _UNWIND_CODE {
    struct {
        BYTE CodeOffset;
        BYTE UnwindOp : 4;
        BYTE OpInfo : 4;
    };
    USHORT FrameOffset;
} UNWIND_CODE;

typedef struct _UNWIND_INFO {
    BYTE Version : 3;
    BYTE Flags : 5;
    BYTE SizeOfProlog;
    BYTE CountOfCodes;
    BYTE FrameRegister : 4;
    BYTE FrameOffset : 4;
    UNWIND_CODE UnwindCode[1];
} UNWIND_INFO;

typedef struct _UNWINDER_CONTEXT {
    DWORD64 rip;        
    DWORD64 rsp;        
    DWORD64 rbp;        
    DWORD64 registers[16]; 
    M128A xmm_registers[16];  
    DWORD64 flags;            
} UNWINDER_CONTEXT;

static void debug_print(UNWINDER_DEBUG_LEVEL level, const char* format, ...);
static void set_error(DWORD code, const char* message, DWORD64 address);
static BOOL process_runtime_function(UNWINDER_CONTEXT* ctx, RUNTIME_FUNCTION* rfn, DWORD64 imageBase);
static BOOL process_scope_table(UNWINDER_CONTEXT* ctx, UNWIND_INFO* unwindInfo, DWORD64 imageBase);
UNWINDER_API BOOL process_unwind_codes(UNWINDER_CONTEXT* ctx, UNWIND_INFO* unwind_info);
UNWINDER_API BOOL handle_leaf_function(UNWINDER_CONTEXT* ctx);
UNWINDER_API BOOL process_exception_handler(UNWINDER_CONTEXT* ctx, UNWIND_INFO* unwind_info, 
                                          DWORD64 imageBase, EXCEPTION_RECORD* exception);
UNWINDER_API EXCEPTION_DISPOSITION WINAPI MyExceptionDispatcher(
    EXCEPTION_RECORD* ExceptionRecord,
    PVOID EstablisherFrame,
    PCONTEXT ContextRecord,
    PVOID DispatcherContext
);

UNWINDER_API BOOL init_unwinder_context(UNWINDER_CONTEXT* ctx, CONTEXT* win_ctx) {
    if (!ctx || !win_ctx) return FALSE;
    
    ctx->rip = win_ctx->Rip;
    ctx->rsp = win_ctx->Rsp;
    ctx->rbp = win_ctx->Rbp;
    
    ctx->registers[0] = win_ctx->Rax;
    ctx->registers[1] = win_ctx->Rcx;
    ctx->registers[2] = win_ctx->Rdx;
    ctx->registers[3] = win_ctx->Rbx;
    ctx->registers[4] = win_ctx->Rsi;
    ctx->registers[5] = win_ctx->Rdi;
    ctx->registers[8] = win_ctx->R8;
    ctx->registers[9] = win_ctx->R9;
    ctx->registers[10] = win_ctx->R10;
    ctx->registers[11] = win_ctx->R11;
    ctx->registers[12] = win_ctx->R12;
    ctx->registers[13] = win_ctx->R13;
    ctx->registers[14] = win_ctx->R14;
    ctx->registers[15] = win_ctx->R15;
    
    return TRUE;
}

typedef struct _CHAINED_UNWIND_INFO {
    RUNTIME_FUNCTION PrimaryBlock;
    RUNTIME_FUNCTION ChainedBlock;
} CHAINED_UNWIND_INFO;

UNWINDER_API BOOL unwind_frame(UNWINDER_CONTEXT* ctx) {
    if (!ctx) {
        set_error(1, "Invalid context pointer", 0);
        return FALSE;
    }

    debug_print(UW_DEBUG_INFO, "Looking up unwind entry for RIP=0x%p\n", (PVOID)ctx->rip);
    
    DWORD64 imageBase;
    RUNTIME_FUNCTION* rfn = RtlLookupFunctionEntry(ctx->rip, &imageBase, NULL);
    debug_print(UW_DEBUG_INFO, "  → RtlLookupFunctionEntry returned %p (imageBase=0x%p)\n", 
               (void*)rfn, (void*)imageBase);

    if (rfn) {
        UNWIND_INFO* unwindInfo = (UNWIND_INFO*)(imageBase + rfn->UnwindData);
        
        if (unwindInfo->Flags & UNW_FLAG_EHANDLER) {
            debug_print(UW_DEBUG_INFO, "Processing exception handler data\n");
            if (!process_scope_table(ctx, unwindInfo, imageBase)) {
                set_error(2, "Failed to process scope table", ctx->rip);
                return FALSE;
            }
        }
        
        if (unwindInfo->Flags & UNW_FLAG_CHAININFO) {
            debug_print(UW_DEBUG_INFO, "Found chained unwind info\n");
            RUNTIME_FUNCTION* chainedFn = (RUNTIME_FUNCTION*)(
                (BYTE*)unwindInfo + 
                sizeof(UNWIND_INFO) - sizeof(UNWIND_CODE) +
                unwindInfo->CountOfCodes * sizeof(UNWIND_CODE)
            );
            chainedFn = (RUNTIME_FUNCTION*)(((DWORD64)chainedFn + 3) & ~3);
            
            UNWINDER_CONTEXT chainedCtx = *ctx;
            if (!process_runtime_function(&chainedCtx, chainedFn, imageBase)) {
                set_error(3, "Failed to process chained function", ctx->rip);
                return FALSE;
            }
            *ctx = chainedCtx;
        }
    }
    
    CONTEXT winCtx = {0};
    winCtx.ContextFlags = CONTEXT_ALL;  
    winCtx.Rip = ctx->rip;
    winCtx.Rsp = ctx->rsp;
    winCtx.Rbp = ctx->rbp;
    winCtx.Rax = ctx->registers[0];
    winCtx.Rcx = ctx->registers[1];
    winCtx.Rdx = ctx->registers[2];
    winCtx.Rbx = ctx->registers[3];
    winCtx.Rsi = ctx->registers[4];
    winCtx.Rdi = ctx->registers[5];
    winCtx.R8  = ctx->registers[8];
    winCtx.R9  = ctx->registers[9];
    winCtx.R10 = ctx->registers[10];
    winCtx.R11 = ctx->registers[11];
    winCtx.R12 = ctx->registers[12];
    winCtx.R13 = ctx->registers[13];
    winCtx.R14 = ctx->registers[14];
    winCtx.R15 = ctx->registers[15];

    if (rfn) {
        PVOID handlerData = NULL;
        DWORD64 establisherFrame = 0;
        KNONVOLATILE_CONTEXT_POINTERS contextPointers = {0};

        PVOID handler = RtlVirtualUnwind(
            UNW_FLAG_NHANDLER,
            imageBase,
            winCtx.Rip,
            rfn,
            &winCtx,
            &handlerData,        
            &establisherFrame,   
            &contextPointers     
        );
        printf("  RtlVirtualUnwind returned handler=0x%p\n", handler);
    } else {
        printf("  No unwind info, using leaf handler\n");
        if (!handle_leaf_function(ctx)) {
            printf("  Leaf handler failed\n");
            return FALSE;
        }
        return TRUE;
    }

    ctx->registers[0] = winCtx.Rax;
    ctx->registers[1] = winCtx.Rcx;
    ctx->registers[2] = winCtx.Rdx;
    ctx->registers[3] = winCtx.Rbx;
    ctx->registers[4] = winCtx.Rsi;
    ctx->registers[5] = winCtx.Rdi;
    ctx->registers[8] = winCtx.R8;
    ctx->registers[9] = winCtx.R9;
    ctx->registers[10] = winCtx.R10;
    ctx->registers[11] = winCtx.R11;
    ctx->registers[12] = winCtx.R12;
    ctx->registers[13] = winCtx.R13;
    ctx->registers[14] = winCtx.R14;
    ctx->registers[15] = winCtx.R15;
    ctx->rip = winCtx.Rip;
    ctx->rsp = winCtx.Rsp;
    ctx->rbp = winCtx.Rbp;

    printf("  Post-unwind RIP=0x%p, RSP=0x%p, RBP=0x%p\n", 
           (PVOID)ctx->rip, (PVOID)ctx->rsp, (PVOID)ctx->rbp);

    return TRUE;
}

UNWINDER_API BOOL test_leaf() {
    CONTEXT c = {0};
    c.ContextFlags = CONTEXT_FULL;
    RtlCaptureContext(&c);
    
    printf("Test leaf function:\n");
    printf("  Initial RIP: 0x%p\n", (PVOID)c.Rip);
    printf("  Initial RSP: 0x%p\n", (PVOID)c.Rsp);
    printf("  Initial RBP: 0x%p\n", (PVOID)c.Rbp);
    
    UNWINDER_CONTEXT u = {0};
    if (!init_unwinder_context(&u, &c)) {
        printf("  Failed to initialize unwinder context\n");
        return FALSE;
    }
    
    BOOL result = handle_leaf_function(&u);
    printf("  Leaf handler %s\n", result ? "succeeded" : "failed");
    if (result) {
        printf("  New RIP: 0x%p\n", (PVOID)u.rip);
        printf("  New RSP: 0x%p\n", (PVOID)u.rsp);
        printf("  New RBP: 0x%p\n", (PVOID)u.rbp);
    }
    return result;
}

UNWINDER_API BOOL process_unwind_codes(UNWINDER_CONTEXT* ctx, UNWIND_INFO* unwind_info) {
    if (!ctx || !unwind_info) return FALSE;
    
    for (BYTE i = 0; i < unwind_info->CountOfCodes; i++) {
        UNWIND_CODE* code = &unwind_info->UnwindCode[i];
        
        switch (code->UnwindOp) {
            case 0: 
                ctx->rsp += 8;  
                break;
            case 1: 
                if (code->OpInfo == 0) {
                    ctx->rsp += (DWORD64)unwind_info->UnwindCode[i + 1].FrameOffset * 8;
                    i++;
                } else {
                    ctx->rsp += *(DWORD32*)&unwind_info->UnwindCode[i + 1];
                    i += 2;
                }
                break;
            case 2: 
                ctx->rsp += (code->OpInfo + 1) * 8;
                break;
            case 3: 
                ctx->rsp = ctx->registers[unwind_info->FrameRegister] - 
                          (unwind_info->FrameOffset * 16);
                break;
            
            case 4: 
                ctx->rsp -= 8;
                ctx->registers[code->OpInfo] = *(DWORD64*)(ctx->rsp);
                break;
                
            case 5: 
                {
                    DWORD64 offset = unwind_info->UnwindCode[i + 1].FrameOffset * 8;
                    ctx->registers[code->OpInfo] = *(DWORD64*)(ctx->rsp + offset);
                    i++;
                }
                break;
                
            case 8: 
                {
                    DWORD64 offset = unwind_info->UnwindCode[i + 1].FrameOffset * 16;
                    ctx->xmm_registers[code->OpInfo] = *(M128A*)(ctx->rsp + offset);
                    i++;
                }
                break;
        }
    }
    
    return TRUE;
}

UNWINDER_API BOOL handle_leaf_function(UNWINDER_CONTEXT* ctx) {
    if (!ctx) return FALSE;
    
    printf("Entering handle_leaf_function\n");
    printf("  Initial RSP=0x%p\n", (PVOID)ctx->rsp);
    
    if (ctx->rsp == 0) {
        printf("  Invalid RSP (NULL), aborting leaf handler\n");
        return FALSE;
    }
    
    if (IsBadReadPtr((PVOID)ctx->rsp, sizeof(DWORD64))) {
        printf("  Invalid RSP memory access at 0x%p, aborting leaf handler\n", (PVOID)ctx->rsp);
        return FALSE;
    }
    
    DWORD64 original_rsp = ctx->rsp;
    ctx->rip = *(DWORD64*)original_rsp;  
    ctx->rsp = original_rsp + 8;  
    
    printf("  Leaf handler success: new RIP=0x%p, new RSP=0x%p\n", 
           (PVOID)ctx->rip, (PVOID)ctx->rsp);
    return TRUE;
}

UNWINDER_API BOOL test_and_unwind() {
    printf("Entered test_and_unwind\n"); 
    
    CONTEXT ctx = {0};
    ctx.ContextFlags = CONTEXT_FULL;
    RtlCaptureContext(&ctx);
    
    printf("Initial native context:\n");
    printf("  RIP: 0x%p\n", (PVOID)ctx.Rip);
    printf("  RSP: 0x%p\n", (PVOID)ctx.Rsp);
    printf("  RBP: 0x%p\n", (PVOID)ctx.Rbp);
    
    UNWINDER_CONTEXT u = {0};
    if (!init_unwinder_context(&u, &ctx)) {
        printf("Failed to initialize unwinder context\n");
        return FALSE;
    }

    return unwind_frame(&u);
}

UNWINDER_API BOOL init_windows_context(CONTEXT* win_ctx, UNWINDER_CONTEXT* ctx) {
    if (!win_ctx || !ctx) return FALSE;
    
    win_ctx->ContextFlags = CONTEXT_ALL;
    win_ctx->Rip = ctx->rip;
    win_ctx->Rsp = ctx->rsp;
    win_ctx->Rbp = ctx->rbp;
    
    win_ctx->Rax = ctx->registers[0];
    win_ctx->Rcx = ctx->registers[1];
    win_ctx->Rdx = ctx->registers[2];
    win_ctx->Rbx = ctx->registers[3];
    win_ctx->Rsi = ctx->registers[4];
    win_ctx->Rdi = ctx->registers[5];
    win_ctx->R8 = ctx->registers[8];
    win_ctx->R9 = ctx->registers[9];
    win_ctx->R10 = ctx->registers[10];
    win_ctx->R11 = ctx->registers[11];
    win_ctx->R12 = ctx->registers[12];
    win_ctx->R13 = ctx->registers[13];
    win_ctx->R14 = ctx->registers[14];
    win_ctx->R15 = ctx->registers[15];
    
    if (ctx->flags & CONTEXT_XSTATE) {
        memcpy(&win_ctx->Xmm0, ctx->xmm_registers, sizeof(M128A) * 16);
    }
    
    return TRUE;
}

UNWINDER_API BOOL process_exception_handler(UNWINDER_CONTEXT* ctx, UNWIND_INFO* unwind_info, 
                                          DWORD64 imageBase, EXCEPTION_RECORD* exception) {
    if (!ctx || !unwind_info) 
        return FALSE;
    
    BYTE* unwindInfoPtr = (BYTE*)unwind_info;
    BYTE* handlerDataPtr = unwindInfoPtr + 
        sizeof(UNWIND_INFO) - sizeof(UNWIND_CODE) + 
        unwind_info->CountOfCodes * sizeof(UNWIND_CODE);
    
    handlerDataPtr = (BYTE*)(((DWORD64)handlerDataPtr + 3) & ~3);
    
    PEXCEPTION_ROUTINE handler = (PEXCEPTION_ROUTINE)(imageBase + *(DWORD*)handlerDataPtr);
    if (!handler) return FALSE;
    
    CONTEXT winCtx = {0};
    if (!init_windows_context(&winCtx, ctx)) {
        return FALSE;
    }
    
    DISPATCHER_CONTEXT dispCtx = {0};
    
    dispCtx.ControlPc = ctx->rip;
    dispCtx.FunctionEntry = RtlLookupFunctionEntry(ctx->rip, &imageBase, NULL);
    dispCtx.EstablisherFrame = ctx->rsp;
    dispCtx.ContextRecord = &winCtx;
    dispCtx.HandlerData = handlerDataPtr;
    dispCtx.ImageBase = imageBase;
    dispCtx.HistoryTable = NULL;  
    dispCtx.ScopeIndex = 0;
    
    return handler(exception, (PVOID)ctx->rsp, &winCtx, &dispCtx) == ExceptionContinueSearch;
}


LONG CALLBACK VectoredHandler(PEXCEPTION_POINTERS info) {
    DWORD code = info->ExceptionRecord->ExceptionCode;
    if (code != EXCEPTION_BREAKPOINT)  
        return EXCEPTION_CONTINUE_SEARCH;

    printf(">>> Handling our breakpoint at RIP=0x%p\n", 
           (PVOID)info->ContextRecord->Rip);

    DWORD64 controlPc = (DWORD64)info->ContextRecord->Rip;
    DWORD64 imageBase;
    RUNTIME_FUNCTION* rfn = RtlLookupFunctionEntry(controlPc, &imageBase, NULL);
    if (!rfn) {
        printf("No unwind info for 0x%p\n", (PVOID)controlPc);
        return EXCEPTION_CONTINUE_SEARCH;
    }

    UNWIND_INFO* ui = (UNWIND_INFO*)(imageBase + rfn->UnwindData);
    UNWINDER_CONTEXT u = {0};
    init_unwinder_context(&u, info->ContextRecord);

    if (process_exception_handler(&u, ui, imageBase, info->ExceptionRecord)) {
        printf("Dispatcher ran OK!\n");
        init_windows_context(info->ContextRecord, &u);
        return EXCEPTION_CONTINUE_EXECUTION;
    }

    return EXCEPTION_CONTINUE_SEARCH;
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
    switch (fdwReason) {
        case DLL_PROCESS_ATTACH:
            g_debugConfig.level = UW_DEBUG_INFO;
            g_debugConfig.output = stderr;
            g_debugConfig.includeTimestamp = TRUE;
            break;
    }
    return TRUE;
}

typedef struct _UNWINDER_SCOPE_TABLE_ENTRY {
    DWORD BeginAddress;
    DWORD EndAddress;
    DWORD HandlerAddress;
    DWORD JumpTarget;
} UNWINDER_SCOPE_TABLE_ENTRY;

typedef struct _UNWINDER_SCOPE_TABLE {
    DWORD Count;
    UNWINDER_SCOPE_TABLE_ENTRY Entry[1];
} UNWINDER_SCOPE_TABLE;

static BOOL process_scope_table(UNWINDER_CONTEXT* ctx, UNWIND_INFO* unwindInfo, 
                              DWORD64 imageBase) {
    if (!ctx || !unwindInfo) return FALSE;
    
    BYTE* scopePtr = (BYTE*)unwindInfo + 
        sizeof(UNWIND_INFO) - sizeof(UNWIND_CODE) +
        unwindInfo->CountOfCodes * sizeof(UNWIND_CODE);
    
    scopePtr = (BYTE*)(((DWORD64)scopePtr + 3) & ~3);
    
    scopePtr += sizeof(DWORD);
    
    if (IsBadReadPtr(scopePtr, sizeof(DWORD))) {
        debug_print(UW_DEBUG_ERROR, "Invalid scope table count pointer\n");
        return FALSE;
    }
    
    DWORD realCount = *(DWORD*)scopePtr;
    debug_print(UW_DEBUG_INFO, "Processing scope table with %d entries\n", realCount);
    
    if (realCount > 1000) { 
        debug_print(UW_DEBUG_ERROR, "Unreasonable scope table count: %d\n", realCount);
        return FALSE;
    }
    
    scopePtr += sizeof(DWORD);
    
    if (IsBadReadPtr(scopePtr, realCount * sizeof(UNWINDER_SCOPE_TABLE_ENTRY))) {
        debug_print(UW_DEBUG_ERROR, "Cannot read scope table entries\n");
        return FALSE;
    }
    
    UNWINDER_SCOPE_TABLE_ENTRY* entries = (UNWINDER_SCOPE_TABLE_ENTRY*)scopePtr;
    for (DWORD i = 0; i < realCount; i++) {
        debug_print(UW_DEBUG_INFO, "  Scope %d: Begin=0x%x, End=0x%x, Handler=0x%x, Target=0x%x\n",
               i, entries[i].BeginAddress, entries[i].EndAddress,
               entries[i].HandlerAddress, entries[i].JumpTarget);
        
        if (ctx->rip >= imageBase + entries[i].BeginAddress && 
            ctx->rip < imageBase + entries[i].EndAddress) {
            debug_print(UW_DEBUG_INFO, "  Found matching scope for RIP=0x%p\n", (PVOID)ctx->rip);
            return TRUE;
        }
    }
    
    return TRUE; 
}

BOOL process_runtime_function(UNWINDER_CONTEXT* ctx, RUNTIME_FUNCTION* rfn, DWORD64 imageBase) {
    if (!ctx || !rfn) return FALSE;
    
    UNWIND_INFO* unwindInfo = (UNWIND_INFO*)(imageBase + rfn->UnwindData);
    if (!process_unwind_codes(ctx, unwindInfo)) {
        return FALSE;
    }
    
    return TRUE;
}

static void debug_print(UNWINDER_DEBUG_LEVEL level, const char* format, ...) {
    if (level > g_debugConfig.level) 
        return;
        
    va_list args;
    va_start(args, format);
    if (g_debugConfig.includeTimestamp) {
        SYSTEMTIME st;
        GetSystemTime(&st);
        fprintf(g_debugConfig.output, "[%02d:%02d:%02d.%03d] ", 
                st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    }
    vfprintf(g_debugConfig.output, format, args);
    va_end(args);
}

static void set_error(DWORD code, const char* message, DWORD64 address) {
    g_lastError.code = code;
    strncpy_s(g_lastError.message, sizeof(g_lastError.message), message, _TRUNCATE);
    g_lastError.address = address;
    
    debug_print(UW_DEBUG_ERROR, "Error %lu: %s (at 0x%p)\n", 
                code, message, (PVOID)address);
}

UNWINDER_API BOOL get_last_error(DWORD* code, char* message, size_t messageSize, DWORD64* address) {
    if (!code || !message || messageSize == 0) return FALSE;
    
    *code = g_lastError.code;
    strncpy_s(message, messageSize, g_lastError.message, _TRUNCATE);
    if (address) *address = g_lastError.address;
    
    return TRUE;
}
