/*
 * comms.cpp
 *
 *  Created on: Aug 24, 2016
 *      Author: david
 */
#include "pcutil.h"
#include "types.h"
#include "comms.h"
#include "MBCRC16.h"
#include "IntelHex.h"

#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>
#include <time.h>
#include <sys/time.h>

volatile uint8 __attribute__((aligned(4))) rxBuffer[BUFFER_COUNT][BUFFER_SIZE];
volatile uint16 rxBufferLen[BUFFER_COUNT] = {0};
volatile uint8 rxBufWriteIndex = 0;
volatile uint8 rxBufReadIndex = 0;
extern volatile uint8 terminateRxThread;

pthread_mutex_t rxBufLock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t rxBufCond = PTHREAD_COND_INITIALIZER;

extern int sfd;

// Causes the controller to jump to the user application reset address from the bootloader
// returns true if successful, false otherwise
BOOL jumpToProgram()
{
	uint8 response[1];
	uint16 length;
	txByteMessage(COM_CMD_JUMP_TO_PGM);

	length = rxDataReceive(response, sizeof(response));
	if (length == 0) return FALSE;
	if (COM_CMD_JUMP_TO_PGM == response[0])
		return TRUE;
	else
		return FALSE;
}


// Causes the controller to reset and boot into the bootloader.
// The controller will stay in the bootloader until commanded to jump back with jumpToProgram.
// returns true if successful, false otherwise
BOOL jumpToBootloader()
{
	uint8 response[1];
	uint16 length;
	txByteMessage(COM_CMD_JUMP_TO_BOOTLOADER);

	length = rxDataReceive(response, sizeof(response));
	if (length == 0) return FALSE;
	if (COM_CMD_JUMP_TO_BOOTLOADER == response[0])
		return TRUE;
	else
		return FALSE;
}

BOOL isInBootloader(BOOL * inBootloader)
{
	uint8 response[2];
	uint16 length;
	txByteMessage(COM_CMD_IS_IN_BOOTLOADER);

	length = rxDataReceive(response, sizeof(response));
	if (length == 0) return FALSE;
	if (COM_CMD_IS_IN_BOOTLOADER == response[0])
	{
		*inBootloader = response[1];
		return TRUE;
	}
	else
		return FALSE;
}

BOOL setPowerupMode(uint8 mode)
{
	uint8 response[1];
	uint16 length;
	uint8 data = mode;

	txDataMessage(COM_CMD_SET_POWERUP_MODE, &data, sizeof(data));

	length = rxDataReceive(response, sizeof(response));
	if (length == 0) return FALSE;
	if (COM_CMD_SET_POWERUP_MODE == response[0])
		return TRUE;
	else
		return FALSE;
}

BOOL getPowerupMode(uint8 * mode)
{
	uint8 response[2];
	uint16 length;
	txByteMessage(COM_CMD_GET_POWERUP_MODE);

	length = rxDataReceive(response, sizeof(response));
	if (length == 0) return FALSE;
	if (COM_CMD_GET_POWERUP_MODE == response[0])
	{
		*mode = response[1];
		return TRUE;
	}
	else
		return FALSE;
}

BOOL setShippingMode(uint8 mode)
{
	uint8 response[1];
	uint16 length;
	uint8 data = mode;

	txDataMessage(COM_CMD_SET_SHIPPING_MODE, &data, sizeof(data));

	length = rxDataReceive(response, sizeof(response));
	if (length == 0) return FALSE;
	if (COM_CMD_SET_SHIPPING_MODE == response[0])
		return TRUE;
	else
		return FALSE;
}

BOOL getShippingMode(uint8 * mode)
{
	uint8 response[2];
	uint16 length;
	txByteMessage(COM_CMD_GET_SHIPPING_MODE);

	length = rxDataReceive(response, sizeof(response));
	if (length == 0) return FALSE;
	if (COM_CMD_GET_SHIPPING_MODE == response[0])
	{
		*mode = response[1];
		return TRUE;
	}
	else
		return FALSE;
}

BOOL setFanOverrideMode(BOOL enable, uint8 speed)
{
	uint8 response[1];
	uint16 respLen;
	uint8 dataPkt[2];

	dataPkt[0] = enable;
	dataPkt[1] = speed;

	txDataMessage(COM_CMD_SET_FAN_SPEED_OVERRIDE, dataPkt, sizeof(dataPkt));

	respLen = rxDataReceive(response, sizeof(response));
	if (respLen == 0) return FALSE;
	if (COM_CMD_SET_FAN_SPEED_OVERRIDE == response[0])
		return TRUE;
	else
		return FALSE;
}

