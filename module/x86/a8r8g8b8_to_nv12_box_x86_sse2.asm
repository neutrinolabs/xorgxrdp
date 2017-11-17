;
;Copyright 2015 Jay Sorg
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
;ARGB to NV12
;x86 SSE2
;
; notes
;   address s8 should be aligned on 16 bytes, will be slower if not
;   width should be multiple of 8 and > 0
;   height should be even and > 0

%include "common.asm"

PREPARE_RODATA
    cd255  times 4 dd 255

    cw255  times 8 dw 255
    cw16   times 8 dw 16
    cw128  times 8 dw 128
    cw66   times 8 dw 66
    cw129  times 8 dw 129
    cw25   times 8 dw 25
    cw38   times 8 dw 38
    cw74   times 8 dw 74
    cw112  times 8 dw 112
    cw94   times 8 dw 94
    cw18   times 8 dw 18
    cw2    times 8 dw 2

%define LU1            [esp +  0] ; first line U, 8 bytes
%define LV1            [esp +  8] ; first line V, 8 bytes
%define LU2            [esp + 16] ; second line U, 8 bytes
%define LV2            [esp + 24] ; second line V, 8 bytes

%define LS8            [esp + 52] ; s8
%define LSRC_STRIDE    [esp + 56] ; src_stride
%define LD8_Y          [esp + 60] ; d8_y
%define LDST_Y_STRIDE  [esp + 64] ; dst_stride_y
%define LD8_UV         [esp + 68] ; d8_uv
%define LDST_UV_STRIDE [esp + 72] ; dst_stride_uv
%define LWIDTH         [esp + 76] ; width
%define LHEIGHT        [esp + 80] ; height

;int
;a8r8g8b8_to_nv12_box_x86_sse2(const char *s8, int src_stride,
;                              char *d8_y, int dst_stride_y,
;                              char *d8_uv, int dst_stride_uv,
;                              int width, int height);
PROC a8r8g8b8_to_nv12_box_x86_sse2
    push ebx
    RETRIEVE_RODATA
    push esi
    push edi
    push ebp
    sub esp, 32                ; local vars, 32 bytes

    pxor xmm7, xmm7

    mov ebp, LHEIGHT           ; ebp = height
    shr ebp, 1                 ; doing 2 lines at a time

row_loop1:
    mov esi, LS8               ; s8
    mov edi, LD8_Y             ; d8_y
    mov edx, LD8_UV            ; d8_uv

    mov ecx, LWIDTH            ; ecx = width
    shr ecx, 3                 ; doing 8 pixels at a time

