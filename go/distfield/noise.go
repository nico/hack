package noise

import (
	"./math32"
	"math/rand"
	"./vec"
)

var P [256]uint8
var G [256]float32

func lerp(a, min, max float32) float32 { return min*(1-a) + max*a }

func myRandomMagic(i, j, k int) float32 {
	// Old, non-improved perlin noise
	return G[uint8(i)+P[uint8(j)+P[uint8(k)]]]
}

func iGetIntegerAndFractional(v float32) (int, float32) {
	i, f := math32.Modff(v)
	return int(i), f
}

func Noise3f(p vec.Vec3) float32 {
	i, u := iGetIntegerAndFractional(p.X + 100000)
	j, v := iGetIntegerAndFractional(p.Y + 100000)
	k, w := iGetIntegerAndFractional(p.Z + 100000)

	u = u * u * u * (u*(u*6-15) + 10)
	v = v * v * v * (v*(v*6-15) + 10)
	w = w * w * w * (w*(w*6-15) + 10)

	a := myRandomMagic(i+0, j+0, k+0)
	b := myRandomMagic(i+1, j+0, k+0)
	c := myRandomMagic(i+0, j+1, k+0)
	d := myRandomMagic(i+1, j+1, k+0)
	e := myRandomMagic(i+0, j+0, k+1)
	f := myRandomMagic(i+1, j+0, k+1)
	g := myRandomMagic(i+0, j+1, k+1)
	h := myRandomMagic(i+1, j+1, k+1)

	return lerp(w, lerp(v, lerp(u, a, b), lerp(u, c, d)),
		lerp(v, lerp(u, e, f), lerp(u, g, h)))
}

func dnoise3f(p vec.Vec3) (float32, vec.Vec3) {
	i, u := iGetIntegerAndFractional(p.X)
	j, v := iGetIntegerAndFractional(p.Y)
	k, w := iGetIntegerAndFractional(p.Z)

	du := 30 * u * u * (u*(u-2) + 1)
	dv := 30 * v * v * (v*(v-2) + 1)
	dw := 30 * w * w * (w*(w-2) + 1)

	u = u * u * u * (u*(u*6-15) + 10)
	v = v * v * v * (v*(v*6-15) + 10)
	w = w * w * w * (w*(w*6-15) + 10)

	a := myRandomMagic(i+0, j+0, k+0)
	b := myRandomMagic(i+1, j+0, k+0)
	c := myRandomMagic(i+0, j+1, k+0)
	d := myRandomMagic(i+1, j+1, k+0)
	e := myRandomMagic(i+0, j+0, k+1)
	f := myRandomMagic(i+1, j+0, k+1)
	g := myRandomMagic(i+0, j+1, k+1)
	h := myRandomMagic(i+1, j+1, k+1)

	k0 := a
	k1 := b - a
	k2 := c - a
	k3 := e - a
	k4 := a - b - c + d
	k5 := a - c - e + g
	k6 := a - b - e + f
	k7 := -a + b + c - d + e - f - g + h

	v0 := k0 + k1*u + k2*v + k3*w + k4*u*v + k5*v*w + k6*w*u + k7*u*v*w
	v1 := du * (k1 + k4*v + k6*w + k7*v*w)
	v2 := dv * (k2 + k5*w + k4*u + k7*w*u)
	v3 := dw * (k3 + k6*u + k5*v + k7*u*v)
	return v0, vec.Vec3{v1, v2, v3}
}

func Fbm(p vec.Vec3) float32 {
	var f float32 = 0
	var w float32 = 0.5
	const IOCT = 8
	for i := 0; i < IOCT; i++ {
		f += w * Noise3f(p)
		w *= 0.5
		p.X *= 2
		p.Y *= 2
		p.Z *= 2
	}
	return math32.Fabsf(f)
}

func init() {
	for i, v := range rand.Perm(len(P)) {
		P[i] = uint8(v)
		G[i] = 2*rand.Float32() - 1
		/*fmt.Printf("%d %f\n", P[i], G[i])*/
	}
}
