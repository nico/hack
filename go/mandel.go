package main

/*
6g mandel.go && 6l mandel.6 
./6.out
*/

import (
    "fmt";
    "image";
    "image/png";
    "os"
)

const ITERATIONS int = 20;

func lerp(a, min, max float) float {
  return min*(1 - a) + max*a
}

func mandel(img *image.NRGBA) {
  for y := 0; y < img.Height(); y++ {
    for x := 0; x < img.Width(); x++ {
      zr := lerp(float(x)/float(img.Width() - 1), -2, 0.5)
      zc := lerp(float(y)/float(img.Height() - 1), -1.2, 1.2)

      zir, zic := 0.0, 0.0
      c := 0
      for c < ITERATIONS && zir*zir + zic*zic < 4 {
        zir, zic = zir*zir - zic*zic + zr, 2*zir*zic + zc
        c++
      }

      if c < ITERATIONS {
        img.Set(x, y, image.RGBAColor{ 255, 0, 0, 255 })
      } else {
        img.Set(x, y, image.RGBAColor{ 255, 255, 0, 255 })
      }
    }
  }
}

func main() {
  var img *image.NRGBA = image.NewNRGBA(500, 480)
  mandel(img)

  wf, err := os.Open("go_out.png", os.O_CREATE | os.O_WRONLY, 0666)
  if err != nil {
    fmt.Printf("Failed to create image file; err=%2\n", err.String())
    os.Exit(1)
  }
  err = png.Encode(wf, img)
  if err != nil {
    fmt.Printf("encoding image failed; err=%s\n", err.String())
    os.Exit(1)
  }
  wf.Close()
}
