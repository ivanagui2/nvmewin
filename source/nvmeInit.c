/**
 *******************************************************************************
 ** Copyright (c) 2011-2012                                                   **
 **                                                                           **
 **   Integrated Device Technology, Inc.                                      **
 **   Intel Corporation                                                       **
 **   LSI Corporation                                                         **
 **                                                                           **
 ** All rights reserved.                                                      **
 **                                                                           **
 *******************************************************************************
 **                                                                           **
 ** Redistribution and use in source and binary forms, with or without        **
 ** modification, are permitted provided that the following conditions are    **
 ** met:                                                                      **
 **                                                                           **
 **   1. Redistributions of source code must retain the above copyright       **
 **      notice, this list of conditions and the following disclaimer.        **
 **                                                                           **
 **   2. Redistributions in binary form must reproduce the above copyright    **
 **      notice, this list of conditions and the following disclaimer in the  **
 **      documentation and/or other materials provided with the distribution. **
 **                                                                           **
 ** THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS   **
 ** IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, **
 ** THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR    **
 ** PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR         **
 ** CONTRIBUTORS BE LIABLE FOR ANY DIRECT,INDIRECT, INCIDENTAL, SPECIAL,      **
 ** EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,       **
 ** PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR        **
 ** PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF    **
 ** LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING      **
 ** NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS        **
 ** SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.              **
 **                                                                           **
 ** The views and conclusions contained in the software and documentation     **
 ** are those of the authors and should not be interpreted as representing    **
 ** official policies, either expressed or implied, of Intel Corporation,     **
 ** Integrated Device Technology Inc., or Sandforce Corporation.              **
 **                                                                           **
 *******************************************************************************
**/

/*
 * File: nvmeInit.c
 */

#include "precomp.h"

/*******************************************************************************
 * NVMeGetPhysAddr
 *
 * @brief Helper routine for converting virtual address to physical address by
 *        calling StorPortGetPhysicalAddress.
 *
 * @param pAE - Pointer to hardware device extension
 * @param pVirtAddr - Virtual address to convert
 *
 * @return STOR_PHYSICAL_ADDRESS
 *     Physical Address - If all resources are allocated and initialized
 *     NULL - If anything goes wrong
 ******************************************************************************/
STOR_PHYSICAL_ADDRESS NVMeGetPhysAddr(
    PNVME_DEVICE_EXTENSION pAE,
    PVOID pVirtAddr
)
{
    ULONG MappedSize;
    STOR_PHYSICAL_ADDRESS PhysAddr;

    /* Zero out the receiving buffer before converting */
    PhysAddr.QuadPart = 0;
    PhysAddr = StorPortGetPhysicalAddress(pAE,
                                          NULL,
                                          pVirtAddr,
                                          &MappedSize);

    /* If fails, log the event and print out the error */
    if ( PhysAddr.QuadPart == 0) {
        StorPortDebugPrint(ERROR,
            "NVMeGetPhysAddr: <Error> Invalid phys addr.\n");
    }

    return PhysAddr;
} /* NVMeGetPhysAddr */

/*******************************************************************************
 * NVMeGetCurCoreNumber
 *
 * @brief Helper routine for retrieving current processor number by calling
 *        StorPortGetCurrentProcessorNumber
 *
 * @param pAE - Pointer to hardware device extension
 * @param pPN - Pointer to PROCESSOR_NUMBER structure
 *
 * @return BOOLEAN
 *     TRUE - If valid number is retrieved
 *     FALSE - If anything goes wrong
 ******************************************************************************/
BOOLEAN NVMeGetCurCoreNumber(
    PNVME_DEVICE_EXTENSION pAE,
    PPROCESSOR_NUMBER pPN
)
{
    ULONG Status = STOR_STATUS_SUCCESS;

    /* Get Current processor number and make sure it's valid */
    Status = StorPortGetCurrentProcessorNumber(pAE, pPN);
    if (Status != STOR_STATUS_SUCCESS) {
        StorPortDebugPrint(ERROR,
                           "NVMeGetCurCoreNumber: <Error> Failure, Sts=%d.\n",
                           Status);
        return (FALSE);
    }

    if (pPN->Number >= pAE->ResMapTbl.NumActiveCores) {
        StorPortDebugPrint(ERROR,
                    "NVMeGetCurCoreNumber: <Error> Invalid core number = %d.\n",
                     pPN->Number);
        return (FALSE);
    }

    return (TRUE);
} /* NVMeGetCurCoreNumber */

/*******************************************************************************
 * NVMeAllocateMem
 *
 * @brief Helper routoine for Buffer Allocation.
 *        StorPortAllocateContiguousMemorySpecifyCacheNode is called to allocate
 *        memory from the preferred NUMA node. If succeeded, zero out the memory
 *        before returning to the caller.
 *
 * @param pAE - Pointer to hardware device extension
 * @param Size - In bytes
 * @param Node - Preferred NUMA node
 *
 * @return PVOID
 *    Buffer Addr - If all resources are allocated and initialized properly
 *    NULL - If anything goes wrong
 ******************************************************************************/
PVOID NVMeAllocateMem(
    PNVME_DEVICE_EXTENSION pAE,
    ULONG Size,
    ULONG Node
)
{
    PHYSICAL_ADDRESS Low;
    PHYSICAL_ADDRESS High;
    PHYSICAL_ADDRESS Align;
    PVOID pBuf = NULL;
    ULONG Status = 0;

    /* Set up preferred range and alignment before allocating */
    Low.QuadPart = 0;
    High.QuadPart = (-1);
    Align.QuadPart = 0;
    Status = StorPortAllocateContiguousMemorySpecifyCacheNode(
                 pAE, Size, Low, High, Align, MmCached, Node, (PVOID)&pBuf);

    StorPortDebugPrint(INFO,
                       "NVMeAllocateMem: Size=0x%x\n",
                       Size);

    /* It fails, log the error and return 0 */
    if ((Status != 0) || (pBuf == NULL)) {
        StorPortDebugPrint(ERROR,
                           "NVMeAllocateMem:<Error> Failure, sts=0x%x\n",
                           Status);
        return NULL;
    }

    /* Zero out the buffer before return */
    memset(pBuf, 0, Size);

    return pBuf;
} /* NVMeAllocateMem */


/*******************************************************************************
 * NVMeAllocatePool
 *
 * @brief Helper routoine for non-contiguous buffer allocation.
 *        StorPortAllocatePool is called to allocate memory from non-paged pool.
 *        If succeeded, zero out the memory before returning to the caller.
 *
 * @param pAE - Pointer to hardware device extension
 * @param Size - In bytes
 *
 * @return PVOID
 *     Buffer Addr - If all resources are allocated and initialized properly
 *     NULL - If anything goes wrong
 ******************************************************************************/
PVOID NVMeAllocatePool(
    PNVME_DEVICE_EXTENSION pAE,
    ULONG Size
)
{
    ULONG Tag = 'eMVN';
    PVOID pBuf = NULL;
    ULONG Status = STOR_STATUS_SUCCESS;

    /* Call StorPortAllocatePool to allocate the buffer */
    Status = StorPortAllocatePool(pAE, Size, Tag, (PVOID)&pBuf);

    StorPortDebugPrint(TRACE,
                       "NVMeAllocatePool: Size=0x%x\n",
                       Size );

    /* It fails, log the error and return NULL */
    if ((Status != STOR_STATUS_SUCCESS) || (pBuf == NULL)) {
        StorPortDebugPrint(ERROR,
                           "NVMeAllocatePool:<Error> Failure, sts=0x%x\n",
                           Status );
        return NULL;
    }

    /* Zero out the buffer before return */
    memset(pBuf, 0, Size);

    return pBuf;
} /* NVMeAllocatePool */

/*******************************************************************************
 * NVMeActiveProcessorCount
 *
 * @brief Helper routoine for deciding the number of active processors of an
 *        NUMA node.
 *
 * @param Mask - bitmap of the active processors
 *
 * @return USHORT
 *     The number of active processors
 ******************************************************************************/
USHORT NVMeActiveProcessorCount(
    ULONG_PTR Mask
)
{
    USHORT Count = 0;

    /*
     * Loop thru the bits of Mask (a 32 or 64-bit value), increase the count
     * by one when Mask is non-zero after bit-wise AND operation between Mask
     * and Mask-1. This figures out the number of bits (in Mask) that are set
     * as 1.
     */
    while (Mask) {
        Count++;
        Mask &= (Mask - 1);
    }

    return Count;
} /* NVMeActiveProcessorCount */

/*******************************************************************************
 * NVMeEnumNumaCores
 *
 * @brief NVMeEnumNumaCores collects the current NUMA/CPU topology information.
 *        Confirms if NUMA is supported, how many NUMA nodes in current system
 *        before allocating memory for CORE_TBL structures
 *
 * @param pAE - Pointer to hardware device extension
 *
 * @return BOOLEAN
 *     TRUE - If all resources are allocated and initialized properly
 *     FALSE - If anything goes wrong
 ******************************************************************************/
BOOLEAN NVMeEnumNumaCores(
    PNVME_DEVICE_EXTENSION pAE
)
{
    GROUP_AFFINITY GroupAffinity;
    USHORT Bit = 0;
    USHORT Core = 0;
    USHORT BaseCoreNum;
    ULONG Node = 0;
    ULONG Status = 0;
    ULONG TotalCores = 0;
    USHORT MaxNumCoresInGroup = sizeof(KAFFINITY) * 8;
    PRES_MAPPING_TBL pRMT = &pAE->ResMapTbl;
    PCORE_TBL pCoreTblTemp = NULL;
    PNUMA_NODE_TBL pNNT = NULL;
    BOOLEAN FirstCoreFound;

    /*
     * Decide how many NUMA nodes in current system if only one (0 returned),
     * NUMA is not supported.
     */
    StorPortGetHighestNodeNumber( pAE, &Node );
    pRMT->NumNumaNodes = Node + 1;

    StorPortDebugPrint(INFO,
                       "NVMeEnumNumaCores: # of NUMA node(s) = %d.\n",
                       Node + 1);
    /*
     * Allocate buffer for NUMA_NODE_TBL structure array.
     * If fails, return FALSE
     */
    pRMT->pNumaNodeTbl = (PNUMA_NODE_TBL)
        NVMeAllocatePool(pAE, pRMT->NumNumaNodes * sizeof(NUMA_NODE_TBL));

    if (pRMT->pNumaNodeTbl == NULL)
        return (FALSE);

    /* Based on NUMA node number, retrieve their affinity masks and counts */
    for (Node = 0; Node < pRMT->NumNumaNodes; Node++) {
        pNNT = pRMT->pNumaNodeTbl + Node;

        StorPortDebugPrint(INFO, "NVMeEnumNumaCores: NUMA Node#%d\n", Node);

        /* Retrieve the processor affinity based on NUMA nodes */
        memset((PVOID)&GroupAffinity, 0, sizeof(GROUP_AFFINITY));

        Status = StorPortGetNodeAffinity(pAE, Node, &GroupAffinity);
        if (Status != STOR_STATUS_SUCCESS) {
            StorPortDebugPrint(ERROR,
                "NVMeEnumNumaCores: <Error> GetNodeAffinity fails, sts=0x%x\n",
                Status);
            return (FALSE);
        }

        StorPortDebugPrint(INFO, "Core mask is 0x%x\n", GroupAffinity.Mask);

        /* Find out the number of active cores of the NUMA node */
        pNNT->NumCores = NVMeActiveProcessorCount(GroupAffinity.Mask);
        pRMT->NumActiveCores += pNNT->NumCores;
        StorPortMoveMemory((PVOID)&pNNT->GroupAffinity,
                           (PVOID)&GroupAffinity,
                           sizeof(GROUP_AFFINITY));
    }

    /* Allocate buffer for CORE_TBL structure array. If fails, return FALSE */
    pRMT->pCoreTbl = (PCORE_TBL)
        NVMeAllocatePool(pAE, pRMT->NumActiveCores * sizeof(CORE_TBL));

    if (pRMT->pCoreTbl == NULL)
        return (FALSE);

    /* Based on NUMA node number, populate the NUMA/Core tables */
    for (Node = 0; Node < pRMT->NumNumaNodes; Node++) {
        pNNT = pRMT->pNumaNodeTbl + Node;
        BaseCoreNum = pNNT->GroupAffinity.Group * MaxNumCoresInGroup;

        /*
         * For each existing NUMA node, need to find out its first and last
         * associated cores in terms of system-wise processor number for
         * later reference in IO queue allocation. Initialize the as the first
         * core number of the associated group.
         */
        pNNT->FirstCoreNum = BaseCoreNum;
        pNNT->LastCoreNum = BaseCoreNum;
        FirstCoreFound = FALSE;

        /* For each core, populate CORE_TBL structure */
        for (Bit = 0; Bit < MaxNumCoresInGroup; Bit++) {
            /* Save previsou bit check result first */
            if (((pNNT->GroupAffinity.Mask >> Bit) & 1) == 1) {
                Core = BaseCoreNum + Bit;
                pCoreTblTemp = pRMT->pCoreTbl + Core;
                pCoreTblTemp->CoreNum = (USHORT) Core;
                pCoreTblTemp->NumaNode = (USHORT) Node;
                pCoreTblTemp->Group = pNNT->GroupAffinity.Group;

                /* Mark the first core if haven't found yet */
                if (FirstCoreFound == FALSE) {
                    pNNT->FirstCoreNum = Core;
                    FirstCoreFound = TRUE;
                }

                /* Always mark the last core */
                pNNT->LastCoreNum = Core;
                TotalCores++;
            }
        }

        StorPortDebugPrint(INFO,
                           "There are %d cores in Node#%d.\n",
                           pNNT->NumCores, Node);
    }

    /* Double check the total core number */
    if (TotalCores > pRMT->NumActiveCores) {
        StorPortDebugPrint(ERROR,
            "NVMeEnumNumaCores: <Error> Cores number mismatch, %d, %d\n",
            TotalCores, pRMT->NumActiveCores);

        return (FALSE);
    }

    StorPortDebugPrint(INFO,
                       "The total number of CPU cores %d.\n",
                       pRMT->NumActiveCores);

    return(TRUE);
} /* NVMeEnumNumaCores */

/*******************************************************************************
 * NVMeStrCompare
 *
 * @brief Helper routoine for deciding which driver is being loaded
 *
 * @param pTargetString - Pointer to the target string to compare with.
 * @param pArgumentString - Pointer to the string from the system.
 *
 * @return BOOLEAN
 *     TRUE - If it's a normal driver being loaded
 *     FALSE - If it's a crashdump or hibernation driver being loaded
 ******************************************************************************/
BOOLEAN NVMeStrCompare(
    PCSTR pTargetString,
    PCSTR pArgumentString
)
{
    PCHAR pTargetIndex = (PCHAR) pTargetString;
    PCHAR pArgumentIndex = (PCHAR) pArgumentString;
    CHAR Target;
    CHAR Argument;

    if (pArgumentIndex == NULL) {
        return (FALSE);
    }

    while (*pTargetIndex) {
        Target = *pTargetIndex;
        Argument = *pArgumentIndex;

        /* Lower case the char. */
        if ((Target >= 'A') && (Target <= 'Z')) {
            Target = Target + ('a' - 'A');
        }

        if ((Argument >= 'A') && (Argument <= 'Z')) {
            Argument = Argument + ('a' - 'A');
        }

        /* Return FALSE if not matched */
        if (Target != Argument) {
            return FALSE;
        }

        pTargetIndex++;
        pArgumentIndex++;
    }

    return (TRUE);
} /* NVMeStrCompare */

/*******************************************************************************
 * NVMeEnumMsiMessages
 *
 * @brief NVMeEnumMsiMessages retrieves the related information of granted MSI
 *        vectors, such as, address, data, and the number of granted vectors,
 *        etc...
 *
 * @param pAE - Pointer to hardware device extension.
 *
 * @return BOOLEAN
 *     TRUE - If all resources are allocated and initialized properly
 *     FALSE - If anything goes wrong
 ******************************************************************************/
