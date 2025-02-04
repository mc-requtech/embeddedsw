/******************************************************************************
* Copyright (c) 2018 - 2022 Xilinx, Inc.  All rights reserved.
* SPDX-License-Identifier: MIT
******************************************************************************/

/*****************************************************************************/
/**
*
* @file xplmi_cdo.c
*
* This file contains code to handling the CDO Buffer.
*
* <pre>
* MODIFICATION HISTORY:
*
* Ver   Who  Date        Changes
* ----- ---- -------- -------------------------------------------------------
* 1.00  kc   08/23/2018 Initial release
* 1.01  kc   12/02/2019 Added performance timestamps
*       kc   12/17/2019 Added deferred error mechanism for mask poll
*       bsv  01/12/2020 Changes related to bitstream loading
*       ma   02/18/2020 Made performance measurement functions generic
*       bsv  04/03/2020 Code clean up Xilpdi
* 1.02  kc   06/12/2020 Added IPI mask to PDI CDO commands to get
* 						subsystem information
*       kc   06/23/2020 Added code print command details for errors
*       bsv  07/07/2020 Made functions used in single transaltion unit as
*						static
*       bsv  09/30/2020 Added parallel DMA support for SBI, JTAG, SMAP and PCIE
*                       boot modes
*       bm   10/14/2020 Code clean up
*       td	 10/19/2020 MISRA C Fixes
* 1.03  td   11/23/2020 MISRA C Rule 17.8 Fixes
*       bm   02/17/2021 Add overflow check for payloadlen
*       ma   03/24/2021 Reduced minimum digits of time stamp decimals to 3
*       bsv  04/16/2021 Add provision to store Subsystem Id in XilPlmi
* 1.04  bsv  07/05/2021 Added code to handle case where bitstream data starts
*                       at 32K boundary
*       bm   07/12/2021 Updated CDO offset print with byte offset including
*                       header length
*       bsv  07/16/2021 Fix doxygen warnings
*       bsv  08/02/2021 Code clean up to reduce size
*       bsv  08/15/2021 Removed unwanted goto statements
* 1.05  skd  11/18/2021 Removed unwanted time stamps in XPlmi_ProcessCdo
*       kpt  12/13/2021 Replaced Xil_SecureMemCpy with Xil_SMemCpy
*       ma   01/31/2022 Fix DMA Keyhole command issue where the command
*                       starts at the 32K boundary
* 1.06  bm   07/06/2022 Refactor versal and versal_net code
*       bm   07/24/2022 Set PlmLiveStatus during boot time
*       ma   07/25/2022 Enhancements to secure lockdown code
*       bm   08/24/2022 Support Begin, Break and End commands across chunk
*                       boundaries
*
* </pre>
*
* @note
*
******************************************************************************/

/***************************** Include Files *********************************/
#include "xplmi_dma.h"
#include "xplmi_cdo.h"
#include "xplmi_proc.h"
#include "xplmi_hw.h"
#include "xil_util.h"
#include "xplmi_generic.h"
#include "xplmi_wdt.h"
#include "xplmi_tamper.h"

/************************** Constant Definitions *****************************/
#define XPLMI_CMD_LEN_TEMPBUF		(0x8U) /**< This buffer is used to
			store commands which extend across 32K boundaries */

/**************************** Type Definitions *******************************/

/***************** Macros (Inline Functions) Definitions *********************/

/************************** Function Prototypes ******************************/

/************************** Variable Definitions *****************************/

/*****************************************************************************/

/*****************************************************************************/
/**
 * @brief	This function will calculate the size of the command. Bits 16 to 23
 * denote the size of the command. If the value is 255, then the word following
 * CmdId would denote the size of the command.
 *
 * @param	Buf is pointer to buffer
 * @param	Len is length of the command that is available in memory to parse
 *
 * @return	Size of the command
 *
 *****************************************************************************/
