section .text
    global test_and_unwind

%define UNW_FLAG_EHANDLER 1

test_and_unwind:
    extern MyExceptionDispatcher
    int 3
    ret

section .pdata rdata align=4
    dd test_and_unwind wrt ..imagebase
    dd end_test_and_unwind wrt ..imagebase
    dd unwind_info_for_tu wrt ..imagebase

section .xdata rdata align=4
unwind_info_for_tu:
    db (1 | (UNW_FLAG_EHANDLER << 3)) 
    db 0                              
    db 0                              
    db 0                               
    align 4                            
    dd MyExceptionDispatcher wrt ..imagebase  
    dd 0                               
end_test_and_unwind:
