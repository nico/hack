package main

import "fmt"
import "sort"

// Go apparently has no built-in way to sort an []uint64.
type UInt64Slice []uint64
func (p UInt64Slice) Len() int           { return len(p) }
func (p UInt64Slice) Less(i, j int) bool { return p[i] < p[j] }
func (p UInt64Slice) Swap(i, j int)      { p[i], p[j] = p[j], p[i] }

func main() {
  // Read.
  var m, n int; fmt.Scan(&m, &n)
  ms := make([]uint64, m); for i := 0; i < m; i++ { fmt.Scan(&ms[i]) }
  ns := make([]uint64, n); for j := 0; j < n; j++ { fmt.Scan(&ns[j]) }

  // Compute.
  for i := 0; i < m - 1; i++ { ms[i] = ms[i + 1] - ms[i] }
  for j := 0; j < n - 1; j++ { ns[j] = ns[j + 1] - ns[j] }
  // Does go have a set type? This is just a set of keys, the values will
  // always be `true`.
  candidates := make(map[uint64]bool)
  for j := 0; j < (n - 1) - (m - 1) + 1; j++ {
    // Morally this computes big.NewRat(ns[j], ms[0]) here and compares it to
    // big.NewRat(ns[j + i], ms[i]) below -- but math/big is too slow, so
    // use that we know that a uint128 will always fit the result.
    var a uint64 = ns[j]
    var b uint64 = ms[0]
    is_match := true
    for i := 1; i < m - 1 && is_match; i++ {
      var x uint64 = ns[j + i]
      var y uint64 = ms[i]
      // Want to compute (a / b) == (x / y) <=> a*y == x*b.  a, b, x, y are
      // uint64s though, and Go has no uint128 type. So say
      //   a = (a1 << 32) + a0 with a1, a0 being uint32s
      // and the same for b, x, y.  Then
      //   a*y <=> (a1*y1 << 64) + ((a0*y1 + a1*y0) << 32) + a0*y0
      //   where all products are 64-bit.

      const M = 0xffffffff

      // Compute ay0 (low 64 bit of the a * y), ay1 (high 64 bit)
      var a0 uint64 = a & M
      var a1 uint64 = (a >> 32)
      var y0 uint64 = y & M
      var y1 uint64 = (y >> 32)

      var s0 uint64 = (a0 * y0) >> 32
      var s1 uint64 = a1 * y0
      var s2 uint64 = a0 * y1
      var s3 uint64 = ((s0 + (s1 & M) + (s2 & M)) >> 32) & M +
                      (s1 >> 32) + (s2 >> 32) + a1 * y1

      var ay0 uint64 = a * y  // Truncates to lowest 64 bit, that's intended.
      var ay1 uint64 = s3

      // Compute xb0 (low 64 bit of the x * b), xb1 (high 64 bit)
      var x0 uint64 = x & M
      var x1 uint64 = (x >> 32)
      var b0 uint64 = b & M
      var b1 uint64 = (b >> 32)

      var t0 uint64 = (x0 * b0) >> 32
      var t1 uint64 = x1 * b0
      var t2 uint64 = x0 * b1
      var t3 uint64 = ((t0 + (t1 & M) + (t2 & M)) >> 32) & M +
                      (t1 >> 32) + (t2 >> 32) + x1 * b1

      var xb0 uint64 = x * b  // Truncates to lowest 64 bit, that's intended.
      var xb1 uint64 = t3

      if ay0 != xb0 || ay1 != xb1 {
        is_match = false
      }
    }
    if is_match { candidates[ns[j]] = true }
  }

  // Print.
  // Saying `sorted(map.keys())` is apparently 3 lines in Go (plus the
  // UInt64Slice interface above if it's a uint64 array).
  keys := make(UInt64Slice, 0, len(candidates))
  for k := range candidates { keys = append(keys, k) }
  sort.Sort(keys)

  fmt.Printf("%d\n", len(keys))
  for i, k := range keys {
    if i > 0 { fmt.Printf(" ") }
    fmt.Printf("%d", k)
  }
  fmt.Printf("\n")
}
