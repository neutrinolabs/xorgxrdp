;
;Copyright 2014 Jay Sorg
;Copyright 2017 mirabilos
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
;ARGB to ABGR
;x86 SSE2 32 bit
;

%include "common.asm"

PREPARE_RODATA
c1 times 4 dd 0xFF00FF00
c2 times 4 dd 0x00FF0000
c3 times 4 dd 0x000000FF

; s8 and d8 may have independent alignments, including between rows
;int
;a8r8g8b8_to_a8b8g8r8_box_x86_sse2(const char *s8, int src_stride,
;                                  char *d8, int dst_stride,
;                                  int width, int height);
PROC a8r8g8b8_to_a8b8g8r8_box_x86_sse2
    cmp dword [esp + 20], 0 ; width
    jle return_zero
    cmp dword [esp + 24], 0 ; height
    jle return_zero

    push ebx
    RETRIEVE_RODATA
    push esi
    push edi
    push ebp

    movdqa xmm4, [lsym(c1)]
    movdqa xmm5, [lsym(c2)]
    movdqa xmm6, [lsym(c3)]

    mov esi, [esp + 20]  ; src
    mov edi, [esp + 28]  ; dst

loop_y:
    mov ecx, [esp + 36]  ; width

    prefetchnta [esi]

; A R G B A R G B A R G B A R G B to
; A B G R A B G R A B G R A B G R

loop_x8:
    cmp ecx, 8
    jl done_loop_x8

    prefetchnta [esi + 32]

    movdqu xmm0, [esi]
    lea esi, [esi + 16]
    movdqa xmm3, xmm0    ; a and g
    pand xmm3, xmm4
    movdqa xmm1, xmm0    ; r
    pand xmm1, xmm5
    psrld xmm1, 16
    por xmm3, xmm1
    movdqa xmm1, xmm0    ; b
    pand xmm1, xmm6
    pslld xmm1, 16
    por xmm3, xmm1
    movdqu [edi], xmm3
    lea edi, [edi + 16]
    sub ecx, 4

    movdqu xmm0, [esi]
    lea esi, [esi + 16]
    movdqa xmm3, xmm0    ; a and g
    pand xmm3, xmm4
    movdqa xmm1, xmm0    ; r
    pand xmm1, xmm5
    psrld xmm1, 16
    por xmm3, xmm1
    movdqa xmm1, xmm0    ; b
    pand xmm1, xmm6
    pslld xmm1, 16
    por xmm3, xmm1
    movdqu [edi], xmm3
    lea edi, [edi + 16]
    sub ecx, 4

    jmp loop_x8
done_loop_x8:

loop_x:
    cmp ecx, 1
    jl done_loop_x
    mov eax, [esi]
    lea esi, [esi + 4]
    mov edx, eax         ; a and g
    and edx, 0xFF00FF00
    mov ebp, eax         ; r
    and ebp, 0x00FF0000
    shr ebp, 16
    or edx, ebp
    mov ebp, eax         ; b
    and ebp, 0x000000FF
    shl ebp, 16
    or edx, ebp
    mov [edi], edx
    lea edi, [edi + 4]
    dec ecx
    jmp loop_x
done_loop_x:

    mov esi, [esp + 20]
    add esi, [esp + 24]
    mov [esp + 20], esi

    mov edi, [esp + 28]
    add edi, [esp + 32]
    mov [esp + 28], edi

    mov ecx, [esp + 40] ; height
    dec ecx
    mov [esp + 40], ecx
    jnz loop_y

    pop ebp
    pop edi
    pop esi
    pop ebx
return_zero:
    mov eax, 0          ; return value
    ret
END_OF_FILE
