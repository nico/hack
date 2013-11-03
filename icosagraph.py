# prints a graph that when 3-colored can be used to build an icosahedron
# where each face is extruded upwards at an additional center vertex at each
# face, so that every of the 3 faces of every face-pyramid has 3 distinct colors
# and one color on the opposite sides of neighboring face-pyramid faces match.

N = 30
E = N * 4 / 2

from collections import namedtuple


Vertex = namedtuple('Vertex', ['id', 'color', 'neighbors'])

v = [Vertex(i, -1, set()) for i in range(N)]

def face(a, b, c):
  v[a].neighbors.add(b); v[a].neighbors.add(c)
  v[b].neighbors.add(a); v[b].neighbors.add(c)
  v[c].neighbors.add(a); v[c].neighbors.add(b)

face(0, 1, 2)

face(0, 3, 4)
face(1, 5, 6)
face(2, 7, 8)

face(4, 9, 12)
face(5, 9, 13)
face(6, 10, 14)
face(7, 10, 15)
face(8, 11, 16)
face(3, 11, 17)

face(29 - 4, 29 - 9, 16)
face(29 - 5, 29 - 9, 15)
face(29 - 6, 29 - 10, 14)
face(29 - 7, 29 - 10, 13)
face(29 - 8, 29 - 11, 12)
face(29 - 3, 29 - 11, 17)

face(29 - 0, 29 - 3, 29 - 4)
face(29 - 1, 29 - 5, 29 - 6)
face(29 - 2, 29 - 7, 29 - 8)

face(29 - 0, 29 - 1, 29 - 2)

for p in v:
  print p
