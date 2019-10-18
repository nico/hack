# From atreus-cherry-3mm.eps, in 1/72th of an inch:
pts = [
 [ 668.909, 19.578 ],
 [ 710.41, 262.184 ],
 [ 448.77, 314.531 ],
 [ 407.5, 14.605 ],

 [ 61.055, 19.578 ],
 [ 19.551, 262.184 ],
 [ 281.191, 314.531 ],
 [ 322.461, 14.605 ],
]
def to_mm(p): return [xy / 72.0 * 25.4 for xy in p]

# From atreus_case.scad, removing everything and putting just
# `screw_holes(0.0001);` at the bottom and exporting to svg, in mm:
# (readme mentions that the top screwholes don't match prod)
pts2 = [
  [ 7.23572, -102.221 ],
  [ -7.23555, -102.222 ],
  [ 107.209, -102.221 ],
  [ -107.209, -102.221 ],
  [ 121.642, -20.3702 ],
  [ -121.641, -20.3703 ],
  [ 21.6, 0.316116 ],
  [ -21.5998, 0.316055 ],
]

def print_canon(pts):
  pts = sorted(pts)
  for pt in pts:
    print pt[0] - pts[0][0], pt[1] - pts[0][1]

print_canon(map(to_mm, pts))
print
print_canon(pts2)
