package vec

import "./math32"

type Vec3 struct {
	X, Y, Z float32
}

func (p Vec3) Length() float32 { return math32.Sqrtf(p.X*p.X + p.Y*p.Y + p.Z*p.Z) }

func (p Vec3) Normalized() Vec3 {
	len := p.Length()
	return Vec3{p.X / len, p.Y / len, p.Z / len}
}

func (p *Vec3) Add(dir Vec3, amnt float32) Vec3 {
	return Vec3{p.X + dir.X*amnt, p.Y + dir.Y*amnt, p.Z + dir.Z*amnt}
}

func (p Vec3) Trans(X, Y, Z float32) Vec3 { return Vec3{p.X + X, p.Y + Y, p.Z + Z} }

func (p Vec3) Vtrans(d Vec3) Vec3 { return Vec3{p.X + d.X, p.Y + d.Y, p.Z + d.Z} }

func (p Vec3) Scale(s float32) Vec3 { return Vec3{s * p.X, s * p.Y, s * p.Z} }

func (p Vec3) Rotx(amnt float32) Vec3 {
	s, c := math32.Sincosf(amnt)
	return Vec3{p.X,
		-p.Z*s + p.Y*c,
		-p.Z*c - p.Y*s}
}

func (p Vec3) Roty(amnt float32) Vec3 {
	s, c := math32.Sincosf(amnt)
	return Vec3{p.X*c + p.Z*s,
		p.Y,
		p.X*s - p.Z*c}
}

func (p Vec3) Rotz(amnt float32) Vec3 {
	s, c := math32.Sincosf(amnt)
	return Vec3{p.X*c - p.Y*s,
		p.X*s + p.Y*c,
		p.Z}
}

func (p Vec3) Dot(b Vec3) float32 { return p.X*b.X + p.Y*b.Y + p.Z*b.Z }
