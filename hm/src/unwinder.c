#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <winnt.h>  

#define UNWINDER_API __declspec(dllexport)

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
} UNWINDER_CONTEXT;

UNWINDER_API BOOL process_unwind_codes(UNWINDER_CONTEXT* ctx, UNWIND_INFO* unwind_info);
UNWINDER_API BOOL handle_leaf_function(UNWINDER_CONTEXT* ctx);

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

UNWINDER_API BOOL unwind_frame(UNWINDER_CONTEXT* ctx) {
    if (!ctx) return FALSE;

    printf("Looking up unwind entry for RIP=0x%p\n", (PVOID)ctx->rip);
    DWORD64 imageBase;
    RUNTIME_FUNCTION* rfn = RtlLookupFunctionEntry(ctx->rip, &imageBase, NULL);
    printf("  → RtlLookupFunctionEntry returned %p (imageBase=0x%p)\n", 
           (void*)rfn, (void*)imageBase);

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
        PVOID contextPointers = NULL;  

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