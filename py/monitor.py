#!/usr/bin/env python3

# IPC with Python
# let's prepend a string to a given shared memory segment

import ctypes, ctypes.util, sys
import struct
import numpy as np
import sysv_ipc
import matplotlib.pylab as plt
import matplotlib.animation as animation

from ctypes import string_at, byref, cast, sizeof, create_string_buffer, pointer, POINTER

def pack(ctype_instance):
    buf = string_at(byref(ctype_instance), sizeof(ctype_instance))
    return buf

def unpack(ctype, buf):
    cstring = create_string_buffer(buf)
    ctype_instance = cast(pointer(cstring), POINTER(ctype)).contents
    return ctype_instance

class DaqInfo(ctypes.Structure):
    _fields_=(
        ('nch', ctypes.c_uint32),
        ('batch', ctypes.c_uint32),
        ('nbatch', ctypes.c_uint32),
        ('init_magic', ctypes.c_uint32),
        ('fcenter', ctypes.c_float),
        ('bw', ctypes.c_float),
    )

# shared memory segment is identified by this key and is 10 megs in size
key_info = 1234
key_payload=1235

raw_daq_info=sysv_ipc.SharedMemory(key_info)
daq_info=unpack(DaqInfo, raw_daq_info.read())

fcenter=daq_info.fcenter
bw=daq_info.bw
nch=daq_info.nch
raw_data=sysv_ipc.SharedMemory(key_payload)
buffer=np.frombuffer(raw_data, dtype=np.float32).reshape((-1, nch))
fig = plt.figure()

rate=bw
dt=1/bw*nch
fmin=(fcenter-bw/2)/1e6
fmax=(fcenter+bw/2)/1e6

im = plt.imshow(buffer, animated=True, aspect='auto', extent=[fmin, fmax, dt*buffer.shape[1], 0])
def updatefig(*args):
    im.set_array(buffer)
    return im,

ani = animation.FuncAnimation(fig, updatefig, interval=5, blit=True)
plt.show()