static u32 XPlmi_CmdSize(const u32 *Buf, u32 Len)
{
	u32 Size = 1U;
	if (Len >= Size) {
		u32 CmdId = Buf[0U];
		u32 PayloadLen = (CmdId & XPLMI_CMD_LEN_MASK) >> 16U;
		if (PayloadLen == XPLMI_MAX_SHORT_CMD_LEN) {
			Size = XPLMI_LONG_CMD_HDR_LEN;
			if (Len >= Size) {
				PayloadLen = Buf[1U];
			}
			if (PayloadLen > XPLMI_MAX_LONG_CMD_LEN) {
				PayloadLen = XPLMI_MAX_LONG_CMD_LEN;
			}
		}
		Size += PayloadLen;
	}
	return Size;
}

/*****************************************************************************/
/**
 * @brief	This function will setup the command structure.
 *
 * @param	Cmd is pointer to the command structure
 * @param	Buf is pointer to the command buffer
 * @param	BufLen is length of the buffer. It may not have total Command
 * length if buffer is not available.
 *
 * @return None
 *
 *****************************************************************************/
static void XPlmi_SetupCmd(XPlmi_Cmd * Cmd, u32 *Buf, u32 BufLen)
{
	u32 HdrLen = 1U;

	Cmd->CmdId = Buf[0U];
	Cmd->Len = (Cmd->CmdId >> XPLMI_SHORT_CMD_LEN_SHIFT) & XPLMI_MAX_SHORT_CMD_LEN;
	Cmd->Payload = &Buf[1U];
	Cmd->ProcessedLen = 0U;
	if (Cmd->Len == XPLMI_MAX_SHORT_CMD_LEN) {
		HdrLen = XPLMI_LONG_CMD_HDR_LEN;
		Cmd->Len = Buf[1U];
		Cmd->Payload = &Buf[XPLMI_LONG_CMD_HDR_LEN];
	}

	/* Assign the available payloadlen in the buffer */
	if (Cmd->Len > (BufLen - HdrLen)) {
		Cmd->PayloadLen = BufLen - HdrLen;
	} else {
		Cmd->PayloadLen = Cmd->Len;
	}
}

/*****************************************************************************/
/**
 * @brief	This function verifies CDO header.
 *
 * @param	CdoPtr is pointer to the CDO structure
 *
 * @return	XST_SUCCESS on success and error code on failure
 *
 *****************************************************************************/
static int XPlmi_CdoVerifyHeader(const XPlmiCdo *CdoPtr)
{
	int Status = XST_FAILURE;
	u32 CheckSum = 0U;
	const u32 *CdoHdr = CdoPtr->BufPtr;
	u32 Index;

	if (CdoHdr[1U] != XPLMI_CDO_HDR_IDN_WRD) {
		XPlmi_Printf(DEBUG_GENERAL,
				"CDO Header Identification Failed\n\r");
		Status = XPlmi_UpdateStatus(XPLMI_ERR_CDO_HDR_ID, 0);
		goto END;
	}
	for (Index = 0U; Index < (XPLMI_CDO_HDR_LEN - 1U); Index++) {
		CheckSum += CdoHdr[Index];
	}

	/* Invert checksum */
	CheckSum ^= 0xFFFFFFFFU;
	if (CheckSum != CdoHdr[Index]) {
		XPlmi_Printf(DEBUG_GENERAL,
				"Config Object Checksum Failed\n\r");
		Status = XPlmi_UpdateStatus(XPLMI_ERR_CDO_CHECKSUM, 0);
		goto END;
	} else {
		Status = XST_SUCCESS;
	}

	XPlmi_Printf(DEBUG_INFO,
		"Config Object Version 0x%08x\n\r", CdoHdr[2U]);
	XPlmi_Printf(DEBUG_INFO,
		"Length 0x%08x\n\r", CdoHdr[3U]);

END:
	return Status;
}

/*****************************************************************************/
/**
 * @brief	This function initializes the CDO pointer structure.
 *
 * @param	CdoPtr is pointer to the CDO structure
 *
 * @return	XST_SUCCESS on success and error code on failure
 *
 *****************************************************************************/
int XPlmi_InitCdo(XPlmiCdo *CdoPtr)
{
	int Status = XST_FAILURE;

	/* Initialize the CDO structure variables */
	Status = XPlmi_MemSetBytes(CdoPtr, sizeof(XPlmiCdo), 0U,
		sizeof(XPlmiCdo));
	if (Status != XST_SUCCESS) {
		goto END;
	}
	/* Initialize the CDO buffer user params */
	CdoPtr->Cdo1stChunk = (u8)TRUE;

END:
	return Status;
}

