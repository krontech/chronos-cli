#include <stdio.h>
#include <unistd.h>
#include <stdint.h>

#include "fpga.h"

uint16_t
fpga_sci_read(struct fpga *fpga, uint8_t addr)
{
    uint16_t first;
    int i;

    /* Set RW, address and length. */
    fpga->sensor->sci_control |= SENSOR_SCI_CONTROL_RW_MASK;
    fpga->sensor->sci_address = addr;
    fpga->sensor->sci_datalen = 2;

    /* Start the transfer and then wait for completion. */
    fpga->sensor->sci_control |= SENSOR_SCI_CONTROL_RUN_MASK;
    first = fpga->sensor->sci_control & SENSOR_SCI_CONTROL_RUN_MASK;
    while (fpga->sensor->sci_control & SENSOR_SCI_CONTROL_RUN_MASK) {
        i++;
    }

    if (!first && (i != 0)) {
        fprintf(stderr, "fpga_sci_read: Read first busy was missed, address: 0x%02x\n", addr);
    }
    else if (i == 0) {
        fprintf(stderr, "fpga_sci_read: Read busy not detected, something probably wrong, address: 0x%02x\n", addr);
    }

    usleep(1000);
    return fpga->sensor->sci_fifo_data;
} /* fpga_sci_read */

void
fpga_sci_write(struct fpga *fpga, uint8_t addr, uint16_t value)
{
    uint16_t first;
    int i;

    /* Clear RW, and setup the transfer and fill the FIFO */
    fpga->sensor->sci_control &= ~SENSOR_SCI_CONTROL_RW_MASK;
    fpga->sensor->sci_address = addr;
    fpga->sensor->sci_datalen = 2;
    fpga->sensor->sci_fifo_addr = (value >> 8) & 0xff;
    fpga->sensor->sci_fifo_addr = (value >> 0) & 0xff;

    /* Start the transfer and then wait for completion. */
    fpga->sensor->sci_control |= SENSOR_SCI_CONTROL_RUN_MASK;
    first = fpga->sensor->sci_control & SENSOR_SCI_CONTROL_RUN_MASK;
    while (fpga->sensor->sci_control & SENSOR_SCI_CONTROL_RUN_MASK) {
        i++;
    }
    if (!first && (i != 0)) {
        fprintf(stderr, "fpga_sci_write: Write first busy was missed, address: 0x%02x\n", addr);
    }
    else if (i == 0) {
        fprintf(stderr, "fpga_sci_write: Busy not detected, something probably wrong, address: 0x%02x\n", addr);
    }
} /* fpga_sci_write */
