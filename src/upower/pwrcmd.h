/****************************************************************************
 *  Copyright (C) 2018 Kron Technologies Inc <http://www.krontech.ca>.      *
 *                                                                          *
 *  This program is free software: you can redistribute it and/or modify    *
 *  it under the terms of the GNU General Public License as published by    *
 *  the Free Software Foundation, either version 3 of the License, or       *
 *  (at your option) any later version.                                     *
 *                                                                          *
 *  This program is distributed in the hope that it will be useful,         *
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of          *
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the           *
 *  GNU General Public License for more details.                            *
 *                                                                          *
 *  You should have received a copy of the GNU General Public License       *
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.   *
 ****************************************************************************/
#ifndef __PWRCMD_H
#define __PWRCMD_H

#include <stdint.h>
#include <stdlib.h>

/*
 Packet format
 Data	Description		Size
 0x5A	Start of frame	1
 xx		Data length		2
 xx		Data			1 to 65535
 xx		CRC16			2
 */
#define PWRCMD_SOF_VALUE        0x5a

#define PWRCMD_GET_DATA         0x00
#define PWRCMD_SHUTDOWN         0x01
#define PWRCMD_REQ_POWERDOWN    0x02
#define PWRCMD_JUMP_BOOTLOADER  0x03
#define PWRCMD_GET_DATA_EXT     0x04

#define PWRCMD_IS_IN_BOOTLOADER 0x7f

/* Flags for the extended battery data. */
#define PWRCMD_FLAG_BATT_PRESENT    0x01
#define PWRCMD_FLAG_LINE_POWER      0x02
#define PWRCMD_FLAG_CHARGING        0x04

typedef struct  {
	unsigned char battCapacityPercent;  //Battery data from ENEL4A.c
	unsigned char battSOHPercent;
	unsigned int battVoltage;
	unsigned int battCurrent;
	unsigned int battHiResCap;
	unsigned int battHiResSOC;
	unsigned int battVoltageCam;
	int battCurrentCam;
	int mbTemperature;
	unsigned char flags;
	unsigned char fanPWM;
} BatteryData;

/* Transmit a single-byte command to the power controller. */
int pwrcmd_command(int fd, uint8_t cmd);
int pwrcmd_receive(int fd, uint8_t *buf, size_t maxlen, size_t *offs);

/* Parse the battery data command. */
int pwrcmd_parse_battery(BatteryData *batt, uint8_t *buf, size_t maxlen);

#endif /* __PWRCMD_H */
