#ifndef USER_SETJMP_H
#define USER_SETJMP_H

typedef int jmp_buf[10];

int  setjmp(jmp_buf env);
void longjmp(jmp_buf env, int val) __attribute__((noreturn));

/* POSIX aliases */
#define _setjmp  setjmp
#define _longjmp longjmp

#endif /* USER_SETJMP_H */
