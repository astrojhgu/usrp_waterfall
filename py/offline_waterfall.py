#!/usr/bin/env python3

import numpy as np
import matplotlib.pylab as plt
import sys


nch=int(sys.argv[2])

data=np.fromfile(sys.argv[1], dtype=np.complex64).reshape((-1,nch))
data=np.fft.fftshift(np.fft.fft(data, axis=1), axes=1)

plt.imshow(np.abs(data)**2,aspect='auto')
plt.show()