BOOLEAN NVMeEnumMsiMessages (
    PNVME_DEVICE_EXTENSION pAE
)
{
    ULONG32 MsgID;
    ULONG32 Status = STOR_STATUS_SUCCESS;
    MESSAGE_INTERRUPT_INFORMATION MII;
    PRES_MAPPING_TBL pRMT = &pAE->ResMapTbl;
    PMSI_MESSAGE_TBL pMMT = NULL;

    /* Assuming it's MSI-X by defult and find it out later */
    pRMT->InterruptType = INT_TYPE_MSIX;

    /*
     * Loop thru each MessageID by calling StorPortMSIInfo
     * to see if it is granted by OS and figure out how many
     * messages are actually granted
     */
    for (MsgID = 0; MsgID <= pRMT->NumActiveCores; MsgID++) {
        pMMT = pRMT->pMsiMsgTbl + MsgID;

        memset(&MII, 0, sizeof(MESSAGE_INTERRUPT_INFORMATION));

        Status = StorPortGetMSIInfo ( pAE, MsgID, &MII );
        if (Status == STOR_STATUS_SUCCESS) {
            /* It's granted only when the IDs matched */
            if (MsgID == MII.MessageId) {
                pMMT->MsgID = MII.MessageId;
                pMMT->Addr = MII.MessageAddress;
                pMMT->Data = MII.MessageData;
            } else {
                ASSERT(FALSE);
            }
        } else {
            /* Use INTx when failing to retrieve any message information */
            if (MsgID == 0)
                pRMT->InterruptType = INT_TYPE_INTX;

            break;
        }
    }

    pRMT->NumMsiMsgGranted = MsgID;

    StorPortDebugPrint(INFO,
                       "NVMeEnumMsiMessages: Msg granted=%d\n",
                       pRMT->NumMsiMsgGranted);

    /* Is the request message number satisfied? */
    if (pRMT->NumMsiMsgGranted > 1) {
        if (pRMT->NumMsiMsgGranted > pRMT->NumActiveCores) {
            /*
             * Seems they are all granted, need to determine it's MSI or MSI-X.
             * If the addresses for the first 2 messaes are == then its MSI
             */
            pMMT = pRMT->pMsiMsgTbl + 1;
            if (pMMT->Addr.QuadPart == pRMT->pMsiMsgTbl->Addr.QuadPart) {
                pRMT->InterruptType = INT_TYPE_MSI;
            }
        } else {
            /*
             * Should not happen, Windows either grants all requested messages
             * or just one. In this case, only the first message is used and
             * shared as well
             */
            pRMT->InterruptType = INT_TYPE_MSI;
            pRMT->pMsiMsgTbl->CoreNum = RESOURCE_SHARED;
            pRMT->pMsiMsgTbl->Shared = TRUE;
        }
    } else if (pRMT->NumMsiMsgGranted == 1) {
        /* Only one message granted and it is shared */
        pRMT->InterruptType = INT_TYPE_MSI;
        pRMT->pMsiMsgTbl->CoreNum = RESOURCE_SHARED;
        pRMT->pMsiMsgTbl->Shared = TRUE;
    } else {
        /* Using INTx and the interrupt is shared anyway */
        pRMT->pMsiMsgTbl->CoreNum = RESOURCE_SHARED;
        pRMT->pMsiMsgTbl->Shared = TRUE;
    }

    return (TRUE);
} /* NVMeEnumMsiMessages */

/*******************************************************************************
 * NVMeMsiMapCores
 *
 * @brief NVMeMsiMapCores is called to setup the initial mapping for MSI or MSIX
 *        modes.  Initial mapping is just 1:1, learning will happen as each core
 *        processes an IO and new mappings will be created for optimal use.
 *
 * @param pAE - Pointer to hardware device extension.
 *
 * @return VOID
 ******************************************************************************/
VOID NVMeMsiMapCores(
    PNVME_DEVICE_EXTENSION pAE
)
{
    UCHAR Core;
    PRES_MAPPING_TBL pRMT = &pAE->ResMapTbl;
    PMSI_MESSAGE_TBL pMMT = NULL;
    PCORE_TBL pCT = NULL;

    /*
     * If not enough messages granted, the interrupt type is set as
     * INT_TYPE_MSI and all cores share it.
     */
    if (pRMT->NumMsiMsgGranted <= pRMT->NumActiveCores) {
        /*
         * Resource Mapping table is already completed in NVMeEnumMsiMessages,
         * simply return here.
         */
        return;
    }

    /*
     * Loop thru the cores and assign granted messages in sequential manner.
     * When requests completed, based on the messagID and look up the
     * associated completion queue for just-completed entries
     */
    for (Core = 0; Core < pRMT->NumActiveCores; Core++) {
        /* Handle one Core Table at a time */
        pCT = pRMT->pCoreTbl + Core;

        /* Mark down the initial associated message + SQ/CQ for this core */
        pCT->MsiMsgID = pCT->CplQueue;

        /*
         * On the other side, mark down the associated core number
         * for the message as well
         */
        pMMT = pRMT->pMsiMsgTbl + pCT->MsiMsgID;
        pMMT->CoreNum = Core;
        pMMT->CplQueueNum = pCT->CplQueue;

        StorPortDebugPrint(INFO,
                           "NVMeMsiMapCores: Core(0x%x)Msg#(0x%x)\n",
                           Core, pCT->MsiMsgID);
    }
} /* NVMeMsiMapCores */

/*******************************************************************************
 * NVMeCompleteResMapTbl
 *
 * @brief NVMeCompleteResMapTbl completes the resource mapping table among
 *        active cores, the granted vectors and the allocated IO queues.
 *
 * @param pAE - Pointer to hardware device extension.
 *
 * @return VOID
 ******************************************************************************/
VOID NVMeCompleteResMapTbl(
    PNVME_DEVICE_EXTENSION pAE
)
{
    PRES_MAPPING_TBL pRMT = &pAE->ResMapTbl;

    /* The last thing to do is completing Resource Mapping Table. */
    if ( (pRMT->InterruptType == INT_TYPE_MSIX) ||
         (pRMT->InterruptType == INT_TYPE_MSI) ) {
        NVMeMsiMapCores(pAE);
    }

    /* No need to do anything more for INTx */

} /* NVMeCompleteResMapTbl */

/*******************************************************************************
 * NVMeMapCore2Queue
 *
 * @brief NVMeMapCore2Queue is called to retrieve the associated queue ID of the
 *        given core number
 *
 * @param pAE - Pointer to hardware device extension
 * @param pPN - Pointer to PROCESSOR_NUMBER structure
 * @param pSubQueue - Pointer to buffer to save retrieved submI[QueueID]ssion queue ID
 * @param pCplQueue - Pointer to buffer to save retrieved Completion queue ID
 *
 * @return ULONG
 *     STOR_STATUS_SUCCESS - If valid number is retrieved
 *     STOR_STATUS_UNSUCCESSFUL - If anything goes wrong
 ******************************************************************************/
ULONG NVMeMapCore2Queue(
    PNVME_DEVICE_EXTENSION pAE,
    PPROCESSOR_NUMBER pPN,
    USHORT* pSubQueue,
    USHORT* pCplQueue
)
{
    PRES_MAPPING_TBL pRMT = &pAE->ResMapTbl;
    PCORE_TBL pCT = NULL;

    /* Ensure the core number is valid first */
    if (pPN->Number >= pRMT->NumActiveCores) {
        StorPortDebugPrint(ERROR,
            "NVMeGetCurCoreNumber: <Error> Invalid core number = %d.\n",
            pPN->Number);

        return (STOR_STATUS_UNSUCCESSFUL);
    }

    /* Locate the target CORE_TBL entry for the specific core number
     * indexed depending on whether we're still learning the table or not
     */
    if (pAE->LearningCores == pRMT->NumActiveCores) {
        pCT = pRMT->pCoreTbl + pPN->Number;
        /* Return the queue IDs */
        *pSubQueue = pCT->SubQueue;
        *pCplQueue = pCT->CplQueue;
    } else {
        *pSubQueue = (USHORT)pAE->LearningCores + 1;
        *pCplQueue = (USHORT)pAE->LearningCores + 1;
    }

    return (STOR_STATUS_SUCCESS);
} /* NVMeMapCore2Queue */

/*******************************************************************************
 * NVMeInitFreeQ
 *
 * @brief NVMeInitFreeQ gets called to initialize the free queue list of the
 *        specific submission queue with its associated command entries and PRP
 *        List buffers.
 *
 * @param pSQI - Pointer to the SUB_QUEUE_INFO structure.
 *
 * @return VOID
 ******************************************************************************/
VOID NVMeInitFreeQ(
    PSUB_QUEUE_INFO pSQI,
    PNVME_DEVICE_EXTENSION pAE
)
{
    USHORT Entry;
    PCMD_ENTRY pCmdEntry = NULL;
    PCMD_INFO pCmdInfo = NULL;
    ULONG_PTR CurPRPList = 0;
    ULONG prpListSz = 0;
#ifdef DUMB_DRIVER
    ULONG_PTR PtrTemp;
    ULONG dblBuffSz = 0;
#endif

    /* For each entry, initialize the CmdID and PRPList flields */
    CurPRPList = (ULONG_PTR)((PUCHAR)pSQI->pPRPListStart);

    for (Entry = 0; Entry < pSQI->SubQEntries; Entry++) {
        pCmdEntry = (PCMD_ENTRY)pSQI->pCmdEntry;
        pCmdEntry += Entry;
        pCmdInfo = &pCmdEntry->CmdInfo;

        /*
         * Set up CmdID and dedicated PRP Lists before adding to FreeQ
         * Use Entry to locate the starting point.
         */
        pCmdInfo->CmdID = Entry;
        pCmdInfo->pPRPList = (PVOID)(CurPRPList + pAE->PRPListSize);

        /*
         * Because PRP List can't cross page boundary, if not enough room left
         * for one list, need to move on to next page boundary.
         * NumPRPListOnepage is calculated for this purpose.
         */
        if (Entry != 0 && ((Entry % pSQI->NumPRPListOnePage) == 0))
            pCmdInfo->pPRPList = PAGE_ALIGN_BUF_PTR(pCmdInfo->pPRPList);

        /* Save the address of current list for calculating next list */
        CurPRPList = (ULONG_PTR)pCmdInfo->pPRPList;
        pCmdInfo->prpListPhyAddr = StorPortGetPhysicalAddress(pAE,
                                      NULL,
                                      pCmdInfo->pPRPList,
                                      &prpListSz);

#ifdef DUMB_DRIVER
        PtrTemp = (ULONG_PTR)((PUCHAR)pSQI->pDlbBuffStartVa);
        pCmdInfo->pDblVir = (PVOID)(PtrTemp + (DUMB_DRIVER_SZ * Entry));
        pCmdInfo->dblPhy = StorPortGetPhysicalAddress(pAE,
                                                      NULL,
                                                      pCmdInfo->pDblVir,
                                                      &dblBuffSz);

        PtrTemp = (ULONG_PTR)((PUCHAR)pSQI->pDlbBuffStartListVa);
        pCmdInfo->pDblPrpListVir = (PVOID)(PtrTemp + (PAGE_SIZE * Entry));
        pCmdInfo->dblPrpListPhy = StorPortGetPhysicalAddress(pAE,
                                                      NULL,
                                                      pCmdInfo->pDblPrpListVir,
                                                      &dblBuffSz);
#endif

        InsertTailList(&pSQI->FreeQList, &pCmdEntry->ListEntry);
    }
} /* NVMeInitFreeQ */

/*******************************************************************************
 * NVMeAllocQueues
 *
 * @brief NVMeAllocQueues gets called to allocate buffers in
 *        non-paged, contiguous memory space for Submission/Completion queues.
 *
 * @param pAE - Pointer to hardware device extension.
 * @param QueueID - Which queue to allocate memory for
 * @param QEntries - the number of entries to allocate for this queue
 * @param NumaNode - Which NUMA node associated memory to allocate from
 *
 * @return ULONG
 *     STOR_STATUS_SUCCESS - If all resources are allocated properly
 *     Otherwise - If anything goes wrong
 ******************************************************************************/
ULONG NVMeAllocQueues (
    PNVME_DEVICE_EXTENSION pAE,
    USHORT QueueID,
    ULONG QEntries,
    USHORT NumaNode
)
{

    /* The number of Submission entries makes up exact one system page size */
    ULONG SysPageSizeInSubEntries;
    PQUEUE_INFO pQI = &pAE->QueueInfo;
    PSUB_QUEUE_INFO pSQI = NULL;
    PRES_MAPPING_TBL pRMT = &pAE->ResMapTbl;
    ULONG SizeQueueEntry = 0;
    ULONG NumPageToAlloc = 0;

    /* Ensure the QueueID is valid via the number of active cores in system */
    if (QueueID > pRMT->NumActiveCores)
        return (STOR_STATUS_INVALID_PARAMETER);

    /* Locate the target SUB_QUEUE_STRUCTURE via QueueID */
    pSQI = pQI->pSubQueueInfo + QueueID;

    /*
     * To ensure:
     *   1. Allocating enough memory and
     *   2. The starting addresses of Sub/Com queues are system page aligned.
     *
     *      Need to:
     *        1. Round up the allocated size of all Submission entries to be
     *           multiple(s) of system page size.
     *        2. Add one extra system page to allocation size
     */
    SysPageSizeInSubEntries = PAGE_SIZE / sizeof (NVMe_COMMAND);
    if ((QEntries % SysPageSizeInSubEntries) != 0)
        QEntries = (QEntries + SysPageSizeInSubEntries) &
                  ~(SysPageSizeInSubEntries - 1);

    /*
     * Determine the allocation size in bytes
     *   1. For Sub/Cpl/Cmd entries
     *   2. For PRP Lists
     */
    SizeQueueEntry = QEntries * (sizeof(NVMe_COMMAND) +
                                 sizeof(NVMe_COMPLETION_QUEUE_ENTRY) +
                                 sizeof(CMD_ENTRY));

    /* Allcate memory for Sub/Cpl/Cmd entries first */
    pSQI->pQueueAlloc = NVMeAllocateMem(pAE,
                                        SizeQueueEntry + PAGE_SIZE,
                                        NumaNode);

    if (pSQI->pQueueAlloc == NULL)
        return ( STOR_STATUS_INSUFFICIENT_RESOURCES );

    /* Save the size if needed to free the unused buffers */
    pSQI->QueueAllocSize = SizeQueueEntry + PAGE_SIZE;

#ifdef DUMB_DRIVER
    pSQI->pDblBuffAlloc = NVMeAllocateMem(pAE,
                                          (QEntries * DUMB_DRIVER_SZ) + PAGE_SIZE,
                                          NumaNode);

    if (pSQI->pDblBuffAlloc == NULL)
        return ( STOR_STATUS_INSUFFICIENT_RESOURCES );

    /* Save the size if needed to free the unused buffers */
    pSQI->dblBuffSz = (QEntries * DUMB_DRIVER_SZ) + PAGE_SIZE;

    /* now the PRP list mem for this SQ to use */
    pSQI->pDblBuffListAlloc = NVMeAllocateMem(pAE,
                                          (QEntries * PAGE_SIZE) + PAGE_SIZE,
                                          NumaNode);

    if (pSQI->pDblBuffListAlloc == NULL)
        return ( STOR_STATUS_INSUFFICIENT_RESOURCES );

    /* Save the size if needed to free the unused buffers */
    pSQI->dblBuffListSz = (QEntries * PAGE_SIZE) + PAGE_SIZE;
#endif

    /*
     * Allcate memory for PRP Lists In order not crossing page boundary, need to
     * calculate how many lists one page can accommodate.
     */
    pSQI->NumPRPListOnePage = PAGE_SIZE / pAE->PRPListSize;
    NumPageToAlloc = (QEntries % pSQI->NumPRPListOnePage) ?
        (QEntries / pSQI->NumPRPListOnePage) + 1 :
        (QEntries / pSQI->NumPRPListOnePage);

    pSQI->pPRPListAlloc = NVMeAllocateMem(pAE,
                                          (NumPageToAlloc + 1) * PAGE_SIZE,
                                          NumaNode);

    if (pSQI->pPRPListAlloc == NULL) {
        /* Free the allcated memory for Sub/Cpl/Cmd entries before returning */
        StorPortFreeContiguousMemorySpecifyCache((PVOID)pAE,
                                                 pSQI->pQueueAlloc,
                                                 pSQI->QueueAllocSize,
                                                 MmCached);

        return ( STOR_STATUS_INSUFFICIENT_RESOURCES );
    }

    /* Save the size if needed to free the unused buffers */
    pSQI->PRPListAllocSize = (NumPageToAlloc + 1) * PAGE_SIZE;

    /* Mark down the number of entries allocated successfully */
    if (QueueID != 0) {
        pQI->NumIoQEntriesAllocated = (USHORT)QEntries;
    } else {
        pQI->NumAdQEntriesAllocated = (USHORT)QEntries;
    }

    return (STOR_STATUS_SUCCESS);
} /* NVMeAllocQueues */

/*******************************************************************************
 * NVMeInitSubQueue
 *
 * @brief NVMeInitSubQueue gets called to initialize the SUB_QUEUE_INFO
 *        structure of the specific submission queue associated with the QueueID
 *
 * @param pAE - Pointer to hardware device extension.
 * @param QueueID - Which submission queue structure to initialize
 *
 * @return ULONG
 *     STOR_STATUS_SUCCESS - If all resources are allocated properly
 *     Otherwise - If anything goes wrong
 ******************************************************************************/
