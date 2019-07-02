/* 
 * Copyright (c) 2012, Texas Instruments Incorporated
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * *  Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * *  Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * *  Neither the name of Texas Instruments Incorporated nor the names of
 *    its contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 * */
/*
 *  ======== loggerSMDump.c ========
 */

/* Standard headers */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "LoggerSM.h"

#if defined (__cplusplus)
extern "C" {
#endif /* defined (__cplusplus) */

/*
 *  Please make sure the LoggerSM configuration on the target corresponds to
 *  the below settings. For example: in dspLoggerCircBuf.cfg there is the
 *  following line (MultiProc.id = 0).
 *      LoggerSM.partitionId = MultiProc.id;
 *
 *  and the numPartitions is set to 3 in the same file.
 *      LoggerSM.numPartitions = 3;
 */
#define NUMPARTITIONS 3

#define DSP_PARTITION   0
#define VIDEO_PARTITION 1
#define VPSS_PARTITION  2

void printHelpAndExit(char *str)
{
    printf("Remote LoggerSM Client Utility, (c) Texas Instruments 2011\n\n");
    printf(" %s [-d] <addr> <size> <core name> [<filename>]\n\n", str);    
    printf(" -d:        Display the contents of the partition headers before\n");
    printf("            the records are processed\n\n");
    printf(" addr:      Remote debug shared memory physical address in Hex\n");
    printf("            The shared memory physical address MUST be 4KB aligned.\n\n");
    printf(" size:      Size of hte remote debug shared memory\n\n");
    printf(" core name: Name of the cores that are processed.\n");
    printf("            valid names are: \"dsp\", \"video\", \"vpss\", \"m3\" or \"all\"\n");
    printf("            m3 denotes to process both video and vpss.\n");
    printf("            all denotes to process all 3 targets.\n\n");
    printf(" filename:  If target has encoded records, name of the file to store\n");
    printf("            the encoded records. They can be decoded by System Analyzer.\n");
    printf("            This parameter is optional. If no filename is specified and encoded\n");
    printf("            events are found, the default file name \"loggerSM.bin\" is used.\n\n");
    printf(" Examples: %s 0x8f000000 0x20000 video myBinaryFile\n", str);
    printf("           %s 0x8f000000 0x20000 m3 myBinaryFile\n", str);
    printf("           %s 0x8f000000 0x20000 all\n\n", str);
    printf("           %s -d 0x8f000000 0x20000\n\n", str);
    exit(0);
}

int main (int argc, char **argv)
{
    unsigned int remoteDebugShmPhysAddr;
    size_t remoteDebugShmPhysSize;
    char *endPtr;
    char *filename = NULL;
    char *coreName;
    unsigned int partitionMask = 0;
        
    /* Deal with all the passed in arguments */
    if ((argc != 1) && (strcmp(argv[1], "-d") == 0)) {
        if (argc < 5) {
            printHelpAndExit(argv[0]);
        }
        remoteDebugShmPhysAddr = strtoul(argv[2], &endPtr, 0);
        remoteDebugShmPhysSize = strtoul(argv[3], &endPtr, 0);
        LoggerSM_dumpSharedMem(remoteDebugShmPhysAddr, remoteDebugShmPhysSize,
                 NUMPARTITIONS); 
        coreName = argv[4];
        /* Set the filename */
        if (argc >= 6) {
            filename = argv[5];
        }
    }
    else {
        if (argc < 4) {
            printHelpAndExit(argv[0]);
        }
        remoteDebugShmPhysAddr = strtoul(argv[1], &endPtr, 0);
        remoteDebugShmPhysSize = strtoul(argv[2], &endPtr, 0);
        coreName = argv[3];
        /* Set the filename */
        if (argc >= 5) {
            filename = argv[4];
        }
    }

    /* Make sure the address is on a 4KB page boundary */    
    if(remoteDebugShmPhysAddr == 0 || (remoteDebugShmPhysAddr & 0xFFF)) {
        printHelpAndExit(argv[0]);
    }
    
    /* Determine which targets to process */
    if (strcasecmp(coreName, "m3") == 0) {
        partitionMask = 1 << VIDEO_PARTITION |
                        1 << VPSS_PARTITION;
    }
    else if (strcasecmp(coreName, "dsp") == 0) {
        partitionMask = 1 << DSP_PARTITION;
    }
    else if (strcasecmp(coreName, "video") == 0) {
        partitionMask = 1 << VIDEO_PARTITION;
    }
    else if (strcasecmp(coreName, "vpss") == 0) {
        partitionMask = 1 << VPSS_PARTITION;
    }
    else if (strcasecmp(coreName, "all") == 0) {
        partitionMask = 1 << DSP_PARTITION |
                        1 << VIDEO_PARTITION |
                        1 << VPSS_PARTITION;
    }
    else {
        printHelpAndExit(argv[0]);
    }    

    /*
     *  Set the names of each partition. Setting these names is optional. If not
     *  done, only the partition id is displayed.
     */
    LoggerSM_setName(DSP_PARTITION,   "DSP  ");
    LoggerSM_setName(VIDEO_PARTITION, "Video");
    LoggerSM_setName(VPSS_PARTITION,  "VPSS ");

    /*
     *  Start dumping the the records.
     *
     *  Parameter: remoteDebugShmPhysAddr
     *  Either look at the target's mapfile for
     *  ti_uia_runtime_LoggerSM_sharedBuffer__A or look in the .cfg file and
     *  look for the hard-coded loggerSM address.
     *
     *  Parameter: remoteDebugShmPhysSize
     *  Size for each partitions LoggerSM buffer size. If you change the
     *  size on the target, make sure to change this value also.
     *
     *  Parameter: NUMPARTITIONS
     *  Number of cores sharing the LoggerSM buffer. If you change the
     *  size on the target, make sure to change this value also.
     *
     *  Parameter: partitionMask
     *  Which cores to process.
     *
     *  Parameter: filename
     *  Use to write binary data if any of the partitions are set to not be
     *  ASCII. Instead it is binary data that can be decoded by DVT. Refer to
     *  the LoggerSM.decode configuration parameter.
     * */
    LoggerSM_run(remoteDebugShmPhysAddr, remoteDebugShmPhysSize,
                 NUMPARTITIONS, partitionMask, filename);

    return (0);
}

#if defined (__cplusplus)
extern "C" {
#endif /* defined (__cplusplus) */
/*
 *  @(#) ti.uia.examples.evmti816x; 2, 0, 0, 0,1; 7-13-2012 11:16:37; /db/vtree/library/trees/uia/uia-d14/src/ xlibrary

 */

