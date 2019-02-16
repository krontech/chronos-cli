import pychronos
raw = pychronos.fpgamap(0x0604, 8)
fpgaVersion = '%d.%d' % (raw.mem32[0], raw.mem32[1])
print(fpgaVersion)
