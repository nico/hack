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

  # Allegedly increases precision (energy loss after 500 steps is just 1.15%
  # instead of 5.54%):
  vx += 0.5 * eps * ax
  vy += 0.5 * eps * ay

  U = 0.5 * 1 * ((vx - 0.5 * eps * ax) ** 2 + (vy - 0.5 * eps * ay) ** 2)
  T = -1 / r
  E = U + T
  print E

  E0 = E

  for i in range(500):
    print "%f\t%f" % (x, y)
    x += eps * vx
    y += eps * vy
    r = (x*x + y*y) ** 0.5
    ax = -x / (r**3)
    ay = -y / (r**3)
    vx += eps * ax
    vy += eps * ay

    U = 0.5 * 1 * ((vx - 0.5 * eps * ax) ** 2 + (vy - 0.5 * eps * ay) ** 2)
    T = -1 / r
    E = U + T
    #print E

  print 'energy change: %f%%' % (100 * (E - E0) / E0)


if __name__ == '__main__':
  main()
