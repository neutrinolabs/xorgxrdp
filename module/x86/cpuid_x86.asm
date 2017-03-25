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
;x86 SSE2
;

%include "common.asm"

;int
;cpuid_x86(int eax_in, int ecx_in, int *eax, int *ebx, int *ecx, int *edx)

PROC cpuid_x86
    ; save registers
    push ebx
    push ecx
    push edx
    push edi
    ; cpuid
    mov eax, [esp + 20]
    mov ecx, [esp + 24]
    cpuid
    mov edi, [esp + 28]
    mov [edi], eax
    mov edi, [esp + 32]
    mov [edi], ebx
    mov edi, [esp + 36]
    mov [edi], ecx
    mov edi, [esp + 40]
    mov [edi], edx
    mov eax, 0
    ; restore registers
    pop edi
    pop edx
    pop ecx
    pop ebx
    ret
END_OF_FILE
