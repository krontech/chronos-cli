/*
 * comms.h
 *
 *  Created on: Aug 24, 2016
 *      Author: david
 */

#ifndef COMMS_H_
#define COMMS_H_

#include "types.h"

BOOL jumpToProgram();
BOOL jumpToBootloader();
BOOL isInBootloader(BOOL * inBootloader);
BOOL setPowerupMode(BOOL mode);
BOOL getPowerupMode(uint8 * mode);
BOOL setShippingMode(BOOL mode);
BOOL getShippingMode(uint8 * mode);
BOOL setFanOverrideMode(BOOL enable, uint8 speed);
BOOL getFanOverrideMode(BOOL * enable, uint8 * speed);
BOOL getBatteryData(BatteryData * bd);
BOOL getPMICVersion(uint16 * version);
void doShutdown(void);
BOOL updateFirmware(const char * filename);
BOOL programData(uint8 * data, uint32 length, uint32 address);
BOOL eraseFlash(uint32 address);
void fillData(uint8 * b, int offset, uint32 data);
void txByteMessage(uint8 data);
BOOL rxDataAvailable(void);
uint16 rxDataReceive(uint8 *data, uint16 maxlen);
void txData(uint8 * data, uint16 length);
void txDataMessage(uint8 command, uint8 * data, uint16 length);
BOOL UART1Avail(void);
uint8 getcUART1(void);
void putcUART1(uint8 ch);
uint32 getUInt32(uint8 * data, uint16 offsetBytes);
void UART1Handler(void);

#define BUFFER_SIZE		(128+16)
#define BUFFER_COUNT	4

#define SOF_VALUE	0x5A


enum {
    COMM_STATE_WAIT_SOF = 0,
    COMM_STATE_DL_H,
    COMM_STATE_DL_L,
    COMM_STATE_DATA,
    COMM_STATE_CRC_H,
    COMM_STATE_CRC_L
};

typedef struct {
    uint8 battCapacityPercent;
    uint8 battSOHPercent;
    uint16 battVoltage;
    uint16 battCurrent;
    uint16 battHiResCap;
    uint16 battHiResSOC;
    uint16 voltage;
    uint16 current;
    uint16 mbTemperature;
    uint8 flags;
    uint8 fanSpeed;
} BattData_t;



#endif /* COMMS_H_ */
