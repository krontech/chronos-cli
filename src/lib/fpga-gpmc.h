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
#ifndef _FPGA_GPMC_H
#define _FPGA_GPMC_H

#include <stdint.h>

struct gpmc_cs {
    uint32_t config1;
    uint32_t config2;
    uint32_t config3;
    uint32_t config4;
    uint32_t config5;
    uint32_t config6;
    uint32_t config7;
    uint32_t nand_cmd;
    uint32_t nand_addr;
    uint32_t nand_data;
    uint32_t __reserved_cs[2];
};

struct gpmc {
    const uint32_t  revision;
    uint32_t        __reserved0[3];
    uint32_t        sysconfig;
    const uint32_t  sysstatus;
    uint32_t        irqstatus;
    uint32_t        irqenable;
    uint32_t        __reserved1[8];
    uint32_t        timeout_control;
    const uint32_t  err_addr;
    const uint32_t  err_type;
    uint32_t        __reserved2[1];
    uint32_t        config;
    uint32_t        status;
    uint32_t        __reserved3[2];
    struct gpmc_cs  cs[6];
    uint32_t        __reserved4[24];
    uint32_t        prefetch_config1;
    uint32_t        prefetch_config2;
    uint32_t        __reserved5[1];
    uint32_t        prefetch_control;
    uint32_t        prefetch_status;
    uint32_t        ecc_config;
    uint32_t        ecc_control;
    uint32_t        ecc_size;
    uint32_t        ecc_result[8];
    /* TODO: BCH stuff is complicated and seemingly unecessary. */
};

#define GPMC_REVISION_REV   0xff

#define GPMC_SYSCONFIG_SIDLEMODE        (0x3<<3)
#define GPMC_SYSCONFIG_SIDLEMODE_FORCE      (0<<3)
#define GPMC_SYSCONFIG_SIDLEMODE_NONE       (1<<3)
#define GPMC_SYSCONFIG_SIDLEMODE_SMART      (2<<3)
#define GPMC_SYSCONFIG_SOFTRESET        (1<<1)
#define GPMC_SYSCONFIG_AUTOIDLE         (1<<0)

#define GPMC_SYSSTATUS_RESETDONE        (1<<0)

/* GPMC IRQ Status and Enable registers */
#define GPMC_IRQ_WAIT1EDGE              (1<<9)
#define GPMC_IRQ_WAIT0EDGE              (1<<8)
#define GPMC_IRQ_TERMINALCOUNT          (1<<1)
#define GPMC_IRQ_FIFOEVENT              (1<<0)

#define GPMC_TIMEOUT_START_VALUE(_x_)   ((_x_) << 4)
#define GPMC_TIMEOUT_ENABLE             (1<<0)

#define GPMC_ERR_TYPE_ILLEGALCMD        (0x7<<8)
#define GPMC_ERR_TYPE_NOTSUPPADD        (1<<4)
#define GPMC_ERR_TYPE_NOTSUPPMCMD       (1<<3)
#define GPMC_ERR_TYPE_TIMEOUT           (1<<1)
#define GPMC_ERR_TYPE_VALID             (1<<0)

#define GPMC_CONFIG_WAIT1_POLARITY      (1<<9)
#define GPMC_CONFIG_WAIT0_POLARITY      (1<<8)
#define GPMC_CONFIG_WRITE_PROTECT       (1<<4)
#define GPMC_CONFIG_LIMITED_ADDRESS     (1<<1)
#define GPMC_CONFIG_NAND_FORCE_POSTED   (1<<0)

#define GPMC_STATUS_WAIT1_STATUS        (1<<9)
#define GPMC_STATUS_WAIT0_STATUS        (1<<8)
#define GPMC_STATUS_EMPTY_WRITE_BUFFER  (1<<0)

/* GPMC_CONFIG1 for signal control parameters */
#define GPMC_CONFIG_CS_WRAP_BUSRT       (1<<31)
#define GPMC_CONFIG_CS_READ_MULTIPLE    (1<<30)
#define GPMC_CONFIG_CS_READ_TYPE        (1<<29)
#define GPMC_CONFIG_CS_WRITE_MULTIPLE   (1<<28)
#define GPMC_CONFIG_CS_WRITE_TYPE       (1<<27)
#define GPMC_CONFIG_CS_CLK_ACTIVATION(_x_)   ((_x_)<<25)
#define GPMC_CONFIG_CS_PAGE_LENGTH      (0x3<<23)
#define GPMC_COFNIG_CS_PAGE_LENGTH_4        (0 << 23)
#define GPMC_COFNIG_CS_PAGE_LENGTH_8        (1 << 23)
#define GPMC_CONFIG_CS_PAGE_LENGTH_16       (2 << 23)
#define GPMC_CONFIG_CS_WAIT_READ        (1<<22)
#define GPMC_CONFIG_CS_WAIT_WRITE       (1<<21)
#define GPMC_CONFIG_CS_WAIT_TIME        (0x3<<18)
#define GPMC_CONFIG_CS_WAIT_TIME_ZERO       (0<<18)
#define GPMC_CONFIG_CS_WAIT_TIME_1CLK       (1<<18)
#define GPMC_CONFIG_CS_WAIT_TIME_2CLK       (2<<18)
#define GPMC_CONFIG_CS_WAIT_PIN         (0x3<<16)
#define GPMC_CONFIG_CS_WAIT_PIN_WAIT0       (0<<16)
#define GPMC_CONFIG_CS_WAIT_PIN_WAIT1       (1<<16)
#define GPMC_CONFIG_CS_DEVICE_SIZE      (0x3<<12)
#define GPMC_CONFIG_CS_DEVICE_8BIT          (0<<12)
#define GPMC_CONFIG_CS_DEVICE_16BIT         (1<<12)
#define GPMC_CONFIG_CS_DEVICE_TYPE      (0x3<<10)
#define GPMC_CONFIG_CS_DEVICE_NOR           (0<<10)
#define GPMC_CONFIG_CS_DEVICE_NAND          (2<<10)
#define GPMC_CONFIG_CS_MUXADDDATA       (0x3<<8)
#define GPMC_CONFIG_CS_MUX_NONE             (0<<8)
#define GPMC_CONFIG_CS_MUX_AAD_PROTO        (1<<8)
#define GPMC_CONFIG_CS_MUX_ADDR_DATA_DEVICE (2<<8)
#define GPMC_CONFIG_CS_TIME_GRANULARITY (1<<4)
#define GPMC_CONFIG_CS_FCLK_DIVIDER     (0x3<<0)
#define GPMC_CONFIG_CS_FCLK_NO_DIV          (0<<0)
#define GPMC_CONFIG_CS_FCLK_DIV2            (1<<0)
#define GPMC_CONFIG_CS_FCLK_DIV4            (2<<0)
#define GPMC_CONFIG_CS_FCLK_DIV8            (3<<0)

