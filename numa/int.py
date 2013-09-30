import math

def main():
  eps = 0.1

  x = 0.5
  y = 0
  vx = 0
  vy = 1.63
  r = (x*x + y*y) ** 0.5
  ax = -x / (r**3)
  ay = -y / (r**3)

  # Allegedly increases precision:
  vx += 0.5 * eps * ax
  vy += 0.5 * eps * ay

  for i in range(500):
    print "%f\t%f" % (x, y)
    x += eps * vx
    y += eps * vy
    r = (x*x + y*y) ** 0.5
    ax = -x / (r**3)
    ay = -y / (r**3)
    vx += eps * ax
    vy += eps * ay


if __name__ == '__main__':
  main()
