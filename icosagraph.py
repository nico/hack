# prints a graph that when 3-colored can be used to build an icosahedron
# where each face is extruded upwards at an additional center vertex at each
# face, so that every of the 3 faces of every face-pyramid has 3 distinct colors
# and one color on the opposite sides of neighboring face-pyramid faces match.

N = 30
E = N * 4 / 2

from collections import namedtuple

UNK, R, G, B = -1, 0, 1, 2

class Vertex(object):
  def __init__(self, id, color, neighbors, possibleColors):
    self.id = id
    self.color = color
    self.neighbors = neighbors
    self.possibleColors = possibleColors

  def __repr__(self):
    return 'Vertex(id=%d, color=%d, possibleColors=%s)' % (
        self.id, self.color, self.possibleColors)

v = [Vertex(i, UNK, set(), set([R, G, B])) for i in range(N)]

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


def assign(i, c):
  v[i].color = c
  old = {}
  for j in v[i].neighbors:
    old[j] = v[j].possibleColors.copy()
    v[j].possibleColors.discard(c)
  return old

def unassign(i, old):
  v[i].color = UNK
  for k, oldcol in old.iteritems():
    v[k].possibleColors = oldcol

assign(0, R)
assign(1, G)
assign(2, B)

# Collect uncolored vertices
# If none, print solution
# Pick vertex with fewest color choices
# For every choice, assign color, recurse, backtrack
def color():
  unk = sorted([p for p in v if p.color == UNK],
               key=lambda p: len(p.possibleColors))

  if len(unk) == 0:
    print 'solution found:'
    for p in v:
      print p
    return

  for c in unk[0].possibleColors.copy():
    old = assign(unk[0].id, c)
    color()
    unassign(unk[0].id,old)

color()
