;
;Copyright 2016 Jay Sorg
;
;Permission to use, copy, modify, distribute, and sell this software and its
;documentation for any purpose is hereby granted without fee, provided that
;the above copyright notice appear in all copies and that both that
;copyright notice and this permission notice appear in supporting
;documentation.
;
;The above copyright notice and this permission notice shall be included in
;all copies or substantial portions of the Software.
;
;THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
;IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
;FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
;OPEN GROUP BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
;AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
;CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
;
;cpuid
;amd64 SSE2
;

%include "common.asm"

;The first six integer or pointer arguments are passed in registers
;RDI, RSI, RDX, RCX, R8, and R9

;int
;cpuid_amd64(int eax_in, int ecx_in, int *eax, int *ebx, int *ecx, int *edx)

PROC cpuid_amd64
    ; save registers
    push rbx

    push rdx
    push rcx
    push r8
    push r9

    mov rax, rdi
    mov rcx, rsi
    cpuid
    pop rdi
    mov [rdi], edx
    pop rdi
    mov [rdi], ecx
    pop rdi
    mov [rdi], ebx
    pop rdi
    mov [rdi], eax
    mov eax, 0
    ; restore registers
    pop rbx
    ret
END_OF_FILE