/* GPMC_CONFIG2 for signal timing parameters */
#define GPMC_CONFIG_CS_WR_OFF_TIME(_x_) (((_x_) & 0x1f) << 16)
#define GPMC_CONFIG_CS_RD_OFF_TIME(_x_) (((_x_) & 0x1f) << 8)
#define GPMC_CONFIG_CS_ON_TIME(_x_)     (((_x_) & 0xf)  << 0)
#define GPMC_CONFIG_CS_EXTRA_DELAY      (1 << 7)

/* GPMC_CONFIG3 for ADV# signal timing */
#define GPMC_CONFIG_ADV_AAD_WR_OFF_TIME(_x_) (((_x_) & 0x7) << 28)
#define GPMC_CONFIG_ADV_AAD_RD_OFF_TIME(_x_) (((_x_) & 0x7) << 24)
#define GPMC_CONFIG_ADV_WR_OFF_TIME(_x_)    (((_x_) & 0x1f) << 16)
#define GPMC_CONFIG_ADV_RD_OFF_TIME(_x_)    (((_x_) & 0x1f) << 8)
#define GPMC_CONFIG_ADV_EXTRA_DELAY         (1<<7)
#define GPMC_CONFIG_ADV_AAD_ON_TIME(_x_)    (((_x_) & 0x7) << 4)
#define GPMC_CONFIG_ADV_ON_TIME(_x_)        (((_x_) & 0xf) << 0)


/* GPMC_CONFIG3 for WE# and OE# signal timing */
#define GPMC_CONFIG_WE_OFF_TIME(_x_)        (((_x_) & 0x1f) << 24)
#define GPMC_CONFIG_WE_EXTRA_DELAY          (1<<23)
#define GPMC_CONFIG_WE_ON_TIME(_x_)         (((_x_) & 0xf) << 16)
#define GPMC_CONFIG_OE_AAD_OFF_TIME(_x_)    (((_x_) & 0x7) << 13)
#define GPMC_CONFIG_OE_OFF_TIME(_x_)        (((_x_) & 0x1f) << 8)
#define GPMC_CONFIG_OE_EXTRA_DELAY          (1<<7)
#define GPMC_CONFIG_OE_AAD_ON_TIME(_x_)     (((_x_) & 0x7) << 4)
#define GPMC_CONFIG_OE_ON_TIME(_x_)         (((_x_) & 0xf) << 0)

/* GPMC_CONFIG4 for read access and cycle timing */
#define GPMC_CONFIG_PAGE_BURST_TIME(_x_)    (((_x_) & 0xf) << 24)
#define GPMC_CONFIG_RD_ACCESS_TIME(_x_)     (((_x_) & 0x1f) << 16)
#define GPMC_CONFIG_WR_CYCLE_TIME(_x_)      (((_x_) & 0x1f) << 8)
#define GPMC_CONFIG_RD_CYCLE_TIME(_x_)      (((_x_) & 0x1f) << 0)

#define GPMC_CONFIG_WR_ACCESS_TIME(_x_)     (((_x_) & 0x1f) << 24)
#define GPMC_CONFIG_WR_DATA_MUX(_x_)        (((_x_) & 0xf) << 16)
#define GPMC_CONFIG_CYCLE_DELAY(_x_)        (((_x_) & 0xf) << 8)
#define GPMC_CONFIG_CYCLE_SAME_CS           (1<<7)
#define GPMC_CONFIG_CYCLE_DIFF_CS           (1<<6)
#define GPMC_COFNIG_BUS_TURNAROUND(_x_)     (((_x_) & 0xf) << 0)

#define GPMC_COFNIG_MASK_ADDRESS        (0xf<<8)
#define GPMC_CONFIG_MASK_ADDRESS_256MB      (0x0<<8)
#define GPMC_CONFIG_MASK_ADDRESS_128MB      (0x8<<8)
#define GPMC_CONFIG_MASK_ADDRESS_64MB       (0xc<<8)
#define GPMC_CONFIG_MASK_ADDRESS_32MB       (0xe<<8)
#define GPMC_CONFIG_MASK_ADDRESS_16MB       (0xf<<8)
#define GPMC_CONFIG_CS_VALID            (1<<6)
#define GPMC_CONFIG_BASE_ADDRESS(_x_)   (((_x_) & 0x3f)<<0)

#endif /* _FPGA_GPMC_H */
