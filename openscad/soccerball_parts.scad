module cells() {
  r = sqrt(3 / (1 + 2*sqrt(5)/5) - 1);
  % for (j = [0, 180]) rotate([0, j, 0])
    for (i = [0:4]) rotate([0, 0, i * 360 / 5])
      for (k = [0, 1])
        rotate([0, -atan(r) - 2*k*atan(r*cos(180/3)), 0])
        rotate([0, 0, 180*k])
        rotate([0, 0, 30])
          translate([0, 0, 0.3]) cylinder(1, 0, r/sqrt(3), $fn=6);

  translate([0, 0, 0.3]) cylinder(1, 0, r/sqrt(3), $fn=5);
  for (i = [0:4]) rotate ([0, 0, i * 72])
  rotate([0, 2*atan(r*cos(180/5)), 0])
  rotate([0, 0, 180]) translate([0, 0, 0.3]) cylinder(1, 0, r/sqrt(3), $fn=5);
}

cells();
