package math32

// Strawman tests for math32, to give me an opportunity to write a gotest.

import "testing"

var innumbers = [][2]float32{
  [2]float32{5.2, 3.5},
  [2]float32{-1.9, -10.4},
}
var outnumbers = []float32{
  3.5,
  -10.4,
}

func TestMinf(t *testing.T) {
  for i := 0; i < len(innumbers); i++ {
    a, b := innumbers[i][0], innumbers[i][1]
    if m := Fminf(a, b); m != outnumbers[i] {
      t.Errorf("Minf(%g, %g) = %g, want %g\n", a, b, m, outnumbers[i])
    }
  }
}
