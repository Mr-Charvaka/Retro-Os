/* crt0.S — clean entry, NO PIC, NO GOT */
.section .text._start, "ax", @progbits
.global _start
.type _start, @function

_start:
    xor  %ebp, %ebp
    /* DON'T realign — kernel already set up argc/argv on stack */
    
    /* Zero BSS */
    movl $__bss_start, %edi
    movl $__bss_end, %ecx
    subl %edi, %ecx
    shrl $2, %ecx
    xorl %eax, %eax
    rep  stosl

    /* argc and argv are already on the stack from kernel */
    call  main

    /* sys_exit */
    movl %eax, %ebx
    movl $12, %eax
    int  $0x80
1:  hlt
    jmp  1b

.size _start, . - _start
