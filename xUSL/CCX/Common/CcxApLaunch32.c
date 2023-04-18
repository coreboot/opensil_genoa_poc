/**
 * @file  CcxApLaunch32.c
 * @brief 32 bit AP launch code.
 *
 */

/* Copyright 2021-2023 Advanced Micro Devices, Inc. All rights reserved.    */
// SPDX-License-Identifier: MIT

#include <SilCommon.h>
#include <stdint.h>
#include <string.h>
#include <xSIM.h>
#include <Ccx.h>
#include <MsrReg.h>
#include <CommonLib/CpuLib.h>

// This needs to be global.
uint8_t ApGdt[0x400];
uint8_t ApStack[AP_STACK_SIZE] = {0,};

/**
 * SetupApStartupRegion
 * @brief This routine sets up the necessary code and data to launch APs.
 *
 * @details First, saves data in the memory region that will be altered by copying it all
 *          to MemoryContentCopy. Then, does some fixup on the ApStartupCode byte code by
 *          putting in the correct bytes for JMP instructions and the correct pointers for
 *          ApLaunchGlobalData and ApEntryInCOffset. Then, copies everything to memory as
 *          detailed in Ccx.h. Finally, restores the memory region.
 *
 * @param ApLaunchGlobalData  AP launch global data
 * @param ApStartupVector     Pointer AP startup code
 * @param MemoryContentCopy   This is temporary buffer to store data at reset
 *                            vector to allow AP launch
 * @param CcxDataBlock        Pointer to CCX input data block
 *
 */

uint32_t gApLaunchGlobalData;