BOOL getFanOverrideMode(BOOL * enable, uint8 * speed)
{
	uint8 response[3];
	uint16 length;
	txByteMessage(COM_CMD_GET_FAN_SPEED_OVERRIDE);

	length = rxDataReceive(response, sizeof(response));
	if (length == 0) return FALSE;
	if (COM_CMD_GET_FAN_SPEED_OVERRIDE == response[0])
	{
		*enable = response[1];
		*speed = response[2];
		return TRUE;
	}
	else
		return FALSE;
}

BOOL getBatteryData(BatteryData * bd)
{
	uint8 i;
	uint8 response[BUFFER_SIZE];
	uint16 length;
	txByteMessage(COM_CMD_GET_DATA_2);

	length = rxDataReceive(response, sizeof(response));
	if (length == 0) return FALSE;
	if (COM_CMD_GET_DATA_2 == response[0])
	{
		i = 1;
		bd->battCapacityPercent = response[i++];
		bd->battSOHPercent = response[i++];
		bd->battVoltage = response[i++] << 8;
		bd->battVoltage |= response[i++] & 0xFF;
		bd->battCurrent = response[i++] << 8;
		bd->battCurrent |= response[i++] & 0xFF;
		bd->battHiResCap = response[i++] << 8;
		bd->battHiResCap |= response[i++] & 0xFF;
		bd->battHiResSOC = response[i++] << 8;
		bd->battHiResSOC |= response[i++] & 0xFF;
		bd->battVoltageCam= response[i++] << 8;
		bd->battVoltageCam |= response[i++] & 0xFF;
		bd->battCurrentCam= response[i++] << 8;
		bd->battCurrentCam |= response[i++] & 0xFF;
		bd->mbTemperature= response[i++] << 8;
		bd->mbTemperature |= response[i++] & 0xFF;
		bd->flags = response[i++];
		bd->fanPWM = response[i++];

		return TRUE;
	}
	else
		return FALSE;

}

BOOL getPMICVersion(uint16 * version)
{
	uint8 response[3];
	uint16 length;
	txByteMessage(COM_CMD_GET_APP_VERSION);

	length = rxDataReceive(response, sizeof(response));
	if (length == 0) return FALSE;
	if (COM_CMD_GET_APP_VERSION == response[0])
	{
		*version = response[1] >> 8;
		*version = response[2] & 0xFF;

		return TRUE;
	}
	else
		return FALSE;
}

BOOL getLastShutdownReason(uint8 * reason)
{
	uint8 response[2];
	uint16 length;
	txByteMessage(COM_CMD_GET_SHUTDOWN_REASON);

	length = rxDataReceive(response, sizeof(response));
	if (length == 0) return FALSE;
	if (COM_CMD_GET_SHUTDOWN_REASON == response[0])
	{
		*reason = response[1];
		return TRUE;
	}
	else
		return FALSE;
}

void doShutdown(void)
{
     txByteMessage(COM_CMD_SHUTDOWN);
}

