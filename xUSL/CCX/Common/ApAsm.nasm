;
; @file ApAsm.nasm
; @brief AP startup code.
; @details
;

;
; Copyright 2021-2023 Advanced Micro Devices, Inc. All rights reserved.
; SPDX-License-Identifier: MIT
;

%include "Porting.h"

SECTION .text

BspMsrLocationOffset                    EQU 0
AllowToLaunchNextThreadLocationOffset   EQU 8
ApStackBasePtrOffset                    EQU 16

AP_STACK_SIZE                           EQU 200h

extern ASM_TAG(RegSettingBeforeLaunchingNextThread)
extern ASM_TAG(ApEntryPointInC)
extern ASM_TAG(mApLaunchGlobalData)
extern ASM_TAG(gBspCr3Value)

global ASM_TAG(ApAsmCode) ; ApAsmCode Address is updated at offset 0x53 in ApStartupCode,
                          ; and ApStartupCode is copied temporarily into reset vector.

;------------------------------------------------------------------------------
; ApAsmCode
;
; @brief    AP startup code
;
; @details  ASM code executed by APs, called at end of ApStartupCode,
;           which syncs MSRs to BSP values, sets up GDT, calls
;           ApEntryPointInC, then allows the next AP to launch
;
;------------------------------------------------------------------------------
bits 32
ASM_TAG(ApAsmCode):
  mov edi, ASM_TAG(mApLaunchGlobalData)
  mov ax, 18h
  mov ds, ax
  mov es, ax
  mov ss, ax
%if IS64BIT == 1
  mov eax, cr4
  bts eax, 5                    ; Set PAE (bit #5)
  mov cr4, eax

  mov ecx, 0x0C0000080          ; Read EFER MSR
  rdmsr
  bts eax, 8                    ; Set LME (bit #8)
  wrmsr

  mov ecx, [ASM_TAG(gBspCr3Value)]        ; Load CR3 with value from BSP
  mov cr3, ecx

  mov eax, cr0
  bts eax, 31                   ; Set PG bit (bit #31)
  mov cr0, eax
  jmp 0x38:FarJump64   ; Set far jump to set code segment to 64bit

bits 64
FarJump64:
  mov ax, 30h
  mov ds, ax
  mov es, ax
  mov ss, ax

  ; Reset RSP
  ; Use only 1 AP stack, later increment of AllowToLaunchNextThreadLocationOffset
  ; needs to be done after finishing stack usage of current AP thread
  xor rax, rax
  mov eax, AP_STACK_SIZE

  mov rsi, [edi + ApStackBasePtrOffset]
  add rax, rsi
  mov rsp, rax

%else
  ; Reset ESP
  ; Use only 1 AP stack, later increment of AllowToLaunchNextThreadLocationOffset
  ; needs to be done after finishing stack usage of current AP thread
  mov eax, AP_STACK_SIZE

  mov esi, [edi + ApStackBasePtrOffset]
  add eax, esi
  mov esp, eax

%endif

  ; Enable Fixed MTRR modification
  mov ecx, 0C0010010h
  rdmsr
  or  eax, 00080000h
  wrmsr

  ; Setup MSRs to BSP values
  mov esi, [edi + BspMsrLocationOffset]
MsrStart:
  mov ecx, [esi]
  cmp ecx, 0FFFFFFFFh
  jz MsrDone
  add esi, 4
  mov eax, [esi]
  add esi, 4
  mov edx, [esi]
  wrmsr
  add esi, 4
  jmp MsrStart

MsrDone:
  ; Disable Fixed MTRR modification and enable MTRRs
  mov ecx, 0C0010010h
  rdmsr
  and eax, 0FFF7FFFFh
  or  eax, 00140000h
  bt  eax, 21           ;SYS_CFG_MTRR_TOM2_EN
  jnc Tom2Disabled
  bts eax, 22           ;SYS_CFG_TOM2_FORCE_MEM_TYPE_WB
Tom2Disabled:
  wrmsr

%if IS64BIT == 1
  ; Enable caching
  mov rax, cr0
  btr eax, 30
  btr eax, 29
  mov cr0, rax
%else
  ; Enable caching
  mov eax, cr0
  btr eax, 30
  btr eax, 29
  mov cr0, eax
%endif

  ; Call into C code before next thread is launched
  call ASM_TAG(RegSettingBeforeLaunchingNextThread)

  ; Call into C code
%if IS64BIT == 1
  mov rcx, rdi
%else
  push edi
%endif
  call ASM_TAG(ApEntryPointInC)

  ; Increment call count to allow to launch next thread, after stack usage is done
  mov esi, [edi + AllowToLaunchNextThreadLocationOffset]
  lock inc WORD [esi]

  ; Hlt
Hlt_loop:
  cli
  hlt
  jmp Hlt_loop
