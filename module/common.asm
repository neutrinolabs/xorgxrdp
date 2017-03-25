;
;Copyright 2017 Pavel Roskin
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
;Common nasm code
;

; Detect ELF formats
%ifidn __OUTPUT_FORMAT__,elf
%define is_elf 1
%endif

%ifidn __OUTPUT_FORMAT__,elf32
%define is_elf 1
%endif

%ifidn __OUTPUT_FORMAT__,elf64
%define is_elf 1
%endif

; Mark stack non-executable
%ifdef is_elf
section .note.GNU-stack noalloc noexec nowrite progbits
%endif

; Global function header
%macro PROC 1
    align 16
%ifdef is_elf
    global %1:function
    %1:
%else
    global _%1
    _%1:
%endif
%endmacro

; Macros for relative access to local data
%undef use_elf_pic
%undef lsym

%ifdef ASM_ARCH_AMD64
; amd64; don't define or call RETRIEVE_RODATA
%define lsym(name) rel name
%endif

%ifdef ASM_ARCH_I386
%ifdef is_elf
%ifdef PIC
; i386 ELF PIC
%define use_elf_pic 1
%macro RETRIEVE_RODATA 0
	call ..@get_GOT
%%getgot:
	add ebx, _GLOBAL_OFFSET_TABLE_ + $$ - %%getgot wrt ..gotpc
%endmacro
%define lsym(name) ebx + name wrt ..gotoff
%else
; i386 ELF, not PIC, default case (see below)
%endif
%else
; i386 not ELF
%ifdef PIC
%error "Position-Independent Code is currently only supported for ELF"
%endif
; i386 not ELF, not PIC, default case (see below)
%endif
%endif

%ifndef lsym
%macro RETRIEVE_RODATA 0
%endmacro
%define lsym(name) name
%endif

%ifnmacro PREPARE_RODATA
%macro PREPARE_RODATA 0
section .data
align 16
%endmacro
%endif

section .text

; Prerequisite code for relative access to local data
%ifdef use_elf_pic
extern _GLOBAL_OFFSET_TABLE_
..@get_GOT:
	mov ebx, [esp]
	ret
%endif
