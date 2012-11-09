package main

/*
go run mandel.go
*/

import (
    "fmt";
    "image";
    "image/color";
    "image/png";
    "os"
)

const ITERATIONS int = 20;

func lerp(a, min, max float32) float32 {
  return min*(1 - a) + max*a
}

func mandel(img *image.NRGBA) {
  for y := 0; y < img.Rect.Dy(); y++ {
    for x := 0; x < img.Rect.Dx(); x++ {
      zr := lerp(float32(x)/float32(img.Rect.Dx() - 1), -2, 0.5)
      zc := lerp(float32(y)/float32(img.Rect.Dy() - 1), -1.2, 1.2)

      zir, zic := float32(0.0), float32(0.0)
      c := 0
      for c < ITERATIONS && zir*zir + zic*zic < 4 {
        zir, zic = zir*zir - zic*zic + zr, 2*zir*zic + zc
        c++
      }

      if c < ITERATIONS {
        img.Set(x, y, color.NRGBA{ 255, 0, 0, 255 })
      } else {
        img.Set(x, y, color.NRGBA{ 255, 255, 0, 255 })
      }
    }
  }
}

func main() {
  var img *image.NRGBA = image.NewNRGBA(image.Rect(0, 0, 500, 480))
  mandel(img)

  wf, err := os.OpenFile("go_out.png", os.O_CREATE | os.O_WRONLY, 0666)
  if err != nil {
    fmt.Printf("Failed to create image file; err=%s\n", err)
    os.Exit(1)
  }
  err = png.Encode(wf, img)
  if err != nil {
    fmt.Printf("encoding image failed; err=%s\n", err)
    os.Exit(1)
  }
  wf.Close()
}
