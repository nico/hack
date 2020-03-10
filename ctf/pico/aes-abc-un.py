#!/usr/bin/env python

BLOCK_SIZE = 16
UMAX = int(256 ** BLOCK_SIZE)


def to_bytes(n):
    s = hex(n)
    s_n = s[2:].rstrip('L')
    if len(s_n) % 2 != 0:
        s_n = '0' + s_n
    decoded = s_n.decode('hex')

    pad = (len(decoded) % BLOCK_SIZE)
    if pad != 0: 
        decoded = "\0" * (BLOCK_SIZE - pad) + decoded
    return decoded


def remove_line(s):
    return s[:s.index('\n') + 1], s[s.index('\n')+1:]


def parse_header_ppm(f):
    data = f.read()

    header = ""

    for i in range(3):
        header_i, data = remove_line(data)
        header += header_i

    return header, data


if __name__=="__main__":
    with open('body.enc.ppm', 'rb') as f:
        header, data = parse_header_ppm(f)

    blocks = [data[i * BLOCK_SIZE:(i+1) * BLOCK_SIZE]
              for i in range(len(data) / BLOCK_SIZE)]

    for i in range(len(blocks) - 2, -1, -1):
        prev_blk = int(blocks[i].encode('hex'), 16)
        curr_blk = int(blocks[i+1].encode('hex'), 16)
        n_curr_blk = (-prev_blk + curr_blk) % UMAX
        blocks[i+1] = to_bytes(n_curr_blk)

    ct_bca = "".join(blocks[1:])

    with open('flag.enc.ppm', 'wb') as fw:
        fw.write(header)
        fw.write(ct_bca)