/*****************************************************************************/
/**
 * @brief	This function will update the command pointer and resume the
 * command from previous state.
 *
 * @param	CdoPtr Pointer to the CDO structure
 * @param	BufPtr Pointer to the buffer
 * @param	BufLen Len of the buffer
 * @param	Size Pointer to the Size consumed by the command execution
 *
 * @return	XST_SUCCESS in case of success
 *
 *****************************************************************************/
static int XPlmi_CdoCmdResume(XPlmiCdo *CdoPtr, u32 *BufPtr, u32 BufLen, u32 *Size)
{
	int Status = XST_FAILURE;
	XPlmi_Cmd *CmdPtr = &CdoPtr->Cmd;
	u32 PrintLen;

	/* Update the Payload buffer and length */
	if (CmdPtr->Len > (CmdPtr->ProcessedLen + BufLen)) {
		CmdPtr->PayloadLen = BufLen;
	} else {
		CmdPtr->PayloadLen = CmdPtr->Len - CmdPtr->ProcessedLen;
		CdoPtr->CmdState = XPLMI_CMD_STATE_START;
	}

	/* Copy the image id to cmd subsystem ID */
	CmdPtr->SubsystemId = CdoPtr->SubsystemId;
	CmdPtr->IpiMask = 0U;
	CmdPtr->Payload = BufPtr;
	CmdPtr->ProcessedCdoLen = CdoPtr->ProcessedCdoLen;
	*Size = CmdPtr->PayloadLen;
	Status = XPlmi_CmdResume(CmdPtr);
	if (Status != XST_SUCCESS) {
		XPlmi_Printf(DEBUG_GENERAL,
			"CMD: 0x%08x Resume failed, Processed Cdo Length 0x%0x\n\r",
			CmdPtr->CmdId, CdoPtr->ProcessedCdoLen * XPLMI_WORD_LEN);
		PrintLen = CmdPtr->PayloadLen;
		if (PrintLen > XPLMI_CMD_LEN_TEMPBUF) {
			PrintLen = XPLMI_CMD_LEN_TEMPBUF;
		}
		XPlmi_PrintArray(DEBUG_GENERAL, (u64)(UINTPTR)CmdPtr->Payload, PrintLen,
				 "CMD payload");
	}

	CdoPtr->ProcessedCdoLen += *Size;

	return Status;
}

/*****************************************************************************/
/**
 * @brief	This function copies gets the prepares the CMD pointer and
 * executes it.
 *
 * @param	CdoPtr is pointer to the CDO structure
 * @param	BufPtr is pointer to the buffer
 * @param	BufLen is length of the buffer
 * @param	Size is pointer to the Size consumed by the command execution
 *
 * @return	XST_SUCCESS on success and error code on failure
 *
 *****************************************************************************/