//Update the firmware
//
//filename - Path to Intel Hex file with program data
//Returns - true if successful, false otherwise
BOOL updateFirmware(const char * filename)
{
	uint8 data[16];
	uint32 length;
	uint32 address;
	BOOL ret;
	struct IntelHex hp;

	ret = IntelHexOpen(&hp, filename);
	if (!ret) return FALSE;

	//Check if in bootloader
	BOOL inBootloader;
	ret = isInBootloader(&inBootloader);
	if(!ret) return FALSE;

	//If not in bootloader, send to bootloader
	if (!inBootloader)
	{
		printf("In user application, jumping to bootloader...\r\n");
		ret = jumpToBootloader();
		if(!ret) return FALSE;

		//Give the controller some time to reboot
		//Thread.Sleep(100);
		int ms = 100;
		struct timespec ts = { ms / 1000, (ms % 1000) * 1000 * 1000 };
		nanosleep(&ts, NULL);
		printf("Done, now in bootloader\r\n");
	}

	printf("Erasing flash...\r\n");

	//Erase the entire IVT flash
	for (address = IVT_START_BYTES; address < IVT_END_BYTES; address += FLASH_PAGE_SIZE_BYTES)
	{
		printf("Erasing Address %x\r", (unsigned int)address);
		ret = eraseFlash(address/2);
		if (!ret) return FALSE;
	}

	//Erase the entire user program (non-bootloader) flash
	for (address = LOWER_ADDRESS; address < UPPER_ADDRESS; address += FLASH_PAGE_SIZE_BYTES)
	{
		printf("Erasing Address %x\r", (unsigned int)address);
		ret = eraseFlash(address/2);
		if (!ret) return FALSE;
	}
	printf("\r\nDone erasing flash\r\n");
	//Program all addresses specified by the program hex file that are within the user program space
	while (IntelHexReadLine(&hp, data, &length, &address) && length > 0)
	{
	 //   if (((address < BOOTLOADER_START_BYTES || address > BOOTLOADER_END_BYTES) && address <= UPPER_ADDRESS)/* ||
	 //       (address >= CONFIG_MEM_START_BYTES && address <= CONFIG_MEM_END_BYTES)*/ ) //If this address is not within the bootloader region
		if (	(address >= IVT_START_BYTES && address <= IVT_END_BYTES) ||
				(address >= LOWER_ADDRESS && address <= UPPER_ADDRESS))
		{
			printf("Writing to address %x\r", (unsigned int)address);

			uint8 block1[FLASH_PAGE_SIZE_BYTES];
			uint8 block2[FLASH_PAGE_SIZE_BYTES];
			BOOL writeBlock2 = FALSE;
			uint32 i;

			//Fill the block buffer with 1s
			for (i = 0; i < FLASH_PAGE_SIZE_BYTES; i++) {
				block1[i] = block2[i] = 0xFF;
			}

			//Get the address to write within this block
			uint32 blockAddress = address & (FLASH_PAGE_SIZE_BYTES-1);

			//Put the data into this block
			for (i = 0; i < length; i++) {
				uint32 addressInBlock = blockAddress + i;

				if (addressInBlock < FLASH_PAGE_SIZE_BYTES)
				{
					block1[addressInBlock] = data[i];
				}
				else
				{
					writeBlock2 = TRUE;
					block2[addressInBlock - FLASH_PAGE_SIZE_BYTES] = data[i];
				}
			}

			ret = programData(block1, FLASH_PAGE_SIZE_BYTES, (address & ~(FLASH_PAGE_SIZE_BYTES-1)) >> 1);   //Address passed here is word address for the beginning of the block
			if (!ret) return FALSE;

			if(writeBlock2)
			{
				ret = programData(block2, FLASH_PAGE_SIZE_BYTES, ((address & ~(FLASH_PAGE_SIZE_BYTES-1)) + FLASH_PAGE_SIZE_BYTES) >> 1);
				if (!ret) return FALSE;
			}

		}
	}

	printf("\r\nJumping to program\r\n");
	//Jump to the user program reset address from the bootloader
	jumpToProgram();
	IntelHexClose(&hp);
	return TRUE;
}


//Write a block of data to flash, 32-bit words
//
//data - Array of uint32 to program (max size 16 words)
//address - Physical address to write data to in flash
//Returns - true if successful, false otherwise
BOOL programData(uint8 * data, uint32 length, uint32 address)
{
	uint8 response[1];
	uint16 respLen;
	uint8 dataPkt[1 + 1 + 4 + 2 + length];
	uint32 i;

	dataPkt[0] = COM_CMD_WRITE_DATA;
	dataPkt[1] = length / 4;
	fillData(dataPkt, 2, address);	//address
	dataPkt[6] = 0; //Padding
	dataPkt[7] = 0;
	for (i = 0; i < length; i++) {
		dataPkt[8 + i] = data[i];
	}

	txData(dataPkt, 1 + 1 + 4 + 2 + length);

	length = rxDataReceive(response, sizeof(response));
	if (length == 0) return FALSE;
	if (COM_CMD_WRITE_DATA == response[0])
		return TRUE;
	else
		return FALSE;
}


// Erase a page of flash memory
// Page size is specified by FLASH_PAGE_SIZE.
//
//address - Starting address of the page to erase (physical address)
//returns - true if successful, false otherwise
BOOL eraseFlash(uint32 address)
{
	uint8 response[1];
	uint16 respLen;
	uint8 data[1 + 4];

	data[0] = COM_CMD_ERASE_PAGE;
	fillData(data, 1, address);	//address
	txData(data, 1 + 4);

	respLen = rxDataReceive(response, sizeof(response));
	if (respLen == 0) return FALSE;
	if (COM_CMD_ERASE_PAGE == response[0])
		return TRUE;
	else
		return FALSE;
}

void fillData(uint8 * b, int offset, uint32 data)
{
	b[offset] =		(uint8)(data & 0xFF);
	b[offset + 1] = (uint8)((data >> 8) & 0xFF);
	b[offset + 2] = (uint8)((data >> 16) & 0xFF);
	b[offset + 3] = (uint8)((data >> 24) & 0xFF);

}


void txByteMessage(uint8 data)
{
	txData(&data, 1);
}