void
SetupApStartupRegion (
  volatile AMD_CCX_AP_LAUNCH_GLOBAL_DATA *ApLaunchGlobalData,
  uint64_t                               *ApStartupVector,
  uint8_t                                *MemoryContentCopy,
  CCXCLASS_DATA_BLK                      *CcxDataBlock
  )
{
  uint8_t             i;
  CCX_GDT_DESCRIPTOR  BspGdtr;
  uint32_t            EntrySize;
  uint64_t            EntryDest;
  uint64_t GdtEntries[] =
  {
    0x0000000000000000,  // [00h] Null descriptor
    0x00CF92000000FFFF,  // [08h] Linear data segment descriptor
    0x00CF9A000000FFFF,  // [10h] Linear code segment descriptor
    0x00CF92000000FFFF,  // [18h] System data segment descriptor
    0x00CF9A000000FFFF,  // [20h] System code segment descriptor
    0x0000000000000000,  // [28h] Spare segment descriptor
    0x00CF93000000FFFF,  // [30h] System data segment descriptor
    0x00AF9B000000FFFF,  // [38h] System code segment descriptor
    0x0000000000000000   // [40h] Spare segment descriptor
  };

  if ((ApLaunchGlobalData == NULL) ||
    (ApStartupVector == NULL) ||
    (MemoryContentCopy == NULL) ||
    (CcxDataBlock == NULL)) {
    CCX_TRACEPOINT (SIL_TRACE_ERROR, "NULL argument passed to SetupApStartupRegion.\n");
    assert (false);
  }
  EntryDest = CONFIG_PSP_BIOS_BIN_BASE;                 //0x76CD0000, 0x75CD0000
  EntrySize = CONFIG_PSP_BIOS_BIN_SIZE;

  /* ApStartupVector is where the AP will begin executing instructions, at the very
     end of the startup region with the last 16 bits being FFF0 (hence - 0x10).*/
  *ApStartupVector = (uint64_t) ((uint32_t) EntryDest + EntrySize - 0x10);

  CCX_TRACEPOINT (SIL_TRACE_INFO, "ApStartupVector = 0x%x\n", *ApStartupVector);

  // Use host allocated space for APs to use as stack space
  ApLaunchGlobalData->ApStackBasePtr = (uintptr_t) ApStack;

  // MemoryContentCopy is used to store data at reset vector
  memset (
    MemoryContentCopy,
    0,
    AP_TEMP_BUFFER_SIZE
    );

  // Copy data at reset vector to temporary buffer so we
  // can temporarily replace it with AP start up code.
  memcpy (
    MemoryContentCopy,
    (void*) ((uintptr_t)*ApStartupVector - AP_STARTUP_CODE_OFFSET),
    AP_TEMP_BUFFER_SIZE
    );
  memset (
    (void *) ((uintptr_t)*ApStartupVector - AP_STARTUP_CODE_OFFSET),
    0,
    AP_TEMP_BUFFER_SIZE
    );

  ApLaunchGlobalData->AllowToLaunchNextThreadLocation = (uint32_t) (*ApStartupVector + 0xE);

  // Copy the near jump to AP startup code to reset vector. The near jump
  // forces execution to start from CS:FFF0 - AP_STARTUP_CODE_OFFSET
  extern const uint8_t Jump16Bit[];
  extern const uint8_t ResetVector[];
  extern const uint8_t eResetVector[];
  const size_t ResetVectorSize = eResetVector - ResetVector;
  const size_t ApResetCodeSize = eResetVector - Jump16Bit;
  memcpy ((void*) ((uintptr_t)*ApStartupVector + ResetVectorSize - ApResetCodeSize), Jump16Bit, ApResetCodeSize);

  // Fill in global data consumed by AP reset vector code
  gApLaunchGlobalData = (uint32_t)ApLaunchGlobalData;


  // Copy GDT Entries to Segment + 0xFFF0 - BSP_GDT_OFFSET
  memcpy (
    (void*) ((uintptr_t)*ApStartupVector - BSP_GDT_OFFSET),
    &GdtEntries,
    sizeof (GdtEntries)
    );

  // Load Fixed-MTRRs list with values from BSP.
  xUslMsrOr (MSR_SYS_CFG, BIT_64(19));

  for (i = 0; ApLaunchGlobalData->ApMtrrSyncList[i].MsrAddr != CPU_LIST_TERMINAL; i++) {
    ApLaunchGlobalData->ApMtrrSyncList[i].MsrData =
        xUslRdMsr (ApLaunchGlobalData->ApMtrrSyncList[i].MsrAddr);
  }

  // Some Fixed-MTRRs should be set according to input arguments
  UpdateApMtrrSettings (ApLaunchGlobalData->ApMtrrSyncList, &(CcxDataBlock->CcxInputBlock));

  xUslMsrAnd (MSR_SYS_CFG, ~((uint64_t) SYS_CFG_MTRR_FIX_DRAM_MOD_EN));

  // Copy MTRRs setting to Segment + 0xFFF0 - BSP_MSR_OFFSET
  ApLaunchGlobalData->BspMsrLocation = (uint32_t) ((*ApStartupVector) - (BSP_MSR_OFFSET));
  assert (ApLaunchGlobalData->SizeOfApMtrr <= BSP_MSR_SIZE);

  // Need to cast ApLaunchGlobalData to avoid volatile quantifier warning (C4090)
  memcpy (
      (void*) ((uintptr_t)*ApStartupVector - BSP_MSR_OFFSET),
      (void*) ApLaunchGlobalData->ApMtrrSyncList,
      ApLaunchGlobalData->SizeOfApMtrr
      );

  CCX_TRACEPOINT (
      SIL_TRACE_INFO,
      "ApLaunchGlobalData->AllowToLaunchNextThreadLocation = 0x%x\n",
      ApLaunchGlobalData->AllowToLaunchNextThreadLocation
      );

  CCX_TRACEPOINT (
      SIL_TRACE_INFO,
      "ApLaunchGlobalData->BspMsrLocation = 0x%x\n",
      ApLaunchGlobalData->BspMsrLocation
      );

  CCX_TRACEPOINT (
      SIL_TRACE_INFO,
      "ApLaunchGlobalData->ApMtrrSyncList = 0x%x\n",
      (uint32_t)*ApStartupVector - BSP_MSR_OFFSET
      );

  BspGdtr.Limit = (uint16_t) sizeof (GdtEntries) - 1;
  BspGdtr.Base = (uint64_t) *ApStartupVector - BSP_GDT_OFFSET;

  // Copy pointer to GDT entries to Segment + 0xFFF4
  memcpy (
    (void*) ((uint32_t)*ApStartupVector + 4),
    &BspGdtr,
    sizeof (BspGdtr)
    );

  // Save BSP's patch level so that AP can use it to determine whether microcode patch
  // loading should be skipped
  ApLaunchGlobalData->BspPatchLevel = xUslRdMsr (MSR_PATCH_LEVEL);

  memcpy (ApGdt, &GdtEntries[0], sizeof (GdtEntries));

  ApLaunchGlobalData->ApGdtDescriptor.Limit = (sizeof (GdtEntries)) - 1;
  ApLaunchGlobalData->ApGdtDescriptor.Base = (uint32_t) (size_t) ApGdt;

  // Force content into memory, out of cache, so that AP can have access.
  xUslWbinvd ();
}