ULONG NVMeInitSubQueue(
    PNVME_DEVICE_EXTENSION pAE,
    USHORT QueueID
)
{
    PQUEUE_INFO pQI = &pAE->QueueInfo;
    PSUB_QUEUE_INFO pSQI = pQI->pSubQueueInfo + QueueID;
    PRES_MAPPING_TBL pRMT = &pAE->ResMapTbl;
    ULONG_PTR PtrTemp = 0;
    USHORT Entries;
    ULONG dbIndex = 0;

    /* Ensure the QueueID is valid via the number of active cores in system */
    if (QueueID > pRMT->NumActiveCores)
        return ( STOR_STATUS_INVALID_PARAMETER );

    /* Initialize static fields of SUB_QUEUE_INFO structure */
    pSQI->SubQEntries = (QueueID != 0) ? pQI->NumIoQEntriesAllocated :
                                         pQI->NumAdQEntriesAllocated;
    pSQI->SubQueueID = QueueID;
    pSQI->FreeSubQEntries = pSQI->SubQEntries;

     /* calculate byte offset per 1.0c spec formula */
    dbIndex = 2 * QueueID * (4 << pAE->pCtrlRegister->CAP.DSTRD);
    /* convert to index */
    dbIndex = dbIndex / sizeof(NVMe_QUEUE_Y_DOORBELL);
    pSQI->pSubTDBL = (PULONG)(&pAE->pCtrlRegister->IODB[dbIndex].QHT );
    StorPortDebugPrint(INFO,
                       "NVMeInitSubQueue : SQ 0x%x pSubTDBL 0x%x at index  0x%x\n",
                       QueueID, pSQI->pSubTDBL, dbIndex);
    pSQI->Requests = 0;
    pSQI->SubQTailPtr = 0;
    pSQI->SubQHeadPtr = 0;

    /*
     * The queue is shared by cores when:
     *   it's Admin queue, or
     *   not enough IO queues are allocated, or
     *   we are in crashdump.
     */
    if ((QueueID == 0)                                   ||
        (pQI->NumSubIoQAllocated < pRMT->NumActiveCores) ||
        (pAE->ntldrDump == TRUE)) {
        pSQI->Shared = TRUE;
    }

    pSQI->CplQueueID = QueueID;

    /*
     * Initialize submission queue starting point. Per NVMe specification, need
     * to make it system page aligned if it's not.
     */
    pSQI->pSubQStart = PAGE_ALIGN_BUF_PTR(pSQI->pQueueAlloc);

    memset(pSQI->pQueueAlloc, 0, pSQI->QueueAllocSize);

    pSQI->SubQStart = NVMeGetPhysAddr(pAE, pSQI->pSubQStart);
    /* If fails on converting to physical address, return here */
    if (pSQI->SubQStart.QuadPart == 0)
        return ( STOR_STATUS_INSUFFICIENT_RESOURCES );

#ifdef DUMB_DRIVER
    pSQI->pDlbBuffStartVa = PAGE_ALIGN_BUF_PTR(pSQI->pDblBuffAlloc);
    memset(pSQI->pDblBuffAlloc, 0, pSQI->dblBuffSz);

    pSQI->pDlbBuffStartListVa = PAGE_ALIGN_BUF_PTR(pSQI->pDblBuffListAlloc);
    memset(pSQI->pDblBuffListAlloc, 0, pSQI->dblBuffListSz);
#endif

    /*
     * Initialize PRP list starting point. Per NVMe specification, need to make
     * it system page aligned if it's not.
     */
    pSQI->pPRPListStart = PAGE_ALIGN_BUF_PTR(pSQI->pPRPListAlloc);
    memset(pSQI->pPRPListAlloc, 0, pSQI->PRPListAllocSize);

    pSQI->PRPListStart = NVMeGetPhysAddr( pAE, pSQI->pPRPListStart );
    /* If fails on converting to physical address, return here */
    if (pSQI->PRPListStart.QuadPart == 0)
        return ( STOR_STATUS_INSUFFICIENT_RESOURCES );

    /* Initialize list head of the free queue list */
    InitializeListHead(&pSQI->FreeQList);

    return (STOR_STATUS_SUCCESS);
} /* NVMeInitSubQueue */

/*******************************************************************************
 * NVMeInitCplQueue
 *
 * @brief NVMeInitCplQueue gets called to initialize the CPL_QUEUE_INFO
 *        structure of the specific completion queue associated with the QueueID
 *
 * @param pAE - Pointer to hardware device extension.
 * @param QueueID - Which completion queue structure to initialize
 *
 * @return ULONG
 *     STOR_STATUS_SUCCESS - If all resources are allocated properly
 *     Otherwise - If anything goes wrong
 ******************************************************************************/
ULONG NVMeInitCplQueue(
    PNVME_DEVICE_EXTENSION pAE,
    USHORT QueueID
)
{
    PQUEUE_INFO pQI = &pAE->QueueInfo;
    PSUB_QUEUE_INFO pSQI = NULL;
    PCPL_QUEUE_INFO pCQI = NULL;
    PRES_MAPPING_TBL pRMT = &pAE->ResMapTbl;
    PCORE_TBL pCT = NULL;
    ULONG_PTR PtrTemp;
    USHORT Entries;
    ULONG queueSize = 0;
    ULONG dbIndex = 0;

    /* Ensure the QueueID is valid via the number of active cores in system */
    if (QueueID > pRMT->NumActiveCores)
        return ( STOR_STATUS_INVALID_PARAMETER );

    pSQI = pQI->pSubQueueInfo + QueueID;
    pCQI = pQI->pCplQueueInfo + QueueID;

    /* Initialize static fields of CPL_QUEUE_INFO structure */
    pCQI->CplQueueID = QueueID;
    pCQI->CplQEntries = pSQI->SubQEntries;

    /* calculate byte offset per 10.c spec formula  */
    dbIndex = (2 * QueueID + 1) * (4 << pAE->pCtrlRegister->CAP.DSTRD);
    /* convert to index */
    dbIndex = dbIndex / sizeof(NVMe_QUEUE_Y_DOORBELL);
    pCQI->pCplHDBL = (PULONG)(&pAE->pCtrlRegister->IODB[dbIndex].QHT );
    StorPortDebugPrint(INFO,
                       "NVMeInitCplQueue : CQ 0x%x pCplHDBL 0x%x at index  0x%x\n",
                       QueueID, pCQI->pCplHDBL, dbIndex);
    pCQI->Completions = 0;
    pCQI->CurPhaseTag = 0;
    pCQI->CplQHeadPtr = 0;

    /**
     * The queue is shared by cores when:
     *   it's Admin queue, or
     *   not enough IO queues are allocated, or
     *   we are in crashdump.
     */
    if ((QueueID == 0)                                   ||
        (pQI->NumCplIoQAllocated < pRMT->NumActiveCores) ||
        (pAE->ntldrDump == TRUE)) {
        pCQI->Shared = TRUE;
    }

    if (pRMT->InterruptType == INT_TYPE_MSI ||
        pRMT->InterruptType == INT_TYPE_MSIX) {
        if (pRMT->NumMsiMsgGranted <= pRMT->NumActiveCores) {
            /* All completion queueus share the single message */
            pCQI->MsiMsgID = 0;
        } else {
            /*
             * When enough queues allocated, the mappings between cores and
             * queues are set up as core#(n) <=> queue#(n+1)
             * Only need to deal with IO queues since Admin queue uses message#0
             */
            if (QueueID != 0) {
                pCT = pRMT->pCoreTbl + QueueID - 1;
                pCQI->MsiMsgID = pCT->MsiMsgID;
            }
        }
    }

    /*
     * Initialize completion queue entries. Firstly, make Cpl queue starting
     * entry system page aligned.
     */
    queueSize = pSQI->SubQEntries * sizeof(NVMe_COMMAND);
    PtrTemp = (ULONG_PTR)((PUCHAR)pSQI->pSubQStart);
    pCQI->pCplQStart = (PVOID)(PtrTemp + queueSize);

    memset(pCQI->pCplQStart, 0, queueSize);
    pCQI->pCplQStart = PAGE_ALIGN_BUF_PTR(pCQI->pCplQStart);

    pCQI->CplQStart = NVMeGetPhysAddr( pAE, pCQI->pCplQStart );
    /* If fails on converting to physical address, return here */
    if (pCQI->CplQStart.QuadPart == 0)
        return ( STOR_STATUS_INSUFFICIENT_RESOURCES );

    return (STOR_STATUS_SUCCESS);
} /* NVMeInitCplQueue */

/*******************************************************************************
 * NVMeInitCmdEntries
 *
 * @brief NVMeInitCmdEntries gets called to initialize the CMD_ENTRY structure
 *        of the specific submission queue associated with the QueueID
 *
 * @param pAE - Pointer to hardware device extension.
 * @param QueueID - Which submission queue structure to initialize
 *
 * @return ULONG
 *     STOR_STATUS_SUCCESS - If all resources are allocated properly
 *     Otherwise - If anything goes wrong
 ******************************************************************************/
ULONG NVMeInitCmdEntries(
    PNVME_DEVICE_EXTENSION pAE,
    USHORT QueueID
)
{
    PQUEUE_INFO pQI = &pAE->QueueInfo;
    PSUB_QUEUE_INFO pSQI = pQI->pSubQueueInfo + QueueID;
    PCPL_QUEUE_INFO pCQI = pQI->pCplQueueInfo + QueueID;
    PRES_MAPPING_TBL pRMT = &pAE->ResMapTbl;
    ULONG_PTR PtrTemp = 0;

    /* Ensure the QueueID is valid via the number of active cores in system */
    if (QueueID > pRMT->NumActiveCores)
        return (STOR_STATUS_INVALID_PARAMETER);

    /* Initialize command entries and Free list */
    PtrTemp = (ULONG_PTR)((PUCHAR)pCQI->pCplQStart);
    pSQI->pCmdEntry = (PVOID) (PtrTemp + (pSQI->SubQEntries *
                                          sizeof(NVMe_COMPLETION_QUEUE_ENTRY)));

    memset(pSQI->pCmdEntry, 0, sizeof(CMD_ENTRY) * pSQI->SubQEntries);
    NVMeInitFreeQ(pSQI, pAE);

    return (STOR_STATUS_SUCCESS);
} /* NVMeInitCmdEntries */

/*******************************************************************************
 * NVMeResetAdapter
 *
 * @brief NVMeResetAdapter gets called to reset the adapter by setting EN bit of
 *        CC register as 0. This causes the adapter to forget about queues --
 *        the internal adapter falls back to initial state.
 *
 * @param pAE - Pointer to hardware device extension.
 *
 * @return BOOLEAN
 *     TRUE - If reset procedure seemed to worky
 *     FALSE - If anything goes wrong
 ******************************************************************************/
BOOLEAN NVMeResetAdapter(
    PNVME_DEVICE_EXTENSION pAE
)
{
    NVMe_CONTROLLER_CONFIGURATION CC;

    /* Need to to ensure the Controller registers are memory-mapped properly */
    if (pAE->pCtrlRegister == NULL)
        return (FALSE);

    /*
     * Immediately reset our start state to indicate that the controller
     * is not ready
     */
    CC.AsUlong= StorPortReadRegisterUlong(pAE,
                                          (PULONG)(&pAE->pCtrlRegister->CC));
    CC.EN = 0;

    StorPortWriteRegisterUlong(pAE,
                               (PULONG)(&pAE->pCtrlRegister->CC),
                               CC.AsUlong);

    pAE->DriverState.NextDriverState = NVMeWaitOnRDY;

    return (TRUE);
} /* NVMeResetAdapter */

/*******************************************************************************
 * NVMeWaitOnReady
 *
 * @brief Polls on the status bit waiting for RDY state
 *
 * @param pAE - Pointer to hardware device extension.
 *
 * @return BOOLEAN
 *     TRUE - If it went RDY before the timeout
 *     FALSE - If anything goes wrong
 ******************************************************************************/
BOOLEAN NVMeWaitOnReady(
    PNVME_DEVICE_EXTENSION pAE
)
{
    NVMe_CONTROLLER_STATUS CSTS;
    ULONG PollMax = pAE->uSecCrtlTimeout / MAX_STATE_STALL_us;
    ULONG PollCount;

    /*
     * Find out the Timeout value from Controller Capability register,
     * which is in 500 ms.
     * In case the read back unit is 0, make it 1, i.e., 500 ms wait.
     */
    for (PollCount = 0; PollCount < PollMax; PollCount++) {
        CSTS.AsUlong = StorPortReadRegisterUlong(pAE,
                                           (PULONG)(&pAE->pCtrlRegister->CSTS));

        if (CSTS.RDY == 0)
            return TRUE;
        NVMeStallExecution(pAE, MAX_STATE_STALL_us);
    }

    return FALSE;
} /* NVMeWaitOnReady */

/*******************************************************************************
 * NVMeEnableAdapter
 *
 * @brief NVMeEnableAdapter gets called to do the followings:
 *     - Program AdminQ related registers
 *     - Set EN bit of CC register as 1
 *     - Check if device is ready via RDY bit of STS register
 *     - Report the readiness of the adapter in return
 *
 * @param pAE - Pointer to hardware device extension.
 *
 * @return VOID
 ******************************************************************************/
VOID NVMeEnableAdapter(
    PNVME_DEVICE_EXTENSION pAE
)
{
    PQUEUE_INFO pQI = &pAE->QueueInfo;
    NVMe_CONTROLLER_CONFIGURATION CC = {0};

    /*
     * Program Admin queue registers before enabling the adapter:
     * Admin Queue Attributes
     */
    StorPortWriteRegisterUlong(
        pAE,
        (PULONG)(&pAE->pCtrlRegister->AQA),
        (pQI->pSubQueueInfo->SubQEntries - 1) +
        ((pQI->pCplQueueInfo->CplQEntries - 1) << NVME_AQA_CQS_LSB));

    /* Admin Submission Queue Base Address (64 bit) */
    StorPortWriteRegisterUlong(
        pAE,
        (PULONG)(&pAE->pCtrlRegister->ASQ.LowPart),
        (ULONG)(pQI->pSubQueueInfo->SubQStart.LowPart));

    StorPortWriteRegisterUlong(
        pAE,
        (PULONG)(&pAE->pCtrlRegister->ASQ.HighPart),
        (ULONG)(pQI->pSubQueueInfo->SubQStart.HighPart));

    /* Admin Completion Queue Base Address (64 bit) */
    StorPortWriteRegisterUlong(
        pAE,
        (PULONG)(&pAE->pCtrlRegister->ACQ.LowPart),
        (ULONG)(pQI->pCplQueueInfo->CplQStart.LowPart));

    StorPortWriteRegisterUlong(
        pAE,
        (PULONG)(&pAE->pCtrlRegister->ACQ.HighPart),
        (ULONG)(pQI->pCplQueueInfo->CplQStart.HighPart));

    /*
     * Set up Controller Configuration Register
     */
    CC.EN = 1;
    CC.CSS = NVME_CC_NVM_CMD;
    CC.MPS = (PAGE_SIZE >> NVME_MEM_PAGE_SIZE_SHIFT);
    CC.AMS = NVME_CC_ROUND_ROBIN;
    CC.SHN = NVME_CC_SHUTDOWN_NONE;
    CC.IOSQES = NVME_CC_IOSQES;
    CC.IOCQES = NVME_CC_IOCQES;

    StorPortWriteRegisterUlong(pAE,
                               (PULONG)(&pAE->pCtrlRegister->CC),
                               CC.AsUlong);
} /* NVMeEnableAdapter */

/*******************************************************************************
 * NVMeSetFeaturesCompletion
 *
 * @brief NVMeSetFeaturesCompletion gets called to exam the first LBA Range
 *        entry of a given namespace. The procedure is described below:
 *
 *        Only exam the first LBA Range entry of a given namespace;
 *
 *        if (NLB == Namespace Size) {
 *            if (Type == 00b) {
 *                Change the followings via Set Features (ID#3):
 *                    Type to Filesystem;
 *                    Attributes(bit0) as 1 (can be overwritten);
 *                    Attributes(bit1) as 0 (visible);
 *                Mark ExposeNamespace as TRUE;
 *                Mark ReadOnly as FALSE;
 *            } else if (Type == 01b) {
 *                if (Attributes(bit0) != 0)
 *                    Mark ReadOnly as FALSE;
 *                else
 *                    Mark ReadOnly as TRUE;
 *
 *                if (Attributes(bit1) != 0)
 *                    Mark ExposeNamespace as FALSE;
 *                else
 *                    Mark ExposeNamespace as TRUE;
 *            } else {
 *                Mark ExposeNamespace as FALSE;
 *                Mark ReadOnly based on Attributes(bit0);
 *            }
 *        } else {
 *            Mark ExposeNamespace as FALSE;
 *            Mark ReadOnly based on Attributes(bit0);
 *        }
 *
 * @param pAE - Pointer to hardware device extension.
 * @param pNVMeCmd - Pointer to the original submission entry
 * @param pCplEntry - Pointer to the completion entry
 *
 * @return VOID
 ******************************************************************************/
