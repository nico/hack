# prints a graph that when 5-colored can be used to paint the faces of a
# dodecahedron in 5 colored triangles each so that triangles that share a
# pentagon edge have the same color, but no two triangles on the same pentagon
# do.

N = 30
E = N * 8 / 2

adj = [[0 for i in range(N)] for j in range(N)]

def face(a, b, c, d, e):
  adj[a][b] = adj[b][a] = 1
  adj[a][c] = adj[c][a] = 1
  adj[a][d] = adj[d][a] = 1
  adj[a][e] = adj[e][a] = 1
  adj[b][c] = adj[c][b] = 1
  adj[b][d] = adj[d][b] = 1
  adj[b][e] = adj[e][b] = 1
  adj[c][d] = adj[d][c] = 1
  adj[c][e] = adj[e][c] = 1
  adj[d][e] = adj[e][d] = 1

face(0, 1, 2, 3, 4)
face(0, 5, 9, 14, 15)
face(5, 4, 6, 17, 16)
face(6, 3, 7, 19, 18)
face(7, 2, 8 ,11, 10)
face(9, 1, 8, 12, 13)

face(13, 14, 23, 27, 22)
face(23, 28, 24, 16, 15)
face(24, 29, 20, 17, 18)
face(21, 25, 20, 19, 10)
face(21, 26, 22, 12, 11)
face(25, 26, 27, 28, 29)

#for l in adj:
  #print sum(l)  # should be 8
  #print l

from math import *
print 'graph dodecahedron {'

# 0 - 4
for i in range(5):
  y = -50 * cos(i * 2 * pi / 5)
  x = -50 * sin(i * 2 * pi / 5)
  print '  %d [pos="%f,%f"];' % (i, x, y)
# 5 - 9
for i in range(5):
  y =  100 * cos(i * 2 * pi / 5 + 6*pi/5)
  x = -100 * sin(i * 2 * pi / 5 + 6*pi/5)
  print '  %d [pos="%f,%f"];' % (i + 5, x, y)

# 10 - 19
for i in range(10):
  y =  150 * cos(i * 2 * pi / 10 + pi/10)
  x = -150 * sin(i * 2 * pi / 10 + pi/10)
  print '  %d [pos="%f,%f"];' % (i + 10, x, y)

# 20 - 24
for i in range(5):
  y =  255 * cos(i * 2 * pi / 5 - 2*pi/10)
  x = -255 * sin(i * 2 * pi / 5 - 2*pi/10)
  print '  %d [pos="%f,%f"];' % (i + 20, x, y)
# 25 - 29
for i in range(5):
  y = -350 * cos(i * 2 * pi / 5 + 10*pi/10)
  x =  350 * sin(i * 2 * pi / 5 + 10*pi/10)
  print '  %d [pos="%f,%f"];' % (i + 25, x, y)

for i in range(N):
  for j in range(i + 1, N):
    if adj[i][j] and not (i >= 25 and j >= 25):
      print '  %d -- %d;' % (i, j)
print '}'