loop1:
    ; first line
    movdqu xmm0, [esi]         ; 4 pixels, 16 bytes
    movdqa xmm1, xmm0          ; blue
    pand xmm1, [lsym(cd255)]   ; blue
    movdqa xmm2, xmm0          ; green
    psrld xmm2, 8              ; green
    pand xmm2, [lsym(cd255)]   ; green
    movdqa xmm3, xmm0          ; red
    psrld xmm3, 16             ; red
    pand xmm3, [lsym(cd255)]   ; red

    movdqu xmm0, [esi + 16]    ; 4 pixels, 16 bytes
    movdqa xmm4, xmm0          ; blue
    pand xmm4, [lsym(cd255)]   ; blue
    movdqa xmm5, xmm0          ; green
    psrld xmm5, 8              ; green
    pand xmm5, [lsym(cd255)]   ; green
    movdqa xmm6, xmm0          ; red
    psrld xmm6, 16             ; red
    pand xmm6, [lsym(cd255)]   ; red

    packssdw xmm1, xmm4        ; xmm1 = 8 blues
    packssdw xmm2, xmm5        ; xmm2 = 8 greens
    packssdw xmm3, xmm6        ; xmm3 = 8 reds

    ; _Y = (( 66 * _R + 129 * _G +  25 * _B + 128) >> 8) +  16;
    movdqa xmm4, xmm1          ; blue
    movdqa xmm5, xmm2          ; green
    movdqa xmm6, xmm3          ; red
    pmullw xmm4, [lsym(cw25)]
    pmullw xmm5, [lsym(cw129)]
    pmullw xmm6, [lsym(cw66)]
    paddw xmm4, xmm5
    paddw xmm4, xmm6
    paddw xmm4, [lsym(cw128)]
    psrlw xmm4, 8
    paddw xmm4, [lsym(cw16)]
    packuswb xmm4, xmm7
    movq [edi], xmm4           ; out 8 bytes yyyyyyyy

    ; _U = ((-38 * _R -  74 * _G + 112 * _B + 128) >> 8) + 128;
    movdqa xmm4, xmm1          ; blue
    movdqa xmm5, xmm2          ; green
    movdqa xmm6, xmm3          ; red
    pmullw xmm4, [lsym(cw112)]
    pmullw xmm5, [lsym(cw74)]
    pmullw xmm6, [lsym(cw38)]
    psubw xmm4, xmm5
    psubw xmm4, xmm6
    paddw xmm4, [lsym(cw128)]
    psraw xmm4, 8
    paddw xmm4, [lsym(cw128)]
    packuswb xmm4, xmm7
    movq LU1, xmm4             ; save for later

    ; _V = ((112 * _R -  94 * _G -  18 * _B + 128) >> 8) + 128;
    movdqa xmm6, xmm1          ; blue
    movdqa xmm5, xmm2          ; green
    movdqa xmm4, xmm3          ; red
    pmullw xmm4, [lsym(cw112)]
    pmullw xmm5, [lsym(cw94)]
    pmullw xmm6, [lsym(cw18)]
    psubw xmm4, xmm5
    psubw xmm4, xmm6
    paddw xmm4, [lsym(cw128)]
    psraw xmm4, 8
    paddw xmm4, [lsym(cw128)]
    packuswb xmm4, xmm7
    movq LV1, xmm4             ; save for later

    ; go down to second line
    add esi, LSRC_STRIDE
    add edi, LDST_Y_STRIDE

    ; second line
    movdqu xmm0, [esi]         ; 4 pixels, 16 bytes
    movdqa xmm1, xmm0          ; blue
    pand xmm1, [lsym(cd255)]   ; blue
    movdqa xmm2, xmm0          ; green
    psrld xmm2, 8              ; green
    pand xmm2, [lsym(cd255)]   ; green
    movdqa xmm3, xmm0          ; red
    psrld xmm3, 16             ; red
    pand xmm3, [lsym(cd255)]   ; red

    movdqu xmm0, [esi + 16]    ; 4 pixels, 16 bytes
    movdqa xmm4, xmm0          ; blue
    pand xmm4, [lsym(cd255)]   ; blue
    movdqa xmm5, xmm0          ; green
    psrld xmm5, 8              ; green
    pand xmm5, [lsym(cd255)]   ; green
    movdqa xmm6, xmm0          ; red
    psrld xmm6, 16             ; red
    pand xmm6, [lsym(cd255)]   ; red

    packssdw xmm1, xmm4        ; xmm1 = 8 blues
    packssdw xmm2, xmm5        ; xmm2 = 8 greens
    packssdw xmm3, xmm6        ; xmm3 = 8 reds

    ; _Y = (( 66 * _R + 129 * _G +  25 * _B + 128) >> 8) +  16;
    movdqa xmm4, xmm1          ; blue
    movdqa xmm5, xmm2          ; green
    movdqa xmm6, xmm3          ; red
    pmullw xmm4, [lsym(cw25)]
    pmullw xmm5, [lsym(cw129)]
    pmullw xmm6, [lsym(cw66)]
    paddw xmm4, xmm5
    paddw xmm4, xmm6
    paddw xmm4, [lsym(cw128)]
    psrlw xmm4, 8
    paddw xmm4, [lsym(cw16)]
    packuswb xmm4, xmm7
    movq [edi], xmm4           ; out 8 bytes yyyyyyyy

    ; _U = ((-38 * _R -  74 * _G + 112 * _B + 128) >> 8) + 128;
    movdqa xmm4, xmm1          ; blue
    movdqa xmm5, xmm2          ; green
    movdqa xmm6, xmm3          ; red
    pmullw xmm4, [lsym(cw112)]
    pmullw xmm5, [lsym(cw74)]
    pmullw xmm6, [lsym(cw38)]
    psubw xmm4, xmm5
    psubw xmm4, xmm6
    paddw xmm4, [lsym(cw128)]
    psraw xmm4, 8
    paddw xmm4, [lsym(cw128)]
    packuswb xmm4, xmm7
    movq LU2, xmm4             ; save for later

    ; _V = ((112 * _R -  94 * _G -  18 * _B + 128) >> 8) + 128;
    movdqa xmm6, xmm1          ; blue
    movdqa xmm5, xmm2          ; green
    movdqa xmm4, xmm3          ; red
    pmullw xmm4, [lsym(cw112)]
    pmullw xmm5, [lsym(cw94)]
    pmullw xmm6, [lsym(cw18)]
    psubw xmm4, xmm5
    psubw xmm4, xmm6
    paddw xmm4, [lsym(cw128)]
    psraw xmm4, 8
    paddw xmm4, [lsym(cw128)]
    packuswb xmm4, xmm7
    movq LV2, xmm4             ; save for later

    ; uv add and divide(average)
    movq mm1, LU1              ; u from first line
    movq mm3, mm1
    pand mm1, [lsym(cw255)]
    psrlw mm3, 8
    pand mm3, [lsym(cw255)]
    paddw mm1, mm3             ; add
    movq mm2, LU2              ; u from second line
    movq mm3, mm2
    pand mm2, [lsym(cw255)]
    paddw mm1, mm2             ; add
    psrlw mm3, 8
    pand mm3, [lsym(cw255)]
    paddw mm1, mm3             ; add
    paddw mm1, [lsym(cw2)]     ; add 2
    psrlw mm1, 2               ; div 4

    movq mm2, LV1              ; v from first line
    movq mm4, mm2
    pand mm2, [lsym(cw255)]
    psrlw mm4, 8
    pand mm4, [lsym(cw255)]
    paddw mm2, mm4             ; add
    movq mm3, LV2              ; v from second line
    movq mm4, mm3
    pand mm3, [lsym(cw255)]
    paddw mm2, mm3             ; add
    psrlw mm4, 8
    pand mm4, [lsym(cw255)]
    paddw mm2, mm4             ; add
    paddw mm2, [lsym(cw2)]     ; add 2
    psrlw mm2, 2               ; div 4

    packuswb mm1, mm1
    packuswb mm2, mm2

    punpcklbw mm1, mm2         ; uv
    movq [edx], mm1            ; out 8 bytes uvuvuvuv

    ; go up to first line
    sub esi, LSRC_STRIDE
    sub edi, LDST_Y_STRIDE

    ; move right
    lea esi, [esi + 32]
    lea edi, [edi + 8]
    lea edx, [edx + 8]

    dec ecx
    jnz loop1

    ; update s8
    mov eax, LS8               ; s8
    add eax, LSRC_STRIDE       ; s8 += src_stride
    add eax, LSRC_STRIDE       ; s8 += src_stride
    mov LS8, eax

    ; update d8_y
    mov eax, LD8_Y             ; d8_y
    add eax, LDST_Y_STRIDE     ; d8_y += dst_stride_y
    add eax, LDST_Y_STRIDE     ; d8_y += dst_stride_y
    mov LD8_Y, eax

    ; update d8_uv
    mov eax, LD8_UV            ; d8_uv
    add eax, LDST_UV_STRIDE    ; d8_uv += dst_stride_uv
    mov LD8_UV, eax

    dec ebp
    jnz row_loop1

    mov eax, 0                 ; return value
    add esp, 32                ; local vars, 32 bytes
    pop ebp
    pop edi
    pop esi
    pop ebx
    ret
END_OF_FILE
