from __future__ import print_function
import fileinput

w, h = 25, 6
line = list(fileinput.input())[0].rstrip()
pic = ['2'] * (w*h)
for i in range(0, len(line)):
    if pic[i % (w*h)] == '2':
        pic[i % (w*h)] = line[i]
for y in range(0, w*h, w):
    print(''.join(pic[y:y + w]).replace('0', ' '))
