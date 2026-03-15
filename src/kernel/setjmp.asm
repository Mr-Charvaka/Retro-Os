[bits 32]
section .text

global _setjmp
global setjmp
_setjmp:
setjmp:
    mov eax, [esp + 4]  ; jmp_buf
    mov [eax], ebp
    mov [eax + 4], ebx
    mov [eax + 8], edi
    mov [eax + 12], esi
    lea edx, [esp + 4]  ; original esp
    mov [eax + 16], edx
    mov edx, [esp]      ; return address
    mov [eax + 20], edx
    xor eax, eax
    ret

global _longjmp
global longjmp
_longjmp:
longjmp:
    mov edx, [esp + 4]  ; jmp_buf
    mov eax, [esp + 8]  ; return value
    test eax, eax
    jnz .nonzero
    inc eax
.nonzero:
    mov ebp, [edx]
    mov ebx, [edx + 4]
    mov edi, [edx + 8]
    mov esi, [edx + 12]
    mov esp, [edx + 16]
    jmp [edx + 20]
