from mpl_toolkits.mplot3d import Axes3D
from matplotlib import cm
from matplotlib.ticker import LinearLocator, FormatStrFormatter
import matplotlib.pyplot as plt
import numpy as np
import sys 

import re

import pdb

from numpy import linspace
from scipy.interpolate import interp2d

fig = plt.figure()
ax = fig.add_subplot(111, projection='3d')

X = []
Y = []
Z = dict()

for l in sys.stdin.readlines():
    if re.match('^[0-9. ]', l):
        e = l.split()
        if len(e)>=3:
            x = int(e[0])
            y = int(e[1])
            z = float(e[2])
            X.append(x)
            Y.append(y)
            Z[(x,y)] = z

print X
print Y
print max(X)
print max(Y)

Yi = range(max(Y)+1)
Xi = range(max(X)+1)

Zi = [[ Z.get((x,y), 0) for x in Xi] for y in Yi]

xim, yim = np.meshgrid(Xi, Yi)

# Convert data
#ax.contourf(Xi, Yi, Zi)
ax.plot_surface(xim, yim, Zi, cmap=cm.coolwarm)

plt.show()

