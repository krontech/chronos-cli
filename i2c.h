/* Copyright 2017 Kron Technologies. All Rights Reserved. */
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
