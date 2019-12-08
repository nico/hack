from __future__ import print_function
import fileinput

w, h = 25, 6
line = list(fileinput.input())[0].rstrip()
layers = [line[i:i+w*h] for i in range(0, len(line), w*h)]
l = min(layers, key=lambda x: x.count('0'))
print(l.count('1') * l.count('2'))