VOID NVMeSetFeaturesCompletion(
    PNVME_DEVICE_EXTENSION pAE,
    PNVMe_COMMAND pNVMeCmd,
    PNVMe_COMPLETION_QUEUE_ENTRY pCplEntry
)
{
    PQUEUE_INFO pQI = &pAE->QueueInfo;
    PADMIN_SET_FEATURES_COMMAND_LBA_RANGE_TYPE_ENTRY pLbaRangeTypeEntry = NULL;
    PNVME_LUN_EXTENSION pLunExt = NULL;
    PADMIN_SET_FEATURES_COMMAND_DW10 pSetFeaturesCDW10 = NULL;
    PADMIN_SET_FEATURES_COMMAND_LBA_RANGE_TYPE_DW11 pSetFeaturesCDW11 = NULL;
    NS_VISBILITY visibility = IGNORED;
    ULONG lunId;

    /*
     * Mark down the resulted information if succeeded. Otherwise, log the error
     * bit in case of errors and fail the state machine
     */
    pSetFeaturesCDW10 =
        (PADMIN_SET_FEATURES_COMMAND_DW10) &pNVMeCmd->CDW10;

    if (pAE->DriverState.InterruptCoalescingSet == FALSE &&
        pNVMeCmd->CDW0.OPC == ADMIN_SET_FEATURES        &&
        pSetFeaturesCDW10->FID == INTERRUPT_COALESCING ) {
        if (pCplEntry->DW3.SF.SC != 0) {
            NVMeDriverFatalError(pAE,
                                (1 << START_STATE_INT_COALESCING_FAILURE));
        } else {
            pAE->DriverState.InterruptCoalescingSet = TRUE;

            /* Reset the counter and keep tihs state to set more features */
            pAE->DriverState.StateChkCount = 0;
            pAE->DriverState.NextDriverState = NVMeWaitOnSetFeatures;
        }
    } else if (pNVMeCmd->CDW0.OPC == ADMIN_SET_FEATURES &&
               pSetFeaturesCDW10->FID == NUMBER_OF_QUEUES) {
        if (pCplEntry->DW3.SF.SC != 0) {
            NVMeDriverFatalError(pAE,
                                (1 << START_STATE_QUEUE_ALLOC_FAILURE));
        } else {
            /*
             * NCQR and NSQR are 0 based values. NumSubIoQAllocFromAdapter and
             * NumSubIoQAllocFromAdapter are 1 based values.
             */
            pQI->NumSubIoQAllocFromAdapter = GET_WORD_0(pCplEntry->DW0) + 1;
            pQI->NumCplIoQAllocFromAdapter = GET_WORD_1(pCplEntry->DW0) + 1;

            /* Reset the counter and keep tihs state to set more features */
            pAE->DriverState.StateChkCount = 0;
            pAE->DriverState.NextDriverState = NVMeWaitOnSetFeatures;
        }
    } else if ((pAE->DriverState.TtlLbaRangeExamined <
                pAE->DriverState.IdentifyNamespaceFetched) &&
               (pSetFeaturesCDW10->FID == LBA_RANGE_TYPE)) {
        /* first check completion status code */
        if (pCplEntry->DW3.SF.SC != 0) {
            NVMeDriverFatalError(pAE,
                                (1 << START_STATE_LBA_RANGE_CHK_FAILURE));
        } else {
            /*
             * When Get Features command completes, exam the completed data to
             * see if Set Features is required. If so, simply set
             * ConfigLbaRangeNeeded as TRUE If not, simply increase
             * TtlLbaRangeExamined and set ConfigLbaRangeNeeded as FALSE When Set
             * Features command completes, simply increase TtlLbaRangeExamined
             */
            lunId = pAE->DriverState.VisibleNamespacesExamined;

            pLbaRangeTypeEntry =
                (PADMIN_SET_FEATURES_COMMAND_LBA_RANGE_TYPE_ENTRY)
                pAE->DriverState.pDataBuffer;
            pSetFeaturesCDW11 =
                (PADMIN_SET_FEATURES_COMMAND_LBA_RANGE_TYPE_DW11) &pNVMeCmd->CDW11;

            pLunExt = pAE->pLunExtensionTable[lunId];

            if (pNVMeCmd->CDW0.OPC == ADMIN_GET_FEATURES) {
                /* driver only supports 1 LBA range type per NS (NUM is 0 based) */
                if (pSetFeaturesCDW11->NUM == 0) {

                        /*
                     *
                     * NOTE:  spec/group still working this behavior out
                     * wrt dealing with range tpyes so making this simple
                     * for now so we can update it when needed.  Currently
                     * we'll IGNORE the range TYPE entirely
                     *
                         */
                    StorPortDebugPrint(INFO,
                           "pLbaRangeTypeEntry type : 0x%llX lun id %d nsid 0x%x\n",
                           pLbaRangeTypeEntry->Type,
                           lunId,
                           pNVMeCmd->NSID);

                    visibility =
                        pLbaRangeTypeEntry->Attributes.Hidden ? HIDDEN:VISIBLE;
                        pLunExt->ReadOnly =
                            pLbaRangeTypeEntry->Attributes.Overwriteable ?
                                FALSE:TRUE;

                    } else {
                        /*
                     * Don't support more than one entry per NS. Mark it IGNORED
                         */
                    visibility = IGNORED;
                }

                    pAE->DriverState.ConfigLbaRangeNeeded = FALSE;
                pAE->DriverState.TtlLbaRangeExamined++;
                if (visibility == VISIBLE) {
                    pLunExt->slotStatus = ONLINE;
                    pAE->DriverState.VisibleNamespacesExamined++;
                } else {
                    StorPortDebugPrint(INFO,"NVMeSetFeaturesCompletion: FYI LnuExt at %d has been cleared (NSID not visible)\n",
                        lunId);
                    RtlZeroMemory(pLunExt, sizeof(NVME_LUN_EXTENSION));
                }

            } else if (pNVMeCmd->CDW0.OPC == ADMIN_SET_FEATURES) {

                /* TODO: set features not currently called, after its ironed out
                 * how we want to handle range types, we'll need to fill
                 * this in
                 */

                /*
                pAE->DriverState.VisibleNamespacesExamined++;
                pLunExt->ReadOnly = FALSE;
                pAE->DriverState.TtlLbaRangeExamined++;
                pAE->DriverState.ConfigLbaRangeNeeded = FALSE;
                */
            }

            /* Reset the counter and set next state accordingly */
            pAE->DriverState.StateChkCount = 0;
            if (pAE->DriverState.TtlLbaRangeExamined ==
                pAE->controllerIdentifyData.NN) {
                /* We have called identify namespace as well as get/set
                 * features for each of the NN namespaces that exist.
                 * Move on to the next state in the state machine.
                 */
                pAE->visibleLuns = pAE->DriverState.VisibleNamespacesExamined;
                pAE->DriverState.NextDriverState = NVMeWaitOnSetupQueues;
            } else {
                /* We have more namespaces to identify and get/set features
                 * for. But before we can move on to the next namespace,
                 * we'll check if we need to call set features - if not,
                 * we'll set the state to NVMeWaitOnIdentifyNS so we can
                 * continue to identify the remaining namespaces.
                 */
                if (TRUE == pAE->DriverState.ConfigLbaRangeNeeded) {
                    // We still need to issue a set features for this namespace
                pAE->DriverState.NextDriverState = NVMeWaitOnSetFeatures;
                } else {
                    // Move on to the next namespace and issue an identify
                    pAE->DriverState.NextDriverState = NVMeWaitOnIdentifyNS;
                }
            }
        }
    }
} /* NVMeSetFeaturesCompletion */

/*******************************************************************************
 * NVMeDeleteQueueCallback
 *
 * @brief NVMeDeleteQueueCallback is the callback function used to notify
 *        the caller that a queue deletion has completed
 *
 * @param pAE - Pointer to hardware device extension.
 * @param pSrbExtension - Pointer to the completion entry
 *
 * @return BOOLEAN
 *     TRUE - to indicate we completed fine
 ******************************************************************************/
BOOLEAN NVMeDeleteQueueCallback(
    PVOID pNVMeDevExt,
    PVOID pSrbExtension
)
{
    PNVME_DEVICE_EXTENSION pAE = (PNVME_DEVICE_EXTENSION)pNVMeDevExt;
    PNVME_SRB_EXTENSION pSrbExt = (PNVME_SRB_EXTENSION)pSrbExtension;
    PNVMe_COMMAND pNVMeCmd = (PNVMe_COMMAND)(&pSrbExt->nvmeSqeUnit);
    PNVMe_COMPLETION_QUEUE_ENTRY pCplEntry = pSrbExt->pCplEntry;
    PQUEUE_INFO pQI = &pAE->QueueInfo;

    if (pNVMeCmd->CDW0.OPC == ADMIN_DELETE_IO_COMPLETION_QUEUE) {
        if (pCplEntry->DW3.SF.SC == 0) {
            PCPL_QUEUE_INFO pCQI = pQI->pCplQueueInfo + pQI->NumCplIoQCreated;
            pCQI->CurPhaseTag = 0;
            pCQI->CplQHeadPtr = 0;
            pQI->NumCplIoQCreated--;
        } else {
            NVMeDriverFatalError(pAE,
                                (1 << FATAL_CPLQ_DELETE_FAILURE));
        }
    } else if (pNVMeCmd->CDW0.OPC == ADMIN_DELETE_IO_SUBMISSION_QUEUE) {
        if (pCplEntry->DW3.SF.SC == 0) {
            PSUB_QUEUE_INFO pSQI = pQI->pSubQueueInfo + pQI->NumSubIoQCreated;
            pSQI->SubQTailPtr = 0;
            pSQI->SubQHeadPtr = 0;
            pQI->NumSubIoQCreated--;
        } else {
            NVMeDriverFatalError(pAE,
                                (1 << FATAL_SUBQ_DELETE_FAILURE));
        }
    }
    return TRUE;
}

#if defined(CHATHAM2)
void HardCodeChatham2Data(
    PNVME_DEVICE_EXTENSION pAE,
    ULONG structId
)
{

    if (pAE->InitInfo.HardCodeIdData == 0) {
        return;
    }

    if (structId == 0) {
        RtlZeroMemory(&pAE->controllerIdentifyData,
            sizeof(ADMIN_IDENTIFY_CONTROLLER));
        pAE->controllerIdentifyData.VID = 0x8086;
        pAE->controllerIdentifyData.SSVID = 0x2011;
#define CHATHAM2_SERIAL "2012"
        RtlCopyMemory((UINT8*)&pAE->controllerIdentifyData.SN[0],
                      CHATHAM2_SERIAL,
                      strlen(CHATHAM2_SERIAL));
#define CHATHAM2_MN "CHATHAM2"
        RtlCopyMemory((UINT8*)&pAE->controllerIdentifyData.MN[0],
                      CHATHAM2_MN,
                      strlen(CHATHAM2_MN));
#define CHATHAM2_FR "0"
        RtlCopyMemory((UINT8*)&pAE->controllerIdentifyData.FR[0],
                      CHATHAM2_FR,
                      strlen(CHATHAM2_FR));
        pAE->controllerIdentifyData.SSVID = 0x2011;
        pAE->controllerIdentifyData.NN = 1;
        pAE->controllerIdentifyData.RAB = 8;
        pAE->controllerIdentifyData.UAERL = 3;
        pAE->controllerIdentifyData.IEEMAC.IEEE = 0x423;
        pAE->controllerIdentifyData.LPA.SupportsSMART_HealthInformationLogPage = 1;
        pAE->controllerIdentifyData.SQES.RequiredSubmissionQueueEntrySize = 6;
        pAE->controllerIdentifyData.SQES.MaximumSubmissionQueueEntrySize = 6;
        pAE->controllerIdentifyData.CQES.RequiredCompletionQueueEntrySize = 4;
        pAE->controllerIdentifyData.CQES.MaximumCompletionQueueEntrySize = 4;

    } else {
        PADMIN_IDENTIFY_NAMESPACE pIdenNS = NULL;
        ADMIN_IDENTIFY_FORMAT_DATA fData = {0};

        pIdenNS = &pAE->pLunExtensionTable[0]->identifyData;
        RtlZeroMemory(pIdenNS,
            sizeof(ADMIN_IDENTIFY_NAMESPACE));

        if ((pAE->InitInfo.NsSize > 0) &&
            (pAE->InitInfo.NsSize <= ChathamNlb)) {
            pIdenNS->NSZE = pAE->InitInfo.NsSize;
            pIdenNS->NCAP = pAE->InitInfo.NsSize;
            pIdenNS->NUSE = pAE->InitInfo.NsSize;
        } else {
            pIdenNS->NSZE = ChathamNlb;
            pIdenNS->NCAP = ChathamNlb;
            pIdenNS->NUSE = ChathamNlb;
        }

        fData.LBADS = 9;
        pIdenNS->LBAFx[0] = fData;
    }
}
#endif

/*******************************************************************************
 * NVMeInitCallback
 *
 * @brief NVMeInitCallback is the callback function used to notify
 *        Initialization module the completion of the requests, which are
 *        initiated by driver's initialization module itself. In addition to the
 *        NVME_DEVICE_EXTENSION, the completion entry are also passed. After
 *        examining the entry, some resulted information needs to be noted and
 *        error status needs to be reported if there is any.
 *
 * @param pAE - Pointer to hardware device extension.
 * @param pSrbExtension - Pointer to the completion entry
 *
 * @return BOOLEAN
 *     TRUE - If all resources are allocated and initialized properly
 *     FALSE - If anything goes wrong
 ******************************************************************************/
