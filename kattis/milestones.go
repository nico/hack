package main

import "fmt"
import "math/big"
import "sort"

// Go apparently has no built-in way to sort an []int64.
type Int64Slice []int64
func (p Int64Slice) Len() int           { return len(p) }
func (p Int64Slice) Less(i, j int) bool { return p[i] < p[j] }
func (p Int64Slice) Swap(i, j int)      { p[i], p[j] = p[j], p[i] }

func main() {
  // Read.
  var m, n int; fmt.Scan(&m, &n)
  ms := make([]int64, m); for i := 0; i < m; i++ { fmt.Scan(&ms[i]) }
  ns := make([]int64, n); for j := 0; j < n; j++ { fmt.Scan(&ns[j]) }

  // Compute.
  for i := 0; i < m - 1; i++ { ms[i] = ms[i + 1] - ms[i] }
  for j := 0; j < n - 1; j++ { ns[j] = ns[j + 1] - ns[j] }
  // Does go have a set type? This is just a set of keys, the values will
  // always be `true`.
  candidates := make(map[int64]bool)
  for j := 0; j < (n - 1) - (m - 1) + 1; j++ {
    speed := big.NewRat(ns[j], ms[0])
    is_match := true
    for i := int(1); i < m - 1; i++ {
      if speed.Cmp(big.NewRat(ns[j + i], ms[i])) != 0 { is_match = false }
    }
    if is_match { candidates[ns[j]] = true }
  }

  // Print.
  // Saying `sorted(map.keys())` is apparently 3 lines in Go (plus the
  // Int64Slice interface above if it's a int64 array).
  keys := make(Int64Slice, 0, len(candidates))
  for k := range candidates { keys = append(keys, k) }
  sort.Sort(keys)

  fmt.Printf("%d\n", len(keys))
  for i, k := range keys {
    if i > 0 { fmt.Printf(" ") }
    fmt.Printf("%d", k)
  }
  fmt.Printf("\n")
}
