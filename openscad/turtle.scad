$fn=100;

module cells() {
  r = sqrt(3 / (1 + 2*sqrt(5)/5) - 1);
  d = 0.1;
  for (j = [0, 180]) rotate([0, j, 0])
    for (i = [0:4]) rotate([0, 0, i * 360 / 5])
      for (k = [0, 1])
        rotate([0, -atan(r) - 2*k*atan(r*cos(180/3)), 0])
        rotate([0, 0, 180*k])
        rotate([0, 0, 30])
          translate([0, 0, d]) cylinder(1, 0, r/sqrt(3), $fn=6);

  y = cos(atan(r)) + r/2 * sin(atan(r));
  nr = sqrt(1 + r*r/4 - y*y) / cos(36);
  translate([0, 0, d]) cylinder(y, 0, nr, $fn=5);
  for (i = [0:4]) rotate ([0, 0, i * 72])
  rotate([0, 2*atan(r*cos(180/5)), 0])
  rotate([0, 0, 180]) translate([0, 0, d]) cylinder(y, 0, nr, $fn=5);
}

intersection() {
  union() {
    // shell
    union() {
      translate([0, 0, -3]) sphere(10, center=true);
      intersection() {
        translate([0, 0, -3])  scale(10) cells();
        difference() {
          translate([0, 0, -3]) sphere(10.3, center=true);
          translate([0, 0, -3]) sphere(9.8, center=true);
        }
      }
   }

    // head
    translate([10, 0, -1]) sphere(4, center=true);
    for (my = [-1, 1]) scale([1, my, 1])
    translate([11.5, 1.8, 1.8]) sphere(0.7, center=true);


    // tail
    rotate([0, -90, 0]) cylinder(h=12, r=5, r2=0);

    // feet
    for (mx = [-1, 1]) for (my = [-1, 1]) scale([mx, my, 1])
    translate([6.5, 6.5, -1]) sphere(2, center=true);
  }
  translate([0, 0, 500]) cube(1000, 1000, 1000, center=true);
}