BOOLEAN NVMeInitCallback(
    PVOID pNVMeDevExt,
    PVOID pSrbExtension
)
{
    PNVME_DEVICE_EXTENSION pAE = (PNVME_DEVICE_EXTENSION)pNVMeDevExt;
    PNVME_SRB_EXTENSION pSrbExt = (PNVME_SRB_EXTENSION)pSrbExtension;
    PNVMe_COMMAND pNVMeCmd = (PNVMe_COMMAND)(&pSrbExt->nvmeSqeUnit);
    PNVMe_COMPLETION_QUEUE_ENTRY pCplEntry = pSrbExt->pCplEntry;
    PQUEUE_INFO pQI = &pAE->QueueInfo;

    switch (pAE->DriverState.NextDriverState) {
        case NVMeWaitOnIdentifyCtrl:
            /*
             * Mark down Controller structure is retrieved if succeeded
             * Otherwise, log the error bit in case of errors and fail the state
             * machine.
             */
            if ((pCplEntry->DW3.SF.SC == 0) &&
                (pCplEntry->DW3.SF.SCT == 0)) {
                /* Reset the counter and set next state */
                pAE->DriverState.NextDriverState = NVMeWaitOnIdentifyNS;
                pAE->DriverState.StateChkCount = 0;
#if defined(CHATHAM2)
                HardCodeChatham2Data(pAE,0);
#endif
            } else {
                NVMeDriverFatalError(pAE,
                                    (1 << START_STATE_IDENTIFY_CTRL_FAILURE));
            }
        break;
        case NVMeWaitOnIdentifyNS:
            /*
             * Mark down Namespace structure is retrieved if succeeded
             * Otherwise, log the error bit in case of errors and
             * fail the state machine
             */
            if ((pCplEntry->DW3.SF.SC == 0) &&
                (pCplEntry->DW3.SF.SCT == 0)) {
                PNVME_LUN_EXTENSION pLunExt =
                    pAE->pLunExtensionTable[pAE->DriverState.VisibleNamespacesExamined];

#if defined(CHATHAM2)
                HardCodeChatham2Data(pAE,1);
#endif
                pAE->DriverState.IdentifyNamespaceFetched++;

                /* Reset the counter and set next state */
                pAE->DriverState.StateChkCount = 0;

                /* Move to the next state to set features for this namespace */
                pAE->DriverState.NextDriverState = NVMeWaitOnSetFeatures;

                /* Mark down the Namespace ID */
                pLunExt->namespaceId =
                    pAE->DriverState.IdentifyNamespaceFetched;

               /* for use in the LBA range type commands we need this info */
               pAE->DriverState.CurrentNsid = pLunExt->namespaceId;

             } else {
                NVMeDriverFatalError(pAE,
                                    (1 << START_STATE_IDENTIFY_NS_FAILURE));
            }
        break;
        case NVMeWaitOnSetFeatures:
            NVMeSetFeaturesCompletion(pAE, pNVMeCmd, pCplEntry);
        break;
        case NVMeWaitOnAER:
            if ((pCplEntry->DW3.SF.SC == 0) &&
                (pCplEntry->DW3.SF.SCT == 0)) {
                /* Reset the counter and set next state */
                pAE->DriverState.NextDriverState = NVMeWaitOnIoCQ;
                pAE->DriverState.StateChkCount = 0;
            } else {
                NVMeDriverFatalError(pAE,
                                    (1 << START_STATE_AER_FAILURE));
            }
        break;
        case NVMeWaitOnIoCQ:
            /*
             * Mark down the number of created completion queues if succeeded
             * Otherwise, log the error bit in case of errors and
             * fail the state machine
             */

#if defined(CHATHAM2)
                pCplEntry->DW3.SF.SC = 0;
                pCplEntry->DW3.SF.SCT = 0;
#endif
            if ((pCplEntry->DW3.SF.SC == 0) &&
                (pCplEntry->DW3.SF.SCT == 0)) {
                pQI->NumCplIoQCreated++;

                /* Reset the counter and set next state */
                pAE->DriverState.StateChkCount = 0;
                if (pQI->NumCplIoQAllocated == pQI->NumCplIoQCreated) {
                    pAE->DriverState.NextDriverState = NVMeWaitOnIoSQ;
                } else {
                    pAE->DriverState.NextDriverState = NVMeWaitOnIoCQ;
                }
            } else {
                NVMeDriverFatalError(pAE,
                                    (1 << START_STATE_CPLQ_CREATE_FAILURE));
            }
        break;
        case NVMeWaitOnIoSQ:
            /*
             * Mark down the number of created submission queues if succeeded
             * Otherwise, log the error bit in case of errors and
             * fail the state machine
             */
            if ((pCplEntry->DW3.SF.SC == 0) &&
                (pCplEntry->DW3.SF.SCT == 0)) {
                PRES_MAPPING_TBL pRMT = &pAE->ResMapTbl;
                pQI->NumSubIoQCreated++;

                /* Reset the counter and set next state */
                pAE->DriverState.StateChkCount = 0;
                if (pQI->NumSubIoQAllocated == pQI->NumSubIoQCreated) {
                    /* if we've learned the cores we're done */
                    if (pAE->LearningCores < pRMT->NumActiveCores) {
                        pAE->DriverState.NextDriverState = NVMeWaitOnLearnMapping;
                    } else {
                        pAE->DriverState.NextDriverState = NVMeStartComplete;
                    }
                } else {
                    pAE->DriverState.NextDriverState = NVMeWaitOnIoSQ;
                }
            } else {
                NVMeDriverFatalError(pAE,
                                    (1 << START_STATE_SUBQ_CREATE_FAILURE));
            }
        break;
        case NVMeWaitOnLearnMapping:
            if ((pCplEntry->DW3.SF.SC == 0) &&
                (pCplEntry->DW3.SF.SCT == 0)) {
                PRES_MAPPING_TBL pRMT = &pAE->ResMapTbl;

                /*
                 * see if we still have more core/vector mapping to learn
                 * or if we're ready to redo the queues based on the new map
                 */
                pAE->DriverState.StateChkCount = 0;
                if (pAE->LearningCores < pRMT->NumActiveCores) {
                    pAE->DriverState.NextDriverState = NVMeWaitOnLearnMapping;
                } else {
                    pAE->DriverState.NextDriverState = NVMeWaitOnReSetupQueues;
                }
            } else {
                PRES_MAPPING_TBL pRMT = &pAE->ResMapTbl;

                /* possibly no NS exists at all, either way this isn't fatal */
                StorPortDebugPrint(INFO,
                    "NVMeInitCallback: WARNING: no learning possible, SC 0x% SCT 0x%x\n",
                     pCplEntry->DW3.SF.SC, pCplEntry->DW3.SF.SCT);
                pAE->LearningCores = pRMT->NumActiveCores;
                pAE->DriverState.NextDriverState = NVMeStartComplete;
            }
        break;
        case NVMeWaitOnReSetupQueues:
            if (TRUE == NVMeDeleteQueueCallback(pAE, pSrbExt)) {
                /* if we've deleted the last CQ, we rae ready to recreate them */
                if (0 == pQI->NumCplIoQCreated) {
                    pAE->DriverState.NextDriverState = NVMeWaitOnIoCQ;
                } else {
                    pAE->DriverState.NextDriverState = NVMeWaitOnReSetupQueues;
                }
            }
        break;
        default:
            NVMeDriverFatalError(pAE,
                                (1 << START_STATE_UNKNOWN_STATE_FAILURE));
        break;
    } /* end switch */

    NVMeCallArbiter(pAE);

    return (TRUE);
} /* NVMeInitCallback */

/*******************************************************************************
 * NVMePreparePRPs
 *
 * @brief NVMePreparePRPs is a helper routine to prepare at most 2 PRP entries
 *        for initialization routines.
 *
 * @param pAE - Pointer to hardware device extension.
 * @param pSubEntry - Pointer to Submission entry
 * @param pBuffer - Pointer to the transferring buffer.
 *
 * @return BOOLEAN
 *     TRUE - If the issued command completed without any errors
 *     FALSE - If anything goes wrong
 ******************************************************************************/
BOOLEAN NVMePreparePRPs(
    PNVME_DEVICE_EXTENSION pAE,
    PNVMe_COMMAND pSubEntry,
    PVOID pBuffer,
    ULONG TxLength
)
{
    STOR_PHYSICAL_ADDRESS PhyAddr;
    ULONG_PTR PtrTemp = 0;
    ULONG RoomInFirstPage = 0;

    /* Go ahead and prepare 1st PRP entries, need at least one PRP entry */
    PhyAddr = NVMeGetPhysAddr(pAE, pBuffer);
    if (PhyAddr.QuadPart == 0)
        return (FALSE);

    pSubEntry->PRP1 = PhyAddr.QuadPart;

    /*
     * Find out how much room still available in current page.
     * If it's enough, only PRP1 is needed. Otherwise, use PRP2 as well.
     */
    RoomInFirstPage = PAGE_SIZE - (PhyAddr.QuadPart & (PAGE_SIZE - 1));
    if ( RoomInFirstPage >= TxLength )
        return (TRUE);

    PtrTemp = (ULONG_PTR)((PUCHAR)pBuffer);
    if (IS_SYS_PAGE_ALIGNED(PtrTemp) != TRUE) {
        /* Need 2 decide if 2nd PRP entry is required */
        PtrTemp = PAGE_ALIGN_BUF_ADDR(PtrTemp);
        PhyAddr = NVMeGetPhysAddr(pAE, (PVOID)PtrTemp);

        if (PhyAddr.QuadPart == 0)
            return (FALSE);

        pSubEntry->PRP2 = PhyAddr.QuadPart;
    }

    return (TRUE);
}

/*******************************************************************************
 * NVMeSetIntCoalescing
 *
 * @brief NVMeSetIntCoalescing gets called to configure interrupt coalescing
 *        with the values fetched from Registry via Set Features command with
 *        Feature ID#8.
 *
 * @param pAE - Pointer to hardware device extension.
 *
 * @return BOOLEAN
 *     TRUE - If the issued command completed without any errors
 *     FALSE - If anything goes wrong
 ******************************************************************************/
BOOLEAN NVMeSetIntCoalescing(
    PNVME_DEVICE_EXTENSION pAE
)
{
    PNVME_SRB_EXTENSION pNVMeSrbExt =
        (PNVME_SRB_EXTENSION)pAE->DriverState.pSrbExt;
    PNVMe_COMMAND pSetFeatures = (PNVMe_COMMAND)(&pNVMeSrbExt->nvmeSqeUnit);

    PADMIN_SET_FEATURES_COMMAND_DW10 pSetFeaturesCDW10 = NULL;
    PADMIN_SET_FEATURES_COMMAND_INTERRUPT_COALESCING_DW11
        pSetFeaturesCDW11 = NULL;

    /* Zero out the extension first */
    memset((PVOID)pNVMeSrbExt, 0, sizeof(NVME_SRB_EXTENSION));

    /* Populate SRB_EXTENSION fields */
    pNVMeSrbExt->pNvmeDevExt = pAE;
    pNVMeSrbExt->pNvmeCompletionRoutine = NVMeInitCallback;

    /* Populate submission entry fields */
    pSetFeatures->CDW0.OPC = ADMIN_SET_FEATURES;
    pSetFeaturesCDW10 = (PADMIN_SET_FEATURES_COMMAND_DW10) &pSetFeatures->CDW10;
    pSetFeaturesCDW11 = (PADMIN_SET_FEATURES_COMMAND_INTERRUPT_COALESCING_DW11)
        &pSetFeatures->CDW11;

    pSetFeaturesCDW10->FID = INTERRUPT_COALESCING;

    /* Set up the Aggregation Time and Threshold */
    pSetFeaturesCDW11->TIME = pAE->InitInfo.IntCoalescingTime;
    pSetFeaturesCDW11->THR = pAE->InitInfo.IntCoalescingEntry;

    /* Now issue the command via Admin Doorbell register */
    return ProcessIo(pAE, pNVMeSrbExt, NVME_QUEUE_TYPE_ADMIN, FALSE);
} /* NVMeSetIntCoalescing */

/*******************************************************************************
 * NVMeAllocQueueFromAdapter
 *
 * @brief NVMeAllocQueueFromAdapter gets called to allocate
 *        submission/completion queues from the adapter via Set Feature command
 *        with Feature ID#7. This routine requests the same number of
 *        submission/completion queues as the number of current active cores in
 *        the system.
 *
 * @param pAE - Pointer to hardware device extension.
 *
 * @return BOOLEAN
 *     TRUE - If the issued command completed without any errors
 *     FALSE - If anything goes wrong
 ******************************************************************************/
BOOLEAN NVMeAllocQueueFromAdapter(
    PNVME_DEVICE_EXTENSION pAE
)
{
    PNVME_SRB_EXTENSION pNVMeSrbExt =
        (PNVME_SRB_EXTENSION)pAE->DriverState.pSrbExt;
    PNVMe_COMMAND pSetFeatures = (PNVMe_COMMAND)(&pNVMeSrbExt->nvmeSqeUnit);
    PADMIN_SET_FEATURES_COMMAND_DW10 pSetFeaturesCDW10 = NULL;
    PADMIN_SET_FEATURES_COMMAND_NUMBER_OF_QUEUES_DW11 pSetFeaturesCDW11 = NULL;

    /* Zero out the extension first */
    memset((PVOID)pNVMeSrbExt, 0, sizeof(NVME_SRB_EXTENSION));

    /* Populate SRB_EXTENSION fields */
    pNVMeSrbExt->pNvmeDevExt = pAE;
    pNVMeSrbExt->pNvmeCompletionRoutine = NVMeInitCallback;

    /* Populate submission entry fields */
    pSetFeatures->CDW0.OPC = ADMIN_SET_FEATURES;
    pSetFeaturesCDW10 = (PADMIN_SET_FEATURES_COMMAND_DW10) &pSetFeatures->CDW10;
    pSetFeaturesCDW11 = (PADMIN_SET_FEATURES_COMMAND_NUMBER_OF_QUEUES_DW11)
        &pSetFeatures->CDW11;
    pSetFeaturesCDW10->FID = NUMBER_OF_QUEUES;

    /* Set up NCSQR and NSQR, which are 0-based. */
    if (pAE->ntldrDump == TRUE) {
        /* In crashdump/hibernation case, only 1 pair of IO queue needed */
        pSetFeaturesCDW11->NCQR = 0;
        pSetFeaturesCDW11->NSQR = 0;
    } else {
        /* Try to allocate same number of queue pairs as active cores */
        pSetFeaturesCDW11->NCQR = pAE->ResMapTbl.NumActiveCores - 1;
        pSetFeaturesCDW11->NSQR = pAE->ResMapTbl.NumActiveCores - 1;
    }

    /* Now issue the command via Admin Doorbell register */
    return ProcessIo(pAE, pNVMeSrbExt, NVME_QUEUE_TYPE_ADMIN, FALSE);
} /* NVMeAllocQueueFromAdapter */

/*******************************************************************************
 * NVMeAccessLbaRangeEntry
 *
 * @brief NVMeAccessLbaRangeEntry gets called to query/update the first LBA
 *        Range entry of a given namespace. The procedure is described below:
 *
 *        Depending on the value of ConfigLbaRangeNeeded, if TRUE, it issues
 *        Set Features command. Otherwise, it issues Get Features commands.
 *
 *        When issuing Set Features, the namespace has:
 *          Type = Filesystem,
 *          ReadOnly = FALSE,
 *          Visible = TRUE.
 *
 *        When command completes, NVMeSetFeaturesCompletion is called to exam
 *        the LBA Range Type entries and wrap up the access.
 *
 * @param pAE - Pointer to hardware device extension.
 *
 * @return BOOLEAN
 *     TRUE - If the issued command completed without any errors
 *     FALSE - If anything goes wrong
 ******************************************************************************/
BOOLEAN NVMeAccessLbaRangeEntry(
    PNVME_DEVICE_EXTENSION pAE
)
{
    PNVME_SRB_EXTENSION pNVMeSrbExt =
        (PNVME_SRB_EXTENSION)pAE->DriverState.pSrbExt;
    PNVMe_COMMAND pSetFeatures = (PNVMe_COMMAND)(&pNVMeSrbExt->nvmeSqeUnit);
    PADMIN_SET_FEATURES_COMMAND_DW10 pSetFeaturesCDW10 = NULL;
    PADMIN_SET_FEATURES_COMMAND_LBA_RANGE_TYPE_ENTRY pLbaRangeEntry = NULL;
    ULONG NSID = pAE->DriverState.CurrentNsid;
    BOOLEAN Query = pAE->DriverState.ConfigLbaRangeNeeded;

    /* Fail here if Namespace ID is 0 or out of range */
    if (NSID == 0 || NSID > pAE->controllerIdentifyData.NN)
        return FALSE;

    /* Zero out the extension first */
    memset((PVOID)pNVMeSrbExt, 0, sizeof(NVME_SRB_EXTENSION));

    /* Populate SRB_EXTENSION fields */
    pNVMeSrbExt->pNvmeDevExt = pAE;
    pNVMeSrbExt->pNvmeCompletionRoutine = NVMeInitCallback;

    /* Populate submission entry fields */
    pSetFeatures->NSID = NSID;

    /*
     * Prepare the buffer for transferring LBA Range entries
     * Need to zero the buffer out first when retrieving the entries
     */
    if (pAE->DriverState.ConfigLbaRangeNeeded == TRUE) {
        pSetFeatures->CDW0.OPC = ADMIN_SET_FEATURES;

        /* Prepare new first LBA Range entry */
        pLbaRangeEntry = (PADMIN_SET_FEATURES_COMMAND_LBA_RANGE_TYPE_ENTRY)
            pAE->DriverState.pDataBuffer;
        pLbaRangeEntry->Type = LBA_TYPE_FILESYSTEM;
        pLbaRangeEntry->Attributes.Overwriteable = 1;
        pLbaRangeEntry->Attributes.Hidden = 0;
        pLbaRangeEntry->NLB = pAE->pLunExtensionTable[NSID-1]->identifyData.NSZE;
    } else {
        pSetFeatures->CDW0.OPC = ADMIN_GET_FEATURES;
        memset(pAE->DriverState.pDataBuffer, 0, PAGE_SIZE);
    }

    /* Prepare PRP entries, need at least one PRP entry */
    if (NVMePreparePRPs(pAE,
                        pSetFeatures,
                        pAE->DriverState.pDataBuffer,
                        sizeof(ADMIN_SET_FEATURES_LBA_COMMAND_RANGE_TYPE_ENTRY))
                        == FALSE) {
        return (FALSE);
    }

    pSetFeaturesCDW10 = (PADMIN_SET_FEATURES_COMMAND_DW10) &pSetFeatures->CDW10;
    pSetFeaturesCDW10->FID = LBA_RANGE_TYPE;

    /* Now issue the command via Admin Doorbell register */
    return ProcessIo(pAE, pNVMeSrbExt, NVME_QUEUE_TYPE_ADMIN, FALSE);
} /* NVMeAccessLbaRangeEntry */

/*******************************************************************************
 * NVMeGetIdentifyStructures
 *
 * @brief NVMeGetIdentifyStructures gets called to retrieve Identify structures,
 *        including the Controller and Namespace information depending on
 *        NamespaceID.
 *
 * @param pAE - Pointer to hardware device extension.
 * @param NamespaceID - Specify either Controller or Namespace structure to
 *                      retrieve
 *
 * @return BOOLEAN
 *     TRUE - If the issued command completed without any errors
 *     FALSE - If anything goes wrong
 ******************************************************************************/
