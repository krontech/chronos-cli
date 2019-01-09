#!/usr/bin/python
from distutils.core import setup, Extension

pychronos = Extension('pychronos',
                      include_dirs = ['lib'],
                      depends = ['pychronos/pychronos.h',
                                 'lib/fpga.h',
                                 'lib/lux1310.h'],
                      sources = ['pychronos/module.c',
                                 'pychronos/registers.c',
                                 'pychronos/lux1310.c'])

setup (name = 'PyChronos',
       version = '0.3.1',
       description = 'Python bindings for the Chronos High Speed Camera',
       author = 'Owen Kirby',
       author_email = 'oskirby@gmail.com',
       url = 'https://github.com/krontech/chronos-cli',
       ext_modules = [pychronos])

