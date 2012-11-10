distfield: distfield.6
	go tool 6l -o $@ $^
clean:
	rm -rf *.6

distfield.6: distfield.go math32.6 vec.6 noise.6
	go tool 6g distfield.go

math32.6: math32.go math32_test.go
	go tool 6g $^

vec.6: vec.go
	go tool 6g $^

noise.6: noise.go vec.6
	go tool 6g noise.go

# All that follows is required for gotest
testpackage-clean:
	rm -rf _test

testpackage: _test/math32.a

_test/math32.a: math32.6
	mkdir _test
	gopack grc $@ math32.6

importpath:
	echo math32

# Ideally, I'd use http://golang.org/doc/code.html#New_package , but that
# requires creating a ton of directories or renaming all my packages afaiu.