BOOLEAN NVMeGetIdentifyStructures(
    PNVME_DEVICE_EXTENSION pAE,
    ULONG NamespaceID
)
{
    PNVME_SRB_EXTENSION pNVMeSrbExt =
        (PNVME_SRB_EXTENSION)pAE->DriverState.pSrbExt;
    PNVMe_COMMAND pIdentify = NULL;
    PADMIN_IDENTIFY_CONTROLLER pIdenCtrl = &pAE->controllerIdentifyData;
    PADMIN_IDENTIFY_NAMESPACE pIdenNS = NULL;
    PADMIN_IDENTIFY_COMMAND_DW10 pIdentifyCDW10 = NULL;

    /* Zero-out the entire SRB_EXTENSION */
    memset((PVOID)pNVMeSrbExt, 0, sizeof(NVME_SRB_EXTENSION));

    /* Populate SRB_EXTENSION fields */
    pNVMeSrbExt->pNvmeDevExt = pAE;
    pNVMeSrbExt->pNvmeCompletionRoutine = NVMeInitCallback;

    /* Populate submission entry fields */
    pIdentify = &pNVMeSrbExt->nvmeSqeUnit;
    pIdentify->CDW0.OPC = ADMIN_IDENTIFY;

    if (NamespaceID == IDEN_CONTROLLER) {
        /* Indicate it's for Controller structure */
        pIdentifyCDW10 = (PADMIN_IDENTIFY_COMMAND_DW10) &pIdentify->CDW10;
        pIdentifyCDW10->CNS = 1;

        /* Prepare PRP entries, need at least one PRP entry */
        if (NVMePreparePRPs(pAE,
                            pIdentify,
                            (PVOID)pIdenCtrl,
                            sizeof(ADMIN_IDENTIFY_CONTROLLER)) == FALSE) {
            return (FALSE);
        }
    } else {
        ULONG lunId;

        if ( pIdenCtrl == NULL )
            return (FALSE);

        /* Indicate it's for Namespace structure */
        pIdentifyCDW10 = (PADMIN_IDENTIFY_COMMAND_DW10) &pIdentify->CDW10;
        pIdentifyCDW10->CNS = 0;

        /* NN of Controller structure is 1-based */
        if (NamespaceID <= pIdenCtrl->NN) {
            lunId = pAE->DriverState.VisibleNamespacesExamined;

            /* Assign the destination buffer for retrieved structure.
             * We're storing it in lunExt->identifyData irrespective of
             * whether this namespace is exposed or not. If we later determine
             * (in Set Features) that this namespace is hidden, we just reuse
             * the same buffer for future identifys, thus overwriting it.
             */

            pIdenNS = &pAE->pLunExtensionTable[lunId]->identifyData;

            /* Namespace ID is 1-based. */
            pIdentify->NSID = NamespaceID;

            StorPortDebugPrint(INFO,
                "NVMeGetIdentifyStructures: Get NS INFO for NSID 0x%x tgt lun 0x%x\n",
                    NamespaceID, lunId);

            /* Prepare PRP entries, need at least one PRP entry */
            if (NVMePreparePRPs(pAE,
                                pIdentify,
                                (PVOID)pIdenNS,
                                sizeof(ADMIN_IDENTIFY_NAMESPACE)) == FALSE) {
                return (FALSE);
            }
        } else {
            /* no initial namespaces defined */
            StorPortDebugPrint(INFO,
                "NVMeGetIdentifyStructures: NamespaceID <= pIdenCtrl->NN\n");
            pAE->DriverState.StateChkCount = 0;
            pAE->visibleLuns = 0;
            pAE->DriverState.NextDriverState = NVMeWaitOnSetupQueues;
            NVMeCallArbiter(pAE);
            return (TRUE);
        }
    }

    /* Now issue the command via Admin Doorbell register */
    return ProcessIo(pAE, pNVMeSrbExt, NVME_QUEUE_TYPE_ADMIN, FALSE);
} /* NVMeGetIdentifyStructures */

/*******************************************************************************
 * NVMeIssueAERs
 *
 * @brief NVMeIssueAERs gets called to issue a number of Asynchronous Event
 *        Request commands. The number of commands to issue is determined by
 *        UAERL of ADMIN_IDENTIFY_CONTROLLER structure. If the number plus the
 *        already issued ones exceeds UAERL, the less number of AER commands
 *        are issued. The number of issued commands is returned.
 *
 * @param pAE - Pointer to hardware device extension.
 * @param NumCmds - Number of AERs to issue.
 *
 * @return UCHAR
 *     Actual number of AER commands issued in this invoke
 ******************************************************************************/
UCHAR NVMeIssueAERs(
    PNVME_DEVICE_EXTENSION pAE,
    UCHAR NumCmds
)
{
    PNVME_SRB_EXTENSION pNVMeSrbExt = NULL;
    PNVMe_COMMAND pAER = NULL;
    UCHAR NumCmdToIssue = NumCmds + pAE->DriverState.NumAERsIssued;
    UCHAR CmdIssued = 0;

    /*
     * Determine how many commands to issue
     * The AER limit indicated in Controller structure is 0-based.
     */
    if (NumCmdToIssue > pAE->controllerIdentifyData.UAERL + 1) {
        NumCmdToIssue = pAE->controllerIdentifyData.UAERL + 1 -
                        pAE->DriverState.NumAERsIssued;
    } else {
        NumCmdToIssue = NumCmds;
    }

    /* If over the limit, just return 0 now */
    if (NumCmdToIssue == 0)
        return (NumCmdToIssue);

    for ( ; NumCmdToIssue > 0; NumCmdToIssue--) {
        /*
         * Need to allocate buffer for each SRB Extension here.
         * When any of AER completes, the buffer needs to be freed
         * If the allocation fails, simply return the number of command issued
         */
        pNVMeSrbExt = (PNVME_SRB_EXTENSION)NVMeAllocatePool(pAE,
                                           sizeof(NVME_SRB_EXTENSION));

        if (pNVMeSrbExt == NULL)
            return (CmdIssued);

        /* Populate SRB_EXTENSION fields */
        pNVMeSrbExt->pNvmeDevExt = pAE;
        pNVMeSrbExt->pNvmeCompletionRoutine = NVMeAERCompletion;

        /* Populate submission entry fields */
        pAER = (PNVMe_COMMAND)(&pNVMeSrbExt->nvmeSqeUnit);
        pAER->CDW0.OPC = ADMIN_ASYNCHRONOUS_EVENT_REQUEST;

        /*
         * Now issue the command via Admin Doorbell register
         * If fails, just return how many AERs are issued
         */
        if (ProcessIo(pAE, pNVMeSrbExt, NVME_QUEUE_TYPE_ADMIN, FALSE) == FALSE)
            return (CmdIssued);

        CmdIssued++;
        pAE->DriverState.NumAERsIssued++;
    }

    return (CmdIssued);
} /* NVMeIssueAERs */

/*******************************************************************************
 * NVMeCreateCplQueue
 *
 * @brief NVMeCreateCplQueue gets called to create one IO completion queue at a
 *        time via issuing Create IO Completion Queue command. The smaller value
 *        of NumCplIoQAllocated of QUEUE_INFO and NumActiveCores of
 *        RES_MAPPING_TABLE decides the number of completion queues to create,
 *        which is indicated in NumCplIoQAllocated of QUEUE_INFO.
 *
 * @param pAE - Pointer to hardware device extension.
 * @param QueueID - Which completion queue to create.
 *
 * @return BOOLEAN
 *     TRUE - If the issued commands completed without any errors
 *     FALSE - If anything goes wrong
 ******************************************************************************/
BOOLEAN NVMeCreateCplQueue(
    PNVME_DEVICE_EXTENSION pAE,
    USHORT QueueID
)
{
    PNVME_SRB_EXTENSION pNVMeSrbExt =
        (PNVME_SRB_EXTENSION)pAE->DriverState.pSrbExt;
    PQUEUE_INFO pQI = &pAE->QueueInfo;
    PNVMe_COMMAND pCreateCpl = NULL;
    PADMIN_CREATE_IO_COMPLETION_QUEUE_DW10 pCreateCplCDW10 = NULL;
    PADMIN_CREATE_IO_COMPLETION_QUEUE_DW11 pCreateCplCDW11 = NULL;
    PCPL_QUEUE_INFO pCQI = NULL;

    if (QueueID != 0 && QueueID <= pQI->NumCplIoQAllocated) {
        /* Zero-out the entire SRB_EXTENSION */
        memset((PVOID)pNVMeSrbExt, 0, sizeof(NVME_SRB_EXTENSION));

        /* Populate SRB_EXTENSION fields */
        pNVMeSrbExt->pNvmeDevExt = pAE;
        pNVMeSrbExt->pNvmeCompletionRoutine = NVMeInitCallback;

        /* Populate submission entry fields */
        pCreateCpl = (PNVMe_COMMAND)(&pNVMeSrbExt->nvmeSqeUnit);
        pCreateCplCDW10 = (PADMIN_CREATE_IO_COMPLETION_QUEUE_DW10)
                          (&pCreateCpl->CDW10);
        pCreateCplCDW11 = (PADMIN_CREATE_IO_COMPLETION_QUEUE_DW11)
                          (&pCreateCpl->CDW11);

        /* Populate submission entry fields */
        pCreateCpl->CDW0.OPC = ADMIN_CREATE_IO_COMPLETION_QUEUE;
        pCQI = pQI->pCplQueueInfo + QueueID;
        pCreateCpl->PRP1 = pCQI->CplQStart.QuadPart;
        pCreateCplCDW10->QID = QueueID;
        pCreateCplCDW10->QSIZE = pQI->NumIoQEntriesAllocated - 1;
        pCreateCplCDW11->PC = 1;
        pCreateCplCDW11->IEN = 1;
        pCreateCplCDW11->IV = pCQI->MsiMsgID;

#ifdef CHATHAM2
        if (pAE->ResMapTbl.NumMsiMsgGranted == 0) {
            pCreateCplCDW11->IEN = 0;
            pCreateCplCDW11->IV = 0;
        }
#endif
        /* Now issue the command via Admin Doorbell register */
        return ProcessIo(pAE, pNVMeSrbExt, NVME_QUEUE_TYPE_ADMIN, FALSE);
    }

    return (FALSE);
} /* NVMeCreateCplQueue */

/*******************************************************************************
 * NVMeCreateSubQueue
 *
 * @brief NVMeCreateSubQueue gets called to create one IO submission queue at a
 *        time via issuing Create IO Submission Queue command. The smaller value
 *        of NumSubIoQAllocated of QUEUE_INFO and NumActiveCores of
 *        RES_MAPPING_TABLE decides the number of submission queues to create,
 *        which is indicated in NumSubIoQAllocated of QUEUE_INFO.
 *
 * @param pAE - Pointer to hardware device extension.
 * @param QueueID - Which submission queue to create.
 *
 * @return BOOLEAN
 *     TRUE - If the issued commands completed without any errors
 *     FALSE - If anything goes wrong
 ******************************************************************************/
BOOLEAN NVMeCreateSubQueue(
    PNVME_DEVICE_EXTENSION pAE,
    USHORT QueueID
)
{
    PNVME_SRB_EXTENSION pNVMeSrbExt =
        (PNVME_SRB_EXTENSION)pAE->DriverState.pSrbExt;
    PQUEUE_INFO pQI = &pAE->QueueInfo;
    PNVMe_COMMAND pCreateSub = NULL;
    PADMIN_CREATE_IO_SUBMISSION_QUEUE_DW10 pCreateSubCDW10 = NULL;
    PADMIN_CREATE_IO_SUBMISSION_QUEUE_DW11 pCreateSubCDW11 = NULL;
    PSUB_QUEUE_INFO pSQI = NULL;

    if (QueueID != 0 && QueueID <= pQI->NumCplIoQAllocated) {
        /* Zero-out the entire SRB_EXTENSION */
        memset((PVOID)pNVMeSrbExt, 0, sizeof(NVME_SRB_EXTENSION));

        /* Populate SRB_EXTENSION fields */
        pNVMeSrbExt->pNvmeDevExt = pAE;
        pNVMeSrbExt->pNvmeCompletionRoutine = NVMeInitCallback;

        /* Populate submission entry fields */
        pCreateSub = (PNVMe_COMMAND)(&pNVMeSrbExt->nvmeSqeUnit);
        pCreateSubCDW10 = (PADMIN_CREATE_IO_SUBMISSION_QUEUE_DW10)
                          (&pCreateSub->CDW10);
        pCreateSubCDW11 = (PADMIN_CREATE_IO_SUBMISSION_QUEUE_DW11)
                          (&pCreateSub->CDW11);

        /* Populate submission entry fields */
        pCreateSub->CDW0.OPC = ADMIN_CREATE_IO_SUBMISSION_QUEUE;
        pSQI = pQI->pSubQueueInfo + QueueID;
        pCreateSub->PRP1 = pSQI->SubQStart.QuadPart;
        pCreateSubCDW10->QID = QueueID;
        pCreateSubCDW10->QSIZE = pQI->NumIoQEntriesAllocated - 1;
        pCreateSubCDW11->CQID = pSQI->CplQueueID;
        pCreateSubCDW11->PC = 1;

        /* Now issue the command via Admin Doorbell register */
        return ProcessIo(pAE, pNVMeSrbExt, NVME_QUEUE_TYPE_ADMIN, FALSE);
    }

    return (FALSE);
} /* NVMeCreateSubQueue */

/*******************************************************************************
 * NVMeDeleteCplQueues
 *
 * @brief NVMeDeleteCplQueues gets called to delete a number of IO completion
 *        queues via issuing Delete IO Completion Queue commands.
 *        For us exclusively by the init state machine
 *
 * @param pAE - Pointer to hardware device extension.
 *
 * @return BOOLEAN
 *     TRUE - If the issued commands completed without any errors
 *     FALSE - If anything goes wrong
 ******************************************************************************/
BOOLEAN NVMeDeleteCplQueues(
    PNVME_DEVICE_EXTENSION pAE
)
{
    PNVME_SRB_EXTENSION pNVMeSrbExt =
        (PNVME_SRB_EXTENSION)pAE->DriverState.pSrbExt;
    PQUEUE_INFO pQI = &pAE->QueueInfo;
    PNVMe_COMMAND pDeleteCpl = NULL;
    PADMIN_DELETE_IO_COMPLETION_QUEUE_DW10 pDeleteCplCDW10 = NULL;
    USHORT QueueID = (USHORT)pQI->NumCplIoQCreated;

    if (QueueID > 0) {

        /* init and setup srb ext */
        NVMeInitSrbExtension(pNVMeSrbExt, pAE, NULL);
        pNVMeSrbExt->pNvmeCompletionRoutine = NVMeInitCallback;

        /* Populate submission entry fields */
        pDeleteCpl = (PNVMe_COMMAND)(&pNVMeSrbExt->nvmeSqeUnit);
        pDeleteCplCDW10 = (PADMIN_DELETE_IO_COMPLETION_QUEUE_DW10)
                          (&pDeleteCpl->CDW10);
        pDeleteCpl->CDW0.OPC = ADMIN_DELETE_IO_COMPLETION_QUEUE;
        pDeleteCplCDW10->QID = QueueID;

        /* Now issue the command via Admin Doorbell register */
        if (ProcessIo(pAE, pNVMeSrbExt, NVME_QUEUE_TYPE_ADMIN, FALSE) == FALSE) {
            return (FALSE);
        }
    }

    return (TRUE);
} /* NVMeDeleteCplQueues */

/*******************************************************************************
 * NVMeDeleteSubQueues
 *
 * @brief NVMeDeleteSubQueues gets called to delete a number of IO submission
 *        queues via issuing Delete IO Submission Queue commands.
 *        For us exclusively by the init state machine
 *
 * @param pAE - Pointer to hardware device extension.
 *
 * @return BOOLEAN
 *     TRUE - If the issued commands completed without any errors
 *     FALSE - If anything goes wrong
 ******************************************************************************/
BOOLEAN NVMeDeleteSubQueues(
    PNVME_DEVICE_EXTENSION pAE
)
{
    PNVME_SRB_EXTENSION pNVMeSrbExt =
        (PNVME_SRB_EXTENSION)pAE->DriverState.pSrbExt;
    PQUEUE_INFO pQI = &pAE->QueueInfo;
    PNVMe_COMMAND pDeleteSub = NULL;
    PADMIN_DELETE_IO_SUBMISSION_QUEUE_DW10 pDeleteSubCDW10 = NULL;
    USHORT QueueID = (USHORT)pQI->NumSubIoQCreated;

    if (QueueID > 0) {

        /* init and setup srb ext */
        NVMeInitSrbExtension(pNVMeSrbExt, pAE, NULL);
        pNVMeSrbExt->pNvmeCompletionRoutine = NVMeInitCallback;

        /* Populate submission entry fields */
        pDeleteSub = (PNVMe_COMMAND)(&pNVMeSrbExt->nvmeSqeUnit);
        pDeleteSubCDW10 = (PADMIN_DELETE_IO_SUBMISSION_QUEUE_DW10)
                          (&pDeleteSub->CDW10);
        pDeleteSub->CDW0.OPC = ADMIN_DELETE_IO_SUBMISSION_QUEUE;
        pDeleteSubCDW10->QID = QueueID;

        /* Now issue the command via Admin Doorbell register */
        if (ProcessIo(pAE, pNVMeSrbExt, NVME_QUEUE_TYPE_ADMIN, FALSE) == FALSE) {
            return (FALSE);
        }
    }
    return (TRUE);
} /* NVMeDeleteSubQueues */

