package main

import "math"
import "fmt"

func main() {
  var f float32 = 4
  var g float32 = 4
  var h float32 = float32(math.Min(float64(f), float64(g)))
  fmt.Printf("hi %f\n", h)
}