uint16 rxDataReceive(uint8 *data, uint16 maxlen)
{
	struct timeval now;
	struct timespec timeout;
	gettimeofday(&now, NULL);
	timeout.tv_sec = now.tv_sec + RXDATA_TIMEOUT;
	timeout.tv_nsec = now.tv_usec * 1000;

	pthread_mutex_lock(&rxBufLock);
	
	/* Wait until data is available. */
	while (rxBufWriteIndex == rxBufReadIndex) {
		int n = pthread_cond_timedwait(&rxBufCond, &rxBufLock, &timeout);
		if (n == ETIMEDOUT) {
			/* An error occured */
			pthread_mutex_unlock(&rxBufLock);
			return 0;
		}
	}

	/* Get the next message off the buffer ring */
	uint16 msglen = rxBufferLen[rxBufReadIndex];
	uint8 *msgdata = (uint8 *)rxBuffer[rxBufReadIndex++];
	if (rxBufReadIndex >= BUFFER_COUNT) {
		rxBufReadIndex = 0;
	}
	
	/* Copy data out to the caller. */
	if (msglen > maxlen) msglen = maxlen; /* Truncate or return error? */
	memcpy(data, msgdata, msglen);

	pthread_mutex_unlock(&rxBufLock);
	return msglen;
}

//Transmit a data packet
void txData(uint8 * data, uint16 length)
{
	uint16 crc;
	uint32 i;
	CRC16Init(&crc);

	putcUART1(SOF_VALUE);
	putcUART1(length >> 8);
	CRC16Iteration(&crc, length >> 8);

	putcUART1(length & 0xFF);
	CRC16Iteration(&crc, length & 0xFF);

	for(i = 0; i < length; i++)
	{
		putcUART1(data[i]);
		CRC16Iteration(&crc, data[i]);
	}

	putcUART1(crc >> 8);
	putcUART1(crc & 0xFF);
}
//Transmit a data packet consisting of command followed by length bytes of the array data
void txDataMessage(uint8 command, uint8 * data, uint16 length)
{
	uint16 crc;
	uint32 i;
	CRC16Init(&crc);

	putcUART1(SOF_VALUE);
	putcUART1((length+1) >> 8);
	CRC16Iteration(&crc, (length+1) >> 8);

	putcUART1((length+1) & 0xFF);
	CRC16Iteration(&crc, (length+1) & 0xFF);

	putcUART1(command);
	CRC16Iteration(&crc, command);

	for(i = 0; i < length; i++)
	{
		putcUART1(data[i]);
		CRC16Iteration(&crc, data[i]);
	}

	putcUART1(crc >> 8);
	putcUART1(crc & 0xFF);
}

/*
 Packet format
 Data	Description		Size
 0x5A	Start of frame	1
 xx		Data length		2
 xx		Data			1 to 65535
 xx		CRC16			2

 */

void putcUART1(uint8 ch)
{
	//printf("0x%02x, ", ch);
	write (sfd, &ch, 1);
}

//These used to be locals in rxThread but some are incorrectly optimized away when optimization is turned on. Out here they don't cause any problems.
unsigned int commState = COMM_STATE_WAIT_SOF;
uint16 crc;
unsigned char data;
unsigned short dataLength, dataCount;
unsigned int readLen;

void* rxThread(void *arg)
{
	while(!terminateRxThread)
	{
		readLen = read (sfd, &data, 1);


		if(readLen > 0)
		{
			pthread_mutex_lock(&rxBufLock);
			//printf("read %02x\r\n", data);
			switch(commState)
			{
				case COMM_STATE_WAIT_SOF:
					if(SOF_VALUE == data)
					{
						commState = COMM_STATE_DL_H;
						CRC16Init(&crc);
					}
				break;

				case COMM_STATE_DL_H:
					dataLength = data << 8;
					CRC16Iteration(&crc, data);
					commState = COMM_STATE_DL_L;
				break;

				case COMM_STATE_DL_L:
					dataLength |= data;
					CRC16Iteration(&crc, data);
					if(dataLength <= BUFFER_SIZE)
					{
						dataCount = 0;
						commState = COMM_STATE_DATA;
					}
					else
					{	//Data length too long
						commState = COMM_STATE_WAIT_SOF;
					}
				break;

				case COMM_STATE_DATA:
					CRC16Iteration(&crc, data);
					rxBuffer[rxBufWriteIndex][dataCount++] = data;
					if(dataCount >= dataLength)
					{
						commState = COMM_STATE_CRC_H;
					}
				break;

				case COMM_STATE_CRC_H:
					if(data == (crc >> 8))
						//CRC check 1st part succeeded
						commState = COMM_STATE_CRC_L;
					else
						//CRC check failed
						commState = COMM_STATE_WAIT_SOF;
				break;

				case COMM_STATE_CRC_L:
					if(data == (crc & 0xFF))
					{	//CRC check succeeded
						pthread_cond_broadcast(&rxBufCond);
						rxBufferLen[rxBufWriteIndex] = dataLength;
						rxBufWriteIndex++;
						if (rxBufWriteIndex >= BUFFER_COUNT)
							rxBufWriteIndex = 0;
					}
					commState = COMM_STATE_WAIT_SOF;
				break;
			}//switch(commState)
			pthread_mutex_unlock(&rxBufLock);
		}
	}

	pthread_exit(NULL);
	return (void *)0;
}
