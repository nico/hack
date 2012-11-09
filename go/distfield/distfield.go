package main

import (
	"fmt"
	"image"
	"image/png"
	"./math32"
	"os"
	"./noise"
	"runtime"
	"./vec"
)

type Object interface {
	Dist(p vec.Vec3) float32
	Color() [3]float32
}

type Plane struct{}

func (Plane) Dist(p vec.Vec3) float32 { return distanceToPlane(p) }

func (Plane) Color() [3]float32 { return [3]float32{1, 1, 0.8} }

type Monster struct{}

func (Monster) Dist(p vec.Vec3) float32 { return distanceToMonster(p) }

func (Monster) Color() [3]float32 { return [3]float32{1, 0.8, 0.8} }


// Distance from sphere around origin
func distanceToSphere(p vec.Vec3) float32 { return p.Length() - 1.3 }

func distanceToCube(p vec.Vec3) float32 {
	return math32.Fmaxf(math32.Fabsf(p.X),
		math32.Fmaxf(math32.Fabsf(p.Y), math32.Fabsf(p.Z))) - 1.0
}

func distanceToPlane(p vec.Vec3) float32 { return p.Y }

func distanceToWarpCube(p vec.Vec3) float32 {
	return math32.Fminf(distanceToSphere(p), distanceToCube(p.Rotz(p.Z/2)))
}

func distanceToWarpCubeField(p vec.Vec3) float32 {
	// -2 to move warpcube away from origin, mod to tile it
	// FIXME: why does this look weird if tile size isn't 4?
	p.X = math32.Fmodf(p.X+10000*4, 4) - 2
	p.Z = math32.Fmodf(p.Z+10000*4, 4) - 2
	return math32.Fminf(distanceToSphere(p), distanceToCube(p.Rotz(p.Z/2)))
}

func distanceToXAxis(p vec.Vec3) float32 {
	if p.X < 0 {
		return 50
	}
	p.X = 0
	return p.Length() - 0.3
}

func distanceToTentacles(p vec.Vec3) float32 {
	rr := p.Trans(0, -p.Y, 0).Length()
	if rr >= 10 {
		return 50
	}
	/*rr := p.Trans(0, 0, -p.Z).Length()*/
	distance := float32(100000000000)
	for i := 0; i < 6; i++ {
		q := p.Roty(2.0*3.14159265*float32(i)/6.0 +
			0.04*rr*noise.Noise3f(vec.Vec3{0.4 * rr, 6.3 * float32(i), 0}))
		/*q.Y += 1.6 * rr * math32.Expf(-0.2 * rr)*/
		q.Y -= 1.0 * math32.Expf(-0.2*rr*rr)
		distance = math32.Fminf(distance, distanceToXAxis(q))
	}
	return distance
}

func lerp(a, min, max float32) float32 { return min*(1-a) + max*a }

func saturate(x float32) float32 { return math32.Fmaxf(0, math32.Fminf(x, 1)) }

func smoothstep(x, a, b float32) float32 {
	x = saturate((x - a) / (b - a))
	return x * x * (3 - 2*x)
}

func distanceToMonster(p vec.Vec3) float32 {
	dist1 := distanceToSphere(p.Trans(0, -2.5, 0))
	dist2 := distanceToTentacles(p.Scale(0.8).Trans(0, -0.3, 0))
	bfact := smoothstep(p.Trans(0, -2.5, 0).Length()/2, 0, 1)
	return lerp(bfact, dist1, dist2)
	/*return math32.Fminf(dist1, dist2)*/
}

func distanceFieldObject(p vec.Vec3) (float32, Object) {
	// This will get more complicated later.
	p.Z = p.Z + 12 // Move camera back
	p = p.Rotx(-0.5).Roty(0.7)
	p.Y += 1 // Move rotated camera up

	objects := []Object{Plane{}, Monster{}}
	var minObj Object = nil
	minDist := float32(10000)
	for _, obj := range objects {
		d := obj.Dist(p)
		if d < minDist {
			minDist = d
			minObj = obj
		}
	}
	return minDist, minObj
}

