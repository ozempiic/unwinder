section .text
global get_current_frame
global restore_frame_context

get_current_frame:
    push rbp
    mov rbp, rsp
    
    mov [rcx], rbp    
    lea rax, [rbp+8]  
    mov [rcx+8], rax  
    mov rax, [rbp+8] 
    mov [rcx+16], rax 
    
    pop rbp
    ret

restore_frame_context:
    mov rsp, rcx  
    mov rbp, rdx 
    jmp rax       