/*
 * user/setjmp.S — setjmp / longjmp for i386 freestanding programs.
 *
 * jmp_buf layout (10 × 4 bytes):
 *   [0]  ebx
 *   [1]  esi
 *   [2]  edi
 *   [3]  ebp
 *   [4]  esp  (caller's esp after the call, i.e. esp+4 at call site)
 *   [5]  eip  (return address of setjmp's caller)
 *   [6-9] reserved / future signal mask
 */

.global setjmp
.global longjmp
.global _setjmp
.global _longjmp

.type setjmp,  @function
.type longjmp, @function

setjmp:
_setjmp:
    movl 4(%esp), %eax          /* eax = jmp_buf pointer */
    movl %ebx,  0(%eax)
    movl %esi,  4(%eax)
    movl %edi,  8(%eax)
    movl %ebp, 12(%eax)
    /* Save caller's ESP (points to return address) */
    leal 4(%esp), %ecx
    movl %ecx, 16(%eax)
    /* Save return address */
    movl (%esp), %ecx
    movl %ecx, 20(%eax)
    xorl %eax, %eax             /* return 0 from setjmp */
    ret
.size setjmp, . - setjmp

longjmp:
_longjmp:
    movl 4(%esp), %edx          /* edx = jmp_buf pointer */
    movl 8(%esp), %eax          /* eax = val (return value) */
    testl %eax, %eax
    jnz   1f
    movl $1, %eax               /* longjmp(buf,0) returns 1 */
1:
    movl  0(%edx), %ebx
    movl  4(%edx), %esi
    movl  8(%edx), %edi
    movl 12(%edx), %ebp
    movl 16(%edx), %esp         /* restore caller's stack pointer */
    jmp  *20(%edx)              /* jump to saved return address */
.size longjmp, . - longjmp
