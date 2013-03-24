$fn=100;

module hollowcyl(r, d) {
  difference() {
    rotate([0, 0, 30]) cylinder(h=20, r=r, $fn=6, center=true);
    rotate([0, 0, 30]) cylinder(h=21, r=r-d, $fn=6, center=true);
  }
}

module cells(r, d) {
  for (t = [-1, 0, 1])
    translate([t * (r * sqrt(3) - d), 0, 0])
      hollowcyl(r, d);

  for (s = [1, -1])
    scale([1, s, 1]) for (t = [-0.5, 0.5])
      translate([t * (r * sqrt(3) - d), -(r * 1.5 - d), 0])
        hollowcyl(r, d);
}


intersection() {
  union() {
    // shell
    difference() {
      translate([0, 0, -3]) sphere(10, center=true);
      intersection() {
        cells(4, 1);
        difference() {
          translate([0, 0, -3]) sphere(11, center=true);
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
