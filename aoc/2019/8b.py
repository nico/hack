from __future__ import print_function
import fileinput

w, h = 25, 6
line = list(fileinput.input())[0].rstrip()
pic = ['2'] * (w*h)
for d, s in zip(range(0, w*h) * (len(line)/w/h), range(0, len(line))):
    if pic[d] == '2':
        pic[d] = line[s]
for y in range(0, w*h, w):
    print(''.join(pic[y:y + w]).replace('0', ' '))
