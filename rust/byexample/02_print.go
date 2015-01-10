// go run 02_print.go
package main

import "fmt"

func main() {
	fmt.Print("January has ")
	fmt.Printf("%d days\n", 31)

	fmt.Printf("%s, this is %s. %s, this is %s\n", "Alice", "Bob", "Bob", "Alice")

	fmt.Printf("%s %s %s\n", "the quick brown fox", "jumpst", "over the lazy dog")

	fmt.Printf("%d of %b people know binary\n", 1, 2)

	fmt.Printf("My name is {0}, {1} {0}\n", "Bond") // Runtime error; if you're lucky r might make fun of you.
}
