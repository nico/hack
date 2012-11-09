package main

import "fmt"

type Int int
type String string

type Comparable interface {
  Equals(b interface{}) bool
}

func (i Int) Equals(b interface{}) bool {
  if j, ok := b.(Int); ok {
    return i == j
  }
  return false
}

func (i String) Equals(b interface{}) bool {
  if j, ok := b.(String); ok {
    return i == j
  }
  return false
}

func AssertEquals(a, b Comparable) {
  if !a.Equals(b) {
    fmt.Printf("zomg, expected %v got %v\n", a, b)
  }
}

type Foo struct {
  a int
}

var f = Foo{1}
var m = map[Foo] float {
  f: 5 }

func main() {
  var a, b Int = 3, 4
  var c String = "hi"
  AssertEquals(a, c)
  AssertEquals(b, c)
}

