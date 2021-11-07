import array
import collections
import struct
import sys


class Struct(object):
  def __init__(self, name, *args):
    def tostruct(s): return s.replace('o', 's').replace('z', 's')
    self.fmt = '<' + ''.join(map(tostruct, args[0::2]))
    # XXX why is checksum space-terminated
    octint = lambda t: int('0' + t.rstrip('\x00 '), 8)
    cstr = lambda t: t.rstrip('\x00')
    ident = lambda t: t
    self.map = map(lambda s:{'o':octint,'z':cstr}.get(s[-1], ident), args[0::2])
    self.type = collections.namedtuple(name, args[1::2])

  def unpack_from(self, buffer, offset=0):
    vals = struct.unpack_from(self.fmt, buffer, offset)
    #print vals
    return self.type(*[f(v) for f, v in zip(self.map, vals)])


# unix stardard tar ("ustar") format.
TARHEADER = Struct('TARHEADER',
                   '100z', 'name',
                     '8o', 'mode',
                     '8o', 'uid',
                     '8o', 'gid',
                    '12o', 'size',
                    '12o', 'mtime',
                     '8o', 'checksum',
                      'c', 'type',
                   '100z', 'linkname',
                     '6s', 'ustar',
                     '2s', 'ustar_version',
                    '32z', 'user',
                    '32z', 'group',
                     '8o', 'device_major',
                     '8o', 'device_minor',
                   '155z', 'name_prefix',
                  )

dat = open(sys.argv[1], 'rb').read()

i = 0
header = TARHEADER.unpack_from(dat, 0)
while header.size != 0:
  print header
  i += (1 + (header.size + 511) / 512) * 512
  header = TARHEADER.unpack_from(dat, i)