/*******************************************************************************
 * NVMeNormalShutdown
 *
 * @brief NVMeNormalShutdown gets called to follow the normal shutdown sequence
 *        defined in NVMe specification when receiving SRB_FUNCTION_SHUTDOWN
 *        from the host:
 *
 *        1. Set state to NVMeShutdown to prevent taking any new requests
 *        2. Delete all created submisison queues
 *        3. If succeeded, delete all created completion queues (reset device)
 *        4. Set SHN to normal shutdown (01b)
 *        5. Keep reading back CSTS for 100 ms and check if SHST is 01b.
 *        6. If so or timeout, return SRB_STATUS_SUCCESS to Storport
 *
 * @param pAE - Pointer to hardware device extension.
 *
 * @return BOOLEAN
 *     TRUE - If the issued commands completed without any errors
 *     FALSE - If anything goes wrong
 ******************************************************************************/
BOOLEAN NVMeNormalShutdown(
    PNVME_DEVICE_EXTENSION pAE
)
{
    NVMe_CONTROLLER_CONFIGURATION CC;
    NVMe_CONTROLLER_STATUS CSTS;
    ULONG PollMax = pAE->uSecCrtlTimeout / MAX_STATE_STALL_us;
    ULONG PollCount;

    /* Check for any pending cmds. */
    if (NVMeDetectPendingCmds(pAE) == TRUE) {
        return FALSE;
    }

    /* Delete all queues */
    NVMeResetAdapter(pAE);

    /* Need to to ensure the Controller registers are memory-mapped properly */
    if ( pAE->pCtrlRegister == FALSE ) {
        return (FALSE);
    }

    /* Ensure all queues are deleted after the controller reset */
    for (PollCount = 0; PollCount < PollMax; PollCount++) {
        CSTS.AsUlong = StorPortReadRegisterUlong(pAE,
                           (PULONG)(&pAE->pCtrlRegister->CSTS.AsUlong));

        if (CSTS.RDY == 0) {
            /* Move on if RDY bit is cleared */
            break;
        }

        NVMeStallExecution(pAE, MAX_STATE_STALL_us);
    }

    /*
     * Read Controller Configuration first before setting SHN bits of
     * Controller Configuration to normal shutdown (01b)
     */
    CC.AsUlong = StorPortReadRegisterUlong(pAE,
                                           (PULONG)(&pAE->pCtrlRegister->CC));
    CC.SHN = 1;
    /* Set SHN bits of Controller Configuration to normal shutdown (01b) */
    StorPortWriteRegisterUlong (pAE,
                                (PULONG)(&pAE->pCtrlRegister->CC),
                                CC.AsUlong);

    /* Checking if the adapter is ready for normal shutdown */
    for (PollCount = 0; PollCount < PollMax; PollCount++) {
        CSTS.AsUlong = StorPortReadRegisterUlong(pAE,
                           (PULONG)(&pAE->pCtrlRegister->CSTS.AsUlong));

        if (CSTS.SHST == 2) {
            /* Free the memory if we are doing shutdown */
            NVMeFreeBuffers(pAE);
            return (TRUE);
        }

        NVMeStallExecution(pAE, MAX_STATE_STALL_us);
    }

#if DBG
    /*
     * QEMU: Returning TRUE is a workaround as Qemu device is not returning the
     * status as shutdown complete. So this is a workaround for QEMU.
     */
    NVMeFreeBuffers(pAE);
    return (TRUE);
#else /* DBG */
    return (FALSE);
#endif /* DBG */
} /* NVMeNormalShutdown */

/*******************************************************************************
 * NVMeFreeBuffers
 *
 * @brief NVMeFreeBuffers gets called to free buffers that had been allocated.
 *        The buffers are required to be physically contiguous.
 *
 * @param pAE - Pointer to hardware device extension.
 *
 * @return VOID
 ******************************************************************************/
VOID NVMeFreeBuffers (
    PNVME_DEVICE_EXTENSION pAE
)
{
    USHORT QueueID;
    PQUEUE_INFO pQI = &pAE->QueueInfo;
    PRES_MAPPING_TBL pRMT = &pAE->ResMapTbl;
    PSUB_QUEUE_INFO pSQI = NULL;

    /* First, free the Start State Data buffer memory allocated by driver */
    if (pAE->DriverState.pDataBuffer != NULL)
        StorPortFreeContiguousMemorySpecifyCache((PVOID)pAE,
                                                 pAE->DriverState.pDataBuffer,
                                                 PAGE_SIZE, MmCached);

    /* Free the NVME_LUN_EXTENSION memory allocated by driver */
    if (pAE->pLunExtensionTable[0] != NULL)
        StorPortFreeContiguousMemorySpecifyCache((PVOID)pAE,
                                                 pAE->pLunExtensionTable[0],
                                                 pAE->LunExtSize,
                                                 MmCached);

    /* Free the allocated queue entry and PRP list buffers */
    if (pQI->pSubQueueInfo != NULL) {
        for (QueueID = 0; QueueID <= pRMT->NumActiveCores; QueueID++) {
            pSQI = pQI->pSubQueueInfo + QueueID;
            if (pSQI->pQueueAlloc != NULL)
                StorPortFreeContiguousMemorySpecifyCache((PVOID)pAE,
                                                         pSQI->pQueueAlloc,
                                                         pSQI->QueueAllocSize,
                                                         MmCached);

            if (pSQI->pPRPListAlloc != NULL)
                StorPortFreeContiguousMemorySpecifyCache((PVOID)pAE,
                                                         pSQI->pPRPListAlloc,
                                                         pSQI->PRPListAllocSize,
                                                         MmCached);

#ifdef DUMB_DRIVER
            if (pSQI->pDblBuffAlloc != NULL)
                StorPortFreeContiguousMemorySpecifyCache((PVOID)pAE,
                                                         pSQI->pDblBuffAlloc,
                                                         pSQI->dblBuffSz,
                                                         MmCached);
            if (pSQI->pDblBuffListAlloc != NULL)
                StorPortFreeContiguousMemorySpecifyCache((PVOID)pAE,
                                                         pSQI->pDblBuffListAlloc,
                                                         pSQI->dblBuffListSz,
                                                         MmCached);
#endif /* DUMB_DRIVER */
        }
    }

    /* Lastly, free the allocated non-contiguous buffers */
    NVMeFreeNonContiguousBuffers(pAE);
} /* NVMeFreeBuffers */


/*******************************************************************************
 * NVMeFreeNonContiguousBuffers
 *
 * @brief NVMeFreeNonContiguousBuffer gets called to free buffers that had been
 *        allocated without requiring physically contiguous.
 *
 * @param pAE - Pointer to hardware device extension.
 *
 * @return VOID
 ******************************************************************************/
VOID NVMeFreeNonContiguousBuffers (
    PNVME_DEVICE_EXTENSION pAE
)
{
    PQUEUE_INFO pQI = &pAE->QueueInfo;
    PRES_MAPPING_TBL pRMT = &pAE->ResMapTbl;

    /* Free the Start State Machine SRB EXTENSION allocated by driver */
    if (pAE->DriverState.pSrbExt != NULL)
        StorPortFreePool((PVOID)pAE, pAE->DriverState.pSrbExt);

    /* Free the resource mapping tables if allocated */
    if (pRMT->pMsiMsgTbl != NULL)
        StorPortFreePool((PVOID)pAE, pRMT->pMsiMsgTbl);

    if (pRMT->pCoreTbl != NULL)
        StorPortFreePool((PVOID)pAE, pRMT->pCoreTbl);

    if (pRMT->pNumaNodeTbl != NULL)
        StorPortFreePool((PVOID)pAE, pRMT->pNumaNodeTbl);

    /* Free the allocated SUB/CPL_QUEUE_INFO structures memory */
    if ( pQI->pSubQueueInfo != NULL )
        StorPortFreePool((PVOID)pAE, pQI->pSubQueueInfo);

    if ( pQI->pCplQueueInfo != NULL )
        StorPortFreePool((PVOID)pAE, pQI->pCplQueueInfo);

#ifdef COMPLETE_IN_DPC
    /* Free the DPC array memory */
    if (pAE->pDpcArray != NULL) {
        StorPortFreePool((PVOID)pAE, pAE->pDpcArray);
    }
#endif

} /* NVMeFreeNonContiguousBuffer */

/*******************************************************************************
 * NVMeAllocIoQueues
 *
 * @brief NVMeAllocIoQueues gets called to allocate IO queue(s) from system
 *        memory after determining the number of queues to allocate. In the case
 *        of failing to allocate memory, it needs to fall back to use one queue
 *        per adapter and free up the allocated buffers that is not used. The
 *        scenarios can be described in the follow pseudo codes:
 *
 *        if (Queue Number granted from adapter >= core number) {
 *            for (NUMA = 0; NUMA < NUMA node number; NUMA++) {
 *                for (Core = FirstCore; Core <= LastCore; Core++) {
 *                    Allocate queue pair for each core;
 *                    if (Succeeded) {
 *                        Note down which queue to use for each core;
 *                    } else {
 *                        if (failed on first queue allocation and NUMA == 0) {
 *                            return FALSE;
 *                        } else { //at least one queue allocated
 *                            // Fall back to one queue per adapter
 *                            Free up the allocated, not used queues;
 *                            Mark down number of queues allocated;
 *                            Note down which queue to use for each core;
 *                            return TRUE;
 *                        }
 *                    }
 *                }
 *            }
 *            Mark down number of queue pairs allocated;
 *            return TRUE;
 *        } else {
 *            // Allocate one queue pair only
 *            Allocate one queue pair;
 *            if (Succeeded) {
 *                Mark down one pair of queues allocated;
 *                Note down which queue to use for each core;
 *                return TRUE;
 *            } else {
 *                return FALSE;
 *            }
 *        }
 *
 * @param pAE - Pointer to hardware device extension.
 *
 * @return BOOLEAN
 *     TRUE - If all resources are allocated and initialized properly
 *     FALSE - If anything goes wrong
 ******************************************************************************/
BOOLEAN NVMeAllocIoQueues(
    PNVME_DEVICE_EXTENSION pAE
)
{
    ULONG Status = STOR_STATUS_SUCCESS;
    PQUEUE_INFO pQI = &pAE->QueueInfo;
    PRES_MAPPING_TBL pRMT = &pAE->ResMapTbl;
    PNUMA_NODE_TBL pNNT = NULL;
    PSUB_QUEUE_INFO pSQI = NULL;
    PSUB_QUEUE_INFO pSQIDest = NULL, pSQISrc = NULL;
    PCORE_TBL pCT = NULL;
    ULONG Core, Node, QEntries;
    USHORT QueueID, Queue;

    pQI->NumSubIoQAllocated = pQI->NumCplIoQAllocated = 0;

        for (Node = 0; Node < pRMT->NumNumaNodes; Node++) {
            pNNT = pRMT->pNumaNodeTbl + Node;
        QueueID = 0;
            for (Core = pNNT->FirstCoreNum; Core <= pNNT->LastCoreNum; Core++) {
                if (((pNNT->GroupAffinity.Mask >> Core) & 1) == 0)
                    continue;

                pCT = pRMT->pCoreTbl + Core;
            /*
             * If there are more cores than Qs alloc'd from the adapter, just
             * cycle through the available Qs in the core table.  Ex, if there
             * are 2 Q's and 4 cores, the table will look like:
             * Core  QID
             *  0     1
             *  1     2
             *  3     1
             *  4     2
             */
            QueueID = (QueueID + 1 > (USHORT)pQI->NumSubIoQAllocFromAdapter) ?
                1 : QueueID + 1;

            if (pQI->NumSubIoQAllocated < QueueID)  {

                QEntries = pAE->InitInfo.IoQEntries;
                Status = NVMeAllocQueues(pAE,
                                         QueueID,
                                         QEntries,
                                         (USHORT)Node);

                if (Status == STOR_STATUS_SUCCESS) {

                    pQI->NumSubIoQAllocated = ++pQI->NumCplIoQAllocated;

                } else {
                    /*
                     * If faling on the very first queue allocation, failure
                     * case.
                     */
                    if (Core == pNNT->FirstCoreNum && Node == 0) {
                        return (FALSE);
                    } else {
                        /*
                         * Fall back to share the very first queue allocated.
                         * Free the other allocated queues before returning
                         * and return TRUE.
                         */
                        Queue = 0;
                        for (Core = 1; Core < pRMT->NumActiveCores; Core++) {
                            Queue = (Queue + 1 > (USHORT)pQI->NumSubIoQAllocFromAdapter) ? 1 : Queue + 1;

                            pSQI = pQI->pSubQueueInfo + Queue;

                            if (pSQI->pQueueAlloc != NULL)
                                StorPortFreeContiguousMemorySpecifyCache(
                                    (PVOID)pAE,
                                    pSQI->pQueueAlloc,
                                    pSQI->QueueAllocSize,
                                    MmCached);

                            if (pSQI->pPRPListAlloc != NULL)
                                StorPortFreeContiguousMemorySpecifyCache(
                                    (PVOID)pAE,
                                    pSQI->pPRPListAlloc,
                                    pSQI->PRPListAllocSize,
                                    MmCached);

#ifdef DUMB_DRIVER
                            if (pSQI->pDblBuffAlloc != NULL)
                                StorPortFreeContiguousMemorySpecifyCache(
                                                        (PVOID)pAE,
                                                        pSQI->pDblBuffAlloc,
                                                        pSQI->dblBuffSz,
                                                        MmCached);

                            if (pSQI->pDblBuffListAlloc != NULL)
                                StorPortFreeContiguousMemorySpecifyCache(
                                                        (PVOID)pAE,
                                                        pSQI->pDblBuffListAlloc,
                                                        pSQI->dblBuffListSz,
                                                        MmCached);

#endif
                            pCT = pRMT->pCoreTbl + Core;
                            pCT->SubQueue = pCT->CplQueue = 1;
                        }

                        pQI->NumSubIoQAllocated = pQI->NumCplIoQAllocated = 1;
                        return (TRUE);
                    } /* fall back to use only one queue */
                } /* failure case */
            }

                /* Succeeded! Mark down the number of queues allocated */
            pCT->SubQueue = pCT->CplQueue = QueueID;
            StorPortDebugPrint(INFO,
                "NVMeAllocIoQueues: Core 0x%x ---> QueueID 0x%x\n",
                 Core, QueueID);
            } /* current core */
        } /* current NUMA node */

        return (TRUE);

} /* NVMeAllocIoQueues */
/*******************************************************************************
 * NVMeAcqQueueEntry
 *
 * @brief NVMeAcqQueueEntry gets called to retrieve a Command Info entry from
 *        the head of the specified FreeQ
 *
 * @param pAE - Pointer to hardware device extension.
 * @param pFreeQ - Pointer to the FreeQ
 * @param queue - Which queue to retrieve entry from
 *
 * @return PCMD_ENTRY
 *     Success: A poiter to the CMD_ENTRY structure
 *     Failure: A NULL pointer
 ******************************************************************************/
PCMD_ENTRY NVMeAcqQueueEntry(
    PNVME_DEVICE_EXTENSION pAE,
    PLIST_ENTRY pFreeQ
)
{
    PLIST_ENTRY pListEntry = NULL;
    PCMD_ENTRY pCmdEntry = NULL;

    if (IsListEmpty(pFreeQ) == FALSE) {
        pListEntry = (PLIST_ENTRY) RemoveHeadList(pFreeQ);
        pCmdEntry = CONTAINING_RECORD(pListEntry, CMD_ENTRY, ListEntry);

        StorPortDebugPrint(TRACE,
                           "NVMeAcqQueueEntry : Entry at 0x%llX\n",
                           pCmdEntry);
    } else {
        StorPortDebugPrint(WARNING,
                           "NVMeAcqQueueEntry: <Warning> No entry acquired.\n");
    }

    return pCmdEntry;
} /* NVMeAcqQueueEntry */

/*******************************************************************************
 * NVMeGetCmdEntry
 *
 * @brief NVMeGetCmdEntry gets called to acquire an command entry for a request
 *        which needs to be processed in adapter. The returned CMD_INFO contains
 *        CmdID and the associated PRPList that might be used when necessary.
 *
 * @param pAE - Pointer to hardware device extension.
 * @param QueueID - Which submission queue to acquire Cmd ID from
 * @param Context - Depending on callers, this can be the original SRB of the
 *                  request or anything else.
 * @param pCmdInfo - Pointer to a caller prepared buffer where the acquired
 *                   command info is saved.
 *
 * @return ULONG
 *     STOR_STATUS_SUCCESS - If a CmdID is successfully acquired
 *     Otherwise - If anything goes wrong
 ******************************************************************************/