static int XPlmi_CdoCmdExecute(XPlmiCdo *CdoPtr, u32 *BufPtr, u32 BufLen, u32 *Size)
{
	int Status = XST_FAILURE;
	XPlmi_Cmd *CmdPtr = &CdoPtr->Cmd;
	u32 PrintLen;
	u32 BufSize;

	/*
	 * Break if CMD says END of commands,
	 * irrespective of the CDO length
	 */
	if (BufPtr[0U] == XPLMI_CMD_END) {
		XPlmi_Printf(DEBUG_INFO, "CMD END detected \n\r");
		CdoPtr->CmdEndDetected = (u8)TRUE;
		Status = XST_SUCCESS;
		goto END;
	}

	*Size = XPlmi_CmdSize(BufPtr, BufLen);
	CmdPtr->Len = *Size;
	/*
	 * Check if Cmd payload is less than buffer size, then copy to
	 * the starting of the next chunk address.
	 */
	if ((*Size > BufLen) && (BufLen < XPLMI_CMD_LEN_TEMPBUF)) {
		BufSize = BufLen * XPLMI_WORD_LEN;
		CdoPtr->TempCmdBuf = (u32 *)(CdoPtr->NextChunkAddr - BufSize);

		/* Copy Cmd to temporary buffer */
		Status = Xil_SMemCpy(CdoPtr->TempCmdBuf, BufSize,
				BufPtr, BufSize,
				BufSize);
		if (Status != XST_SUCCESS) {
			Status = XPlmi_UpdateStatus(XPLMI_ERR_MEMCPY_CMD_EXEC, Status);
			goto END;
		}
		CdoPtr->CopiedCmdLen = BufLen;
		*Size = BufLen;
		Status = XST_SUCCESS;
		goto END;
	}

	/*
	 * If size is greater than tempbuf, execute partially
	 * and resume the cmd in next iteration
	 */
	if (*Size > BufLen) {
		*Size = BufLen;
		CdoPtr->CmdState = XPLMI_CMD_STATE_RESUME;
	}
	/* Copy the image id to cmd subsystem ID */
	CmdPtr->SubsystemId = CdoPtr->SubsystemId;
	CmdPtr->IpiMask = 0U;
	CmdPtr->BreakLength = 0U;

	/* Execute the command */
	XPlmi_SetupCmd(CmdPtr, BufPtr, *Size);
	CmdPtr->DeferredError = (u8)FALSE;
	CmdPtr->ProcessedCdoLen = CdoPtr->ProcessedCdoLen;
	Status = XPlmi_CmdExecute(CmdPtr);
	if (Status != XST_SUCCESS) {
		XPlmi_Printf(DEBUG_GENERAL,
			"CMD: 0x%08x execute failed, Processed Cdo Length 0x%0x\n\r",
			CmdPtr->CmdId, (CdoPtr->ProcessedCdoLen + XPLMI_CDO_HDR_LEN) * XPLMI_WORD_LEN);
		PrintLen = CmdPtr->PayloadLen;
		if (PrintLen > XPLMI_CMD_LEN_TEMPBUF) {
			PrintLen = XPLMI_CMD_LEN_TEMPBUF;
		}
		XPlmi_PrintArray(DEBUG_GENERAL, (u64)(UINTPTR)CmdPtr->Payload, PrintLen,
				 "CMD Payload");
		goto END;
	}

	CdoPtr->ProcessedCdoLen += *Size;
	if(CmdPtr->Len == (CmdPtr->PayloadLen - 1U)) {
		CdoPtr->ProcessedCdoLen +=  CdoPtr->Cmd.KeyHoleParams.ExtraWords;
		CmdPtr->PayloadLen = CmdPtr->Len;
		CdoPtr->CmdState = XPLMI_CMD_STATE_START;
		CdoPtr->CopiedCmdLen = 0U;
	}

END:
	return Status;
}

/*****************************************************************************/
/**
 * @brief	This function process the CDO file.
 *
 * @param	CdoPtr is pointer to the CDO structure
 *
 * @return	XST_SUCCESS on success and error code on failure
 *
 *****************************************************************************/
