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
#ifndef __MBCRC16_H
#define	__MBCRC16_H

#ifdef	__cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <sys/types.h>

#define CRC16_INIT  0xffff

uint16_t CRC16Iteration(uint16_t crc, uint8_t data);
uint16_t CRC16(const uint8_t *data, size_t length, uint16_t crc);

#ifdef	__cplusplus
}
#endif

#endif	/* __MBCRC16_H */
