/*
 * File:   MBCRC16.h
 * Author: David
 *
 * Created on January 14, 2014, 4:48 PM
 */

#ifndef MBCRC16_H
#define	MBCRC16_H

#ifdef	__cplusplus
extern "C" {
#endif

#include "types.h"

void CRC16Init(uint16 * wCRCWord);
uint16 CRC16Iteration(uint16 * wCRCWord, uint8 data);
uint16 CRC16 (const uint8 *nData, uint16 wLength);

#ifdef	__cplusplus
}
#endif

#endif	/* MBCRC16_H */