int XPlmi_ProcessCdo(XPlmiCdo *CdoPtr)
{
	int Status = XST_FAILURE;
	u32 Size = 0U;
	u32 *BufPtr = CdoPtr->BufPtr;
	u32 BufLen = CdoPtr->BufLen;
	u32 RemainingLen;
	u32 SldInitiated = XPlmi_IsSldInitiated();

	/* Verify the header for the first chunk of CDO */
	if (CdoPtr->Cdo1stChunk == (u8)TRUE) {
		Status = XPlmi_CdoVerifyHeader(CdoPtr);
		if (Status != XST_SUCCESS) {
			goto END;
		}
		CdoPtr->Cdo1stChunk = (u8)FALSE;
		CdoPtr->CdoLen = BufPtr[3U];

		BufPtr = &BufPtr[XPLMI_CDO_HDR_LEN];
		BufLen -= XPLMI_CDO_HDR_LEN;
		CdoPtr->BufLen -= XPLMI_CDO_HDR_LEN;
	}

	/*
	 * Check if BufLen is greater than CdoLen
	 * This is required if more buffer is copied than CDO len.
	 * Mainly for PLM CDO where BufLen is not present and is
	 * given as Max PRAM len.
	 */
	if ((BufLen + CdoPtr->ProcessedCdoLen + CdoPtr->CopiedCmdLen) > (CdoPtr->CdoLen)) {
		BufLen = CdoPtr->CdoLen - CdoPtr->ProcessedCdoLen - CdoPtr->CopiedCmdLen;
		CdoPtr->BufLen = BufLen;
	}

	/*
	 * In case CmdEnd is detected in previous iteration,
	 * it just returns
	 */
	if (CdoPtr->CmdEndDetected == (u8)TRUE) {
		Status = XST_SUCCESS;
		goto END;
	}

	XPlmi_Printf(DEBUG_INFO,
			"Processing CDO, Chunk Len 0x%08x\n\r", BufLen);
	/*
	 * Check if cmd data is copied
	 * partially during the last iteration
	 */
	if (CdoPtr->CopiedCmdLen != 0U) {
		BufPtr = CdoPtr->TempCmdBuf;
		BufLen += CdoPtr->CopiedCmdLen;
		CdoPtr->CopiedCmdLen = 0x0U;
	}

	/* Handle the break command occured in previous chunk */
	if (CdoPtr->Cmd.BreakLength != 0U) {
		RemainingLen = CdoPtr->Cmd.BreakLength - CdoPtr->ProcessedCdoLen;
		if (RemainingLen >= BufLen) {
			/* If the end is not present in current chunk, skip this chunk */
			CdoPtr->ProcessedCdoLen += BufLen;
			Status = XST_SUCCESS;
			goto END;
		}
		else {
			/* If the end is present in current chunk, jump to end command */
			CdoPtr->ProcessedCdoLen += RemainingLen;
			BufLen -= RemainingLen;
			BufPtr = &BufPtr[RemainingLen];
			CdoPtr->Cmd.BreakLength = 0U;
		}
	}

	/* Execute the commands in the Cdo Buffer */
	while (BufLen > 0U) {
		/* Check if cmd has to be resumed */
		if (CdoPtr->CmdState == XPLMI_CMD_STATE_RESUME) {
			Status =
				XPlmi_CdoCmdResume(CdoPtr, BufPtr, BufLen, &Size);
		} else {
			Status =
				XPlmi_CdoCmdExecute(CdoPtr, BufPtr, BufLen, &Size);
		}
		CdoPtr->DeferredError |= CdoPtr->Cmd.DeferredError;
		if (Status != XST_SUCCESS) {
			/*
			 * In case of any error, check if secure lockdown proc is running
			 * and continue executing the proc further without exiting the loop.
			 * Otherwise, exit the loop.
			 */
			if (SldInitiated == TRUE) {
				XPlmi_ErrMgr(Status);
			} else {
				goto END;
			}
		}
		/* If command end is detected, exit the loop */
		if (CdoPtr->CmdEndDetected == (u8)TRUE) {
			goto END;
		}

		/* Handle the break command processed in current chunk */
		if (CdoPtr->Cmd.BreakLength != 0U) {
			if (CdoPtr->Cmd.BreakLength < CdoPtr->ProcessedCdoLen) {
				Status = XPLMI_INVALID_BREAK_LENGTH;
				goto END;
			}
			if (BufLen > (Size + CdoPtr->Cmd.BreakLength - CdoPtr->ProcessedCdoLen)) {
				/* If the end is present in current chunk, jump to it */
				Size += CdoPtr->Cmd.BreakLength - CdoPtr->ProcessedCdoLen;
				CdoPtr->ProcessedCdoLen += CdoPtr->Cmd.BreakLength - CdoPtr->ProcessedCdoLen;
				CdoPtr->Cmd.BreakLength = 0U;
			}
			else {
				/* If the end is not present in current chunk, skip processing rest of the chunk */
				CdoPtr->ProcessedCdoLen += BufLen - Size;
				break;
			}
		}

		/* Update the parameters for next iteration */
		BufPtr = &BufPtr[Size];
		BufLen -= Size;
	}

	Status = XST_SUCCESS;

END:
	XPlmi_SetPlmLiveStatus();
	return Status;
}
