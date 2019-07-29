/*
 * pcutil.h
 *
 *  Created on: Oct 12, 2014
 *      Author: david
 */

#ifndef PCUTIL_H_
#define PCUTIL_H_

//IVT
#define IVT_START_WORDS			0x000000     //Words
#define IVT_LENGTH_WORDS		0x000200     //Words
#define IVT_END_WORDS			(IVT_START_WORDS + IVT_LENGTH_WORDS - 1)     //Words
#define IVT_START_BYTES			(IVT_START_WORDS*2)     //Words
#define IVT_LENGTH_BYTES		(IVT_LENGTH_WORDS*2)     //Words
#define IVT_END_BYTES			(IVT_START_BYTES + IVT_LENGTH_BYTES - 1)     //Words

//Bootloader memory
#define BOOTLOADER_START_WORDS	0x000200     //Words
#define BOOTLOADER_LENGTH_WORDS	0x000C00     //Words
#define BOOTLOADER_END_WORDS	(BOOTLOADER_START_WORDS + BOOTLOADER_LENGTH_WORDS - 1)     //Words
#define BOOTLOADER_START_BYTES	(BOOTLOADER_START_WORDS*2)     //Words
#define BOOTLOADER_LENGTH_BYTES	(BOOTLOADER_LENGTH_WORDS*2)     //Words
#define BOOTLOADER_END_BYTES	(BOOTLOADER_START_BYTES + BOOTLOADER_LENGTH_BYTES - 1)     //Words

#define CONFIG_MEM_START_WORDS	0xF80000
#define CONFIG_MEM_LENGTH_WORDS	0x000010
#define CONFIG_MEM_START_BYTES	(CONFIG_MEM_START_WORDS*2)
#define CONFIG_MEM_END_BYTES	(CONFIG_MEM_START_BYTES + CONFIG_MEM_LENGTH_WORDS * 2 - 1)

//Main application memory
#define LOWER_WORD_ADDRESS	0x000E00    //Words
#define UPPER_WORD_ADDRESS	0x002BFF    //Words
#define LOWER_ADDRESS	(LOWER_WORD_ADDRESS*2)    //Words
#define UPPER_ADDRESS	(UPPER_WORD_ADDRESS*2)    //Words
#define FLASH_PAGE_SIZE	64	    //Words
#define FLASH_PAGE_SIZE_BYTES	(FLASH_PAGE_SIZE * 2)	    //Bytes

#define POWERUP_ON_AC_RESTORE	(1 << 0)
#define POWERUP_ON_AC_REMOVE	(1 << 1)

enum {
    //User application commands
    COM_CMD_GET_DATA = 0,
    COM_CMD_SHUTDOWN,
    COM_CMD_POWER_DOWN_REQUEST,
    COM_CMD_JUMP_TO_BOOTLOADER,
    COM_CMD_GET_DATA_2,
    COM_CMD_GET_APP_VERSION,
    COM_CMD_SET_POWERUP_MODE,
    COM_CMD_GET_POWERUP_MODE,
    COM_CMD_SET_FAN_SPEED_OVERRIDE,
    COM_CMD_GET_FAN_SPEED_OVERRIDE,
    COM_CMD_SET_SHIPPING_MODE,
    COM_CMD_GET_SHIPPING_MODE,

    //Common commands
    COM_CMD_IS_IN_BOOTLOADER = 127,

    //Bootloader only commands
    COM_CMD_ERASE_PAGE = 128,
    COM_CMD_WRITE_DATA,
    COM_CMD_JUMP_TO_PGM,
    COM_CMD_GET_BOOT_VERSION

};

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



#endif /* PCUTIL_H_ */
