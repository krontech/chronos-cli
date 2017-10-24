/****************************************************************************
 *  Copyright (C) 2017 Kron Technologies Inc <http://www.krontech.ca>.      *
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
#ifndef _I2C_H
#define _I2C_H

#include <sys/types.h>

#define I2C_BUS_DEFAULT     "/dev/i2c-0"
#define I2C_ADDR_DEFAULT    0x50         /* the 24C16 sits on i2c address 0x50 */

int i2c_eeprom_write(int fd, unsigned int addr, unsigned int offset, const void *buf, size_t len);
int i2c_eeprom_write16(int fd, unsigned int addr, unsigned int offset, const void *buf, size_t len);

int i2c_eeprom_read(int fd, unsigned int addr, unsigned int offset, void *buf, size_t len);
int i2c_eeprom_read16(int fd, unsigned int addr, unsigned int offset, void *buf, size_t len);

#endif // _I2C_H