ULONG NVMeGetCmdEntry(
    PNVME_DEVICE_EXTENSION pAE,
    USHORT QueueID,
    PVOID Context,
    PVOID pCmdInfo
)
{
    PQUEUE_INFO pQI = &pAE->QueueInfo;
    PSUB_QUEUE_INFO pSQI = NULL;
    PCMD_ENTRY pCmdEntry = NULL;
    PLIST_ENTRY pListEntry = NULL;

    if (QueueID > pQI->NumSubIoQCreated || pCmdInfo == NULL)
        return (STOR_STATUS_INVALID_PARAMETER);

    pSQI = pQI->pSubQueueInfo + QueueID;

    /* Retrieve a free CMD_INFO entry for the request */
    pListEntry = RemoveHeadList(&pSQI->FreeQList);

    /* If the retrieved pointer is the ListHead, no free entry indicated */
    if (pListEntry == &pSQI->FreeQList) {
        StorPortDebugPrint(ERROR,
                           "NVMeGetCmdEntry: <Error> Queue#%d is full!\n",
                           QueueID);

        return (STOR_STATUS_INSUFFICIENT_RESOURCES);
    }

    pCmdEntry = (PCMD_ENTRY) pListEntry;

    /* Mark down it's used and save the original context */
    pCmdEntry->Context = Context;
    ASSERT(pCmdEntry->Pending == FALSE);

    pCmdEntry->Pending = TRUE;

    /* Return the CMD_INFO structure */
    *(ULONG_PTR *)pCmdInfo = (ULONG_PTR)(&pCmdEntry->CmdInfo);

    return (STOR_STATUS_SUCCESS);
} /* NVMeGetCmdEntry */

/*******************************************************************************
 * NVMeGetCplEntry
 *
 * @brief NVMeGetCplEntry gets called to retrieve one completion queue entry at
 *        a time from the specified completion queue. This routine is
 *        responsible for looking up teh embedded PhaseTag and determine if the
 *        entry is a newly completed one. If not, return
 *        STOR_STATUS_UNSUCCESSFUL. Otherwise, do the following:
 *
 *        1. Copy the completion entry to pCplEntry
 *        2. Increase the Completion Queue Head Pointer by one.
 *        3. Reset the Head Pointer to 0 and change CurPhaseTag if it's equal to
 *           the associated CplQEntries
 *        4. Program the associated Doorbell register with the Head Pointer
 *        5. Return STOR_STATUS_SUCCESS
 *
 * @param pAE - Pointer to hardware device extension.
 * @param QueueID - Which completion queue to retrieve entry from
 * @param pCplEntry - The caller prepared buffer to save the retrieved entry
 *
 * @return ULONG
 *    STOR_STATUS_SUCCESS - If there is one newly completed entry returned
 *    STOR_STATUS_UNSUCCESSFUL - If there is no newly completed entry
 *    Otherwise - If anything goes wrong
 ******************************************************************************/
ULONG NVMeGetCplEntry(
    PNVME_DEVICE_EXTENSION pAE,
    PCPL_QUEUE_INFO pCQI,
    PVOID pCplEntry
)
{
    PQUEUE_INFO pQI = &pAE->QueueInfo;
    PNVMe_COMPLETION_QUEUE_ENTRY pCQE = NULL;

    /* Make sure the parameters are valid */
    if (pCQI->CplQueueID > pQI->NumCplIoQCreated || pCplEntry == NULL)
        return (STOR_STATUS_INVALID_PARAMETER);

    pCQE = (PNVMe_COMPLETION_QUEUE_ENTRY)pCQI->pCplQStart;
    pCQE += pCQI->CplQHeadPtr;

    /* Check Phase Tag to determine if it's a newly completed entry */
    if (pCQI->CurPhaseTag != pCQE->DW3.SF.P) {
        *(ULONG_PTR *)pCplEntry = (ULONG_PTR)pCQE;
        pCQI->CplQHeadPtr++;
        pCQI->Completions++;

        if (pCQI->CplQHeadPtr == pCQI->CplQEntries) {
            pCQI->CplQHeadPtr = 0;
            pCQI->CurPhaseTag = !pCQI->CurPhaseTag;
        }

        return STOR_STATUS_SUCCESS;
    }

    return STOR_STATUS_UNSUCCESSFUL;
} /* NVMeGetCplEntry */

/*******************************************************************************
 * NVMeReadRegistry
 *
 * @brief This function gets called to fetch values of sub-keys from Registry
 *        and it calls StorPortRegistryRead to complete.
 *
 * @param pAE - Pointer to Device Extension
 * @param pLabel - Pointer to the string name
 * @param Type - The Regsitry data type
 * @param pBuffer - The buffer holding the value
 * @param pLen - The buffer holding the length of readback data
 *
 * @return BOOLEAN
 *     Returns TRUE when no error encountered, otherwise, FALSE.
 ******************************************************************************/
BOOLEAN NVMeReadRegistry(
    PNVME_DEVICE_EXTENSION pAE,
    UCHAR* pLabel,
    ULONG Type,
    UCHAR* pBuffer,
    ULONG* pLen
)
{
    BOOLEAN Ret = FALSE;

    Ret = StorPortRegistryRead(pAE,
                               pLabel,
                               1,
                               Type,
                               pBuffer,
                               pLen);

    if (Ret == FALSE || (*pLen) == 0) {
        StorPortDebugPrint(ERROR,
                           "NVMeReadRegistry: <Error> ret = 0x%x\n",
                           Ret);
        return (FALSE);
    }

    if (*((PULONG)pBuffer) == REGISTRY_KEY_NOT_FOUND) {
        StorPortDebugPrint(ERROR,
                           "NVMeReadRegistry: <Error> Registry is not found\n");
        return (FALSE);
    }

    return (TRUE);
} /* NVMeReadRegistry */

/*******************************************************************************
 * NVMeWriteRegistry
 *
 * @brief This function gets called to write values of sub-keys in Registry and
 *        it calls StorPortRegistryWrite to complete.
 *
 * @param pAE - Pointer to Device Extension
 * @param pLabel - Pointer to the string name
 * @param Type - The Regsitry data type
 * @param pBuffer - The buffer holding the value
 * @param Len - The length of data to write
 *
 * @return BOOLEAN
 *     Returns TRUE when no error encountered, otherwise, FALSE.
 ******************************************************************************/
BOOLEAN NVMeWriteRegistry(
    PNVME_DEVICE_EXTENSION pAE,
    UCHAR* pLabel,
    ULONG Type,
    UCHAR* pBuffer,
    ULONG Len
)
{
    BOOLEAN Ret = FALSE;

    Ret = StorPortRegistryWrite(pAE,
                                pLabel,
                                1,
                                Type,
                                pBuffer,
                                Len);

    if ( Ret == FALSE ) {
        StorPortDebugPrint(ERROR,
                           "NVMeWriteRegistry: <Error> ret = 0x%x\n",
                           Ret);
        return (FALSE);
    }

    return (TRUE);
} /* NVMeWriteRegistry */

/*******************************************************************************
 * NVMeFetchRegistry
 *
 * @brief NVMeFetchRegistry gets called to fetch all the default values from
 *        Registry when the driver first loaded. The sub-keys are:
 *
 *        Namespace: The supported number of Namespace
 *        TranSize: Max transfer size in bytes with one request
 *        AdQueueEntries: The number of Admin queue entries, 128 by default
 *        IoQueueEntries: The number of IO queue entries, 1024 by default
 *        IntCoalescingTime: The frequency of interrupt coalescing time in 100
 *                           ms increments
 *        IntCoalescingEntry: The frequency of interrupt coalescing entries
 *
 * @param pAE - Device Extension
 *
 * @return BOOLEAN
 *     Returns TRUE when no error encountered, otherwise, FALSE.
 ******************************************************************************/
BOOLEAN NVMeFetchRegistry(
    PNVME_DEVICE_EXTENSION pAE
)
{
    UCHAR NAMESPACES[] = "Namespaces";
    UCHAR MAXTXSIZE[] = "MaxTXSize";
    UCHAR ADQUEUEENTRY[] = "AdQEntries";
    UCHAR IOQUEUEENTRY[] = "IoQEntries";
    UCHAR INTCOALESCINGTIME[] = "IntCoalescingTime";
    UCHAR INTCOALESCINGENTRY[] = "IntCoalescingEntries";

    ULONG Type = MINIPORT_REG_DWORD;
    UCHAR* pBuf = NULL;
    ULONG Len = sizeof(ULONG);

    /* Allocate the buffer from Storport API and zero it out if succeeded */
    pBuf = StorPortAllocateRegistryBuffer(pAE, &Len);
    if (pBuf == NULL)
        return (FALSE);

    memset(pBuf, 0, sizeof(ULONG));

    /*
     * Fetch the following initial values from
     * HKLM\System\CurrentControlSet\Services\NVMeOSD\Parameters\Device\
     * NVMeOSD stands for NVM Express Open Source Driver
     */
    if (NVMeReadRegistry(pAE,
                         NAMESPACES,
                         Type,
                         pBuf,
                         (ULONG*)&Len ) == TRUE ) {
        if (RANGE_CHK(*(PULONG)pBuf,
                      MIN_NAMESPACES,
                      MAX_NAMESPACES) == TRUE) {
            memcpy((PVOID)(&pAE->InitInfo.Namespaces),
                   (PVOID)pBuf,
                   sizeof(ULONG));
        }
    }

    memset(pBuf, 0, sizeof(ULONG));

    if (NVMeReadRegistry(pAE,
                         MAXTXSIZE,
                         Type,
                         pBuf,
                         (ULONG*)&Len ) == TRUE ) {
        if (RANGE_CHK(*(PULONG)pBuf, MIN_TX_SIZE, MAX_TX_SIZE) == TRUE) {
            memcpy((PVOID)(&pAE->InitInfo.MaxTxSize),
                   (PVOID)pBuf,
                   sizeof(ULONG));
        }
    }

    memset(pBuf, 0, sizeof(ULONG));

    if (NVMeReadRegistry(pAE,
                         ADQUEUEENTRY,
                         Type,
                         pBuf,
                         (ULONG*)&Len ) == TRUE ) {
        if (RANGE_CHK(*(PULONG)pBuf,
                      MIN_AD_QUEUE_ENTRIES,
                      MAX_AD_QUEUE_ENTRIES) == TRUE) {
            memcpy((PVOID)(&pAE->InitInfo.AdQEntries),
                   (PVOID)pBuf,
                   sizeof(ULONG));
        }
    }

    memset(pBuf, 0, sizeof(ULONG));

    if (NVMeReadRegistry(pAE,
                         IOQUEUEENTRY,
                         Type,
                         pBuf,
                         (ULONG*)&Len ) == TRUE ) {
        if (RANGE_CHK(*(PULONG)pBuf,
                      MIN_IO_QUEUE_ENTRIES,
                      MAX_IO_QUEUE_ENTRIES) == TRUE) {
            memcpy((PVOID)(&pAE->InitInfo.IoQEntries),
                   (PVOID)pBuf,
                   sizeof(ULONG));
        }
    }

    memset(pBuf, 0, sizeof(ULONG));

    if (NVMeReadRegistry(pAE,
                         INTCOALESCINGTIME,
                         Type,
                         pBuf,
                         (ULONG*)&Len ) == TRUE ) {
        if (RANGE_CHK(*(PLONG)pBuf,
                      MIN_INT_COALESCING_TIME,
                      MAX_INT_COALESCING_TIME) == TRUE) {
            memcpy((PVOID)(&pAE->InitInfo.IntCoalescingTime),
                   (PVOID)pBuf,
                   sizeof(ULONG));
        }
    }

    memset(pBuf, 0, sizeof(ULONG));

    if (NVMeReadRegistry(pAE,
                         INTCOALESCINGENTRY,
                         Type,
                         pBuf,
                         (ULONG*)&Len ) == TRUE ) {
        if (RANGE_CHK(*(PLONG)pBuf,
                      MIN_INT_COALESCING_ENTRY,
                      MAX_INT_COALESCING_ENTRY) == TRUE) {
            memcpy((PVOID)(&pAE->InitInfo.IntCoalescingEntry),
                   (PVOID)pBuf,
                   sizeof(ULONG));
        }
    }

#if defined(CHATHAM2)
    {
        UCHAR PARM1[] = "Parm1";
        UCHAR PARM2[] = "Parm2";
        UCHAR PARM3[] = "Parm3";
        UCHAR PARM4[] = "Parm4";
        UCHAR NS[] = "NsSize";
        UCHAR ID[] = "HardCodeIdData";

        memset(pBuf, 0, sizeof(ULONG));
        if (NVMeReadRegistry(pAE,
                             NS,
                             Type,
                             pBuf,
                             (ULONG*)&Len ) == TRUE ) {
                memcpy((PVOID)(&pAE->InitInfo.NsSize),
                       (PVOID)pBuf,
                       sizeof(ULONGLONG));
        }

        memset(pBuf, 0, sizeof(ULONG));
        if (NVMeReadRegistry(pAE,
                             ID,
                             Type,
                             pBuf,
                             (ULONG*)&Len ) == TRUE ) {
                memcpy((PVOID)(&pAE->InitInfo.HardCodeIdData),
                       (PVOID)pBuf,
                       sizeof(ULONGLONG));
        }

        memset(pBuf, 0, sizeof(ULONG));
        if (NVMeReadRegistry(pAE,
                             PARM1,
                             Type,
                             pBuf,
                             (ULONG*)&Len ) == TRUE ) {
                memcpy((PVOID)(&pAE->InitInfo.Parm1),
                       (PVOID)pBuf,
                       sizeof(ULONGLONG));
        }

        memset(pBuf, 0, sizeof(ULONG));
        if (NVMeReadRegistry(pAE,
                             PARM2,
                             Type,
                             pBuf,
                             (ULONG*)&Len ) == TRUE ) {
                memcpy((PVOID)(&pAE->InitInfo.Parm2),
                       (PVOID)pBuf,
                       sizeof(ULONGLONG));
        }

        memset(pBuf, 0, sizeof(ULONG));
        if (NVMeReadRegistry(pAE,
                             PARM3,
                             Type,
                             pBuf,
                             (ULONG*)&Len ) == TRUE ) {
                memcpy((PVOID)(&pAE->InitInfo.Parm3),
                       (PVOID)pBuf,
                       sizeof(ULONGLONG));
        }

        memset(pBuf, 0, sizeof(ULONG));
        if (NVMeReadRegistry(pAE,
                             PARM4,
                             Type,
                             pBuf,
                             (ULONG*)&Len ) == TRUE ) {
                memcpy((PVOID)(&pAE->InitInfo.Parm4),
                       (PVOID)pBuf,
                       sizeof(ULONGLONG));
        }

    }

#endif


    /* Release the buffer before returning */
    StorPortFreeRegistryBuffer( pAE, pBuf );

    return (TRUE);
} /* NVMeFetchRegistry */

/*******************************************************************************
 * NVMeMaskInterrupts
 *
 * @brief Masks/disables the interrupts
 *
 * @param pAE - Device Extension
 *
 * @return VOID
 ******************************************************************************/
VOID NVMeMaskInterrupts(
    PNVME_DEVICE_EXTENSION pAE
)
{
    PRES_MAPPING_TBL pRMT = &pAE->ResMapTbl;
    PMSI_MESSAGE_TBL pMMT = NULL;

    /* Determine the Interrupt type */
    switch (pRMT->InterruptType) {
        case INT_TYPE_INTX: {
            StorPortDebugPrint(INFO,
                "NVMeDisableInterrupts: Disabled INTx interrupts\n");
            break;
        }
        case INT_TYPE_MSIX: {
            break;
        }
        case INT_TYPE_MSI: {
            break;
        }
        case INT_TYPE_NONE:
        default: {
            StorPortDebugPrint(ERROR,
                "NVMeDisableInterrupts: Unrecognized intr type\n");
            break;
        }
    }
} /* NVMeMaskInterrupts */

/*******************************************************************************
 * NVMeUnmaskInterrupts
 *
 * @brief Unmasks/Enables the interrupts
 *
 * @param pAE - Device Extension
 *
 * @return VOID
 ******************************************************************************/
VOID NVMeUnmaskInterrupts(
    PNVME_DEVICE_EXTENSION pAE
)
{
    PRES_MAPPING_TBL pRMT = &pAE->ResMapTbl;
    PMSI_MESSAGE_TBL pMMT = NULL;

    /* Determine the Interrupt type */
    switch (pRMT->InterruptType) {
        case INT_TYPE_INTX: {
            StorPortDebugPrint(INFO,
                "NVMeDisableInterrupts: Disabled INTx interrupts\n");
            break;
        }
        case INT_TYPE_MSIX: {
            break;
        }
        case INT_TYPE_MSI: {
            break;
        }
        case INT_TYPE_NONE:
        default: {
            StorPortDebugPrint(ERROR,
                "NVMeDisableInterrupts: Unrecognized intr type\n");
            break;
        }
    }
} /* NVMeUnmaskInterrupts */