func distanceField(p vec.Vec3) float32 {
	f, _ := distanceFieldObject(p)
	return f
}

func fieldNormalAt(f func(vec.Vec3) float32, p vec.Vec3) vec.Vec3 {
	const E = 0.02
	r := vec.Vec3{
		f(p.Trans(E, 0, 0)) - f(p.Trans(-E, 0, 0)),
		f(p.Trans(0, E, 0)) - f(p.Trans(0, -E, 0)),
		f(p.Trans(0, 0, E)) - f(p.Trans(0, 0, -E))}
	return r.Normalized()
}

func ambientOcclusion(start, normal vec.Vec3) float32 {
	const DELTA = 0.3
	a := float32(0.0)
	k := float32(2.0)
	for i := 1; i < 6; i++ {
		start.X += normal.X * DELTA
		start.Y += normal.Y * DELTA
		start.Z += normal.Z * DELTA
		a += (1.0 / k) * (float32(i)*DELTA - distanceField(start))
		k *= 2.0
	}
	return 1.0 - a
}

func render(img *image.NRGBA, startY, limitY int, c chan int) {
	for y := startY; y < limitY; y++ {
		for x := 0; x < img.Width(); x++ {
			p := vec.Vec3{float32(float32(x)/float32(img.Width()-1) - 0.5),
				float32(-(float32(y)/float32(img.Height()-1) - 0.5)),
				-float32(0.8)}
			d := p.Normalized()

			dist, obj := distanceFieldObject(p)
			step := 0
			for ; step < 63 && dist > -0.005*p.Z; step++ {
				// Error decreases 1/d, so keep min step proportional to d for constant screen-space error
				p = p.Add(d, math32.Fmaxf(-0.005*p.Z, dist))
				if p.Z < -60 {
					break
				}
				dist, obj = distanceFieldObject(p)
			}

			if dist <= -0.01*p.Z {
				n := fieldNormalAt(distanceField, p)
				// compute ambient occlusion before doing bump mapping
				ao := math32.Fminf(ambientOcclusion(p, n), 1.0)

				dnoise := fieldNormalAt(noise.Fbm, p)
				const N = 0.3
				n = n.Vtrans(dnoise.Scale(N)).Normalized()

				distfade := math32.Expf(0.07 * (p.Z + 6))
				/*lit := float32(1.0)*/
				lit := math32.Fmaxf(0, n.Z)
				/*tex := float32(0.8)*/
				tex := (0.4*(math32.Sinf(2*(p.X+1.4*p.Y)+2*noise.Fbm(p))+1) + 0.2)
				v := tex * (lit*0.6 + 0.3) * (ao * ao * ao) * distfade

				col := obj.Color()
				img.Set(x, y, image.RGBAColor{
					/*uint8(math32.Fminf(float32(step)*4+255*v, 255)),*/
					uint8(255 * v * col[0]),
					uint8(255 * v * col[1]),
					uint8(255 * v * col[2]),
					/*uint8(255*ao),*/
					255})
			} else {
				/*img.Set(x, y, image.RGBAColor{uint8(step * 4), 0, 0, 255})*/
				img.Set(x, y, image.RGBAColor{0, 0, 0, 255})
			}
		}
	}
	c <- 1
}


func main() {
	var img *image.NRGBA = image.NewNRGBA(500, 500)

	const NCPU = 2
	runtime.GOMAXPROCS(NCPU)
	c := make(chan int, NCPU)
	for i := 0; i < NCPU; i++ {
		go render(img, i*img.Height()/NCPU, (i+1)*img.Height()/NCPU, c)
	}
	for i := 0; i < NCPU; i++ {
		<-c // wait for completion of all goroutines
	}

	wf, _ := os.Open("go_out.png", os.O_CREATE|os.O_WRONLY, 0666)
	defer wf.Close()
	png.Encode(wf, img)
	fmt.Printf("done.\n")
}
