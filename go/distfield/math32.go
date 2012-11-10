package math32

import "math"

func Minf(a, b float32) float32 {
	if a < b {
		return a
	}
	return b
}

func Maxf(a, b float32) float32 {
	if a > b {
		return a
	}
	return b
}

func Fabsf(a float32) float32 { return float32(math.Abs(float64(a))) }

func Cosf(a float32) float32 { return float32(math.Cos(float64(a))) }

func Sinf(a float32) float32 { return float32(math.Sin(float64(a))) }

func Sincosf(a float32) (float32, float32) {
	s64, c64 := math.Sincos(float64(a))
	return float32(s64), float32(c64)
}

func Sqrtf(a float32) float32 { return float32(math.Sqrt(float64(a))) }

func Modff(a float32) (float32, float32) {
	i64, f64 := math.Modf(float64(a))
	return float32(i64), float32(f64)
}

func Modf(x, y float32) float32 { return float32(math.Mod(float64(x), float64(y))) }

func Expf(x float32) float32 { return float32(math.Exp(float64(x))) }
