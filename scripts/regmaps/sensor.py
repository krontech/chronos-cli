import pychronos

class sensor(pychronos.fpgamap):
    """Return a new map of the FPGA sensor register space.
    
    Return a new map of the FPGA sensor register space. This map provides
    structured read and write access to the registers which control SCI
    communication to the image sensor, and timing parameters for the
    frame control signals.

    Parameters
    ----------
    offset : `int`, optional
        Starting offset of the register map (FPGA_SENSOR_BASE, by default)
    size : `int`, optional
        Length of the register map (FPGA_MAP_SIZE, by default)
    """
    def __init__(self, offset=pychronos.FPGA_SENSOR_BASE, size=0x100):
        super().__init__(offset, size)

    def __regprop(offset, size, docstring):
        return property(fget=lambda self: self.regRead(offset, size),
                        fset=lambda self, value: self.regWrite(offset, size, value),
                        doc = docstring)
    
    # Sensor control registers.
    control =       __regprop(0x00, 2, "Control Register")
    clkPhase =      __regprop(0x04, 2, "Clock Phase Register")
    syncToken =     __regprop(0x08, 2, "Sync Token Register")
    dataCorrect =   __regprop(0x0C, 4, "Data Correct Status Register")
    fifoStart =     __regprop(0x10, 2, "FIFO Starting Threshold Register")
    fifoStop =      __regprop(0x14, 2, "FIFO Stop Threshold Register")
    framePeriod =   __regprop(0x1C, 4, "Frame Period Register")
    intTime =       __regprop(0x20, 4, "Integration Time Register")
    # Serial Communication Interface registers.
    sciControl =    __regprop(0x24, 2, "SCI Control Register")
    sclAddress =    __regprop(0x28, 2, "SCI Address Register")
    sciDataLen =    __regprop(0x2C, 2, "SCI Data Length Register")
    sciFifoWrite =  __regprop(0x30, 2, "SCI Write FIFO Register")
    sciFifoRead =   __regprop(0x34, 2, "SCI Read FIFO Register")
    startDelay =    __regprop(0x68, 2, "ABN/exposure start delay")
    linePeriod =    __regprop(0x6C, 2, "Horizontal line period")
    # Sensor control bits
    RESET       =  (1 << 0)
    EVEN_TIMESLOT = (1 << 1)
    # SCI control bits
    SCI_RUN     = (1 << 0)
    SCI_RW      = (1 << 1)
    SCI_FULL    = (1 << 2)
    SCI_RESET   = (1 << 15)
