section .text
    global test_push_nonvol
    extern test_and_unwind

test_push_nonvol:
    push    rbx               
    mov     rbx, rcx         
    lea     rax, [rel test_and_unwind]
    call    rax
    pop     rbx
    ret

section .pdata "r" align=4
    dd  test_push_nonvol wrt ..imagebase
    dd  end_test_push_nonvol wrt ..imagebase
    dd  unwind_info      wrt ..imagebase

section .xdata "r" align=4
unwind_info:
    db  1          
    db  1         
    db  1           
    db  0           
    db  (3<<4) | 4  
    db  0,0         
    dd  0           
    dd  0          
align 4
end_test_push_nonvol:
