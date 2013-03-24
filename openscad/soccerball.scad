r = sqrt(3 / (1 + 2*sqrt(5)/5) - 1);
module f(n, m) {  // 5, 3: icosahedron; 3, 5: dodecahedron
for (j = [0, 180]) rotate([0, j, 0])
  for (i = [0:(n-1)]) rotate([0, 0, i * 360 / n])
    for (k = [0, 1]) rotate([0, -atan(r) - 2*k*atan(r*cos(180/m)), 0])
      rotate([0, 0, 180*k]) cylinder(1, 0, r, $fn=m);
}
intersection() {
  f(5, 3);
  rotate([0, 0, 36]) rotate([0, 36, 0]) f(3, 5);
}
