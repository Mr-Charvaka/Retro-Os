#ifndef _SETJMP_H
#define _SETJMP_H

typedef int jmp_buf[16]; // Extra space just in case

#ifdef __cplusplus
extern "C" {
#endif

int setjmp(jmp_buf env);
void longjmp(jmp_buf env, int val);

#ifdef __cplusplus
}
#endif

#endif
