/****************************************************************************
 *  Copyright (C) 2019 Kron Technologies Inc <http://www.krontech.ca>.      *
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
#ifndef __REGS_H
#define __REGS_H

#include "fpga.h"
#include <stdint.h>

/* Register definition */
struct regdef {
    const char      *name;
    uint32_t        mask;
    unsigned int    offset;
    unsigned int    size;
    unsigned long   (*reg_read)(const struct regdef *reg, struct fpga *fpga);
    int             (*reg_write)(const struct regdef *reg, struct fpga *fpga, unsigned long val);
};

/* Regsister group definition */
struct reggroup {
    const char  *name;
    /* Steps necessary to execute before accessing any register */
    void        (*setup)(const struct reggroup *group, struct fpga *fpga);
    void        (*cleanup)(const struct reggroup *group, struct fpga *fpga);
    /* Table of register definitions */
    const struct regdef *rtab;
};

/* Some helper functions. */
unsigned long fpga_reg_read(const struct regdef *r, struct fpga *fpga);
unsigned long sci_reg_read(const struct regdef *r, struct fpga *fpga);
int sci_reg_write(const struct regdef *reg, struct fpga *fpga, unsigned long val);
unsigned int sci_read_chipid(struct fpga *fpga);

/* FPGA Register tables. */
extern const struct reggroup sensor_registers;
extern const struct reggroup seq_registers;
extern const struct reggroup trigger_registers;
extern const struct reggroup display_registers;
extern const struct reggroup config_registers;
extern const struct reggroup vram_registers;
extern const struct reggroup seqpgm_registers;
extern const struct reggroup imager_registers;
extern const struct reggroup overlay_registers;
extern const struct reggroup zebra_registers;

/* Luxima Register tables. */
extern const struct reggroup luxima_chipid_registers;
extern const struct reggroup lux1310_registers;
extern const struct reggroup lux2100_sensor_registers;
extern const struct reggroup lux2100_datapath_registers;

#endif /* __REGS_H */
