;
; @file NearJump.nasm
; @brief 16-bit Reset vector.
; @details
;

;
;Copyright 2021-2023 Advanced Micro Devices, Inc. All rights reserved.
;

%include "Porting.h"

global ASM_TAG(Jump16Bit)
global ASM_TAG(ResetVector)
global ASM_TAG(eResetVector)
extern ASM_TAG(gApLaunchGlobalData)
extern ASM_TAG(ApAsmCode)

SECTION .text

bits 32

Jump32Bit:
 mov	edi, [gApLaunchGlobalData]
 jmp	ApAsmCode

align 16

bits 16
ASM_TAG(Jump16Bit):
 mov	si, 0xfff4
 o32 lgdt cs:[si]

 mov	eax, cr0	; Get control register 0
 or	eax, 0x3 	; Set PE bit (bit #0)
 mov	cr0, eax

 mov	eax, cr4
 or	eax, 0x600
 mov	cr4, eax

 mov	ax, 0x18
 mov	ds, eax
 mov	es, ax
 mov	fs, ax
 mov	gs, ax
 mov	ss, ax

 o32 a32 jmp dword 0x10:ASM_TAG(Jump32Bit)

align 16

ASM_TAG(ResetVector):
 nop
 jmp word	ASM_TAG(Jump16Bit)
ASM_TAG(eResetVector):
