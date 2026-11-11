"""A small LZ77 for the banks (src/unlz.s unpacks it):

  0lllllll                l + 1 literal bytes follow (1..127)
  1lllllll oo oo          copy l + 3 bytes (3..130) from `offset + 1` bytes back
                          in the output (16-bit, little endian)
  0x7F                    the end
"""


DEPTH = 400


def pack(b, maxoff=65535):
    n = len(b)
    i = 0
    out = bytearray()
    lit = bytearray()
    idx = {}

    def flush():
        nonlocal lit
        while lit:
            c = lit[:127]
            lit = lit[127:]
            out.append(len(c) - 1)
            out.extend(c)
    while i < n:
        best, bo = 0, 0
        key = bytes(b[i:i + 3])
        if len(key) == 3:
            for j in reversed(idx.get(key, [])[-DEPTH:]):
                if i - j > maxoff:
                    break
                ln = 0
                while i + ln < n and b[j + ln] == b[i + ln] and ln < 130:
                    ln += 1
                if ln > best:
                    best, bo = ln, i - j
                    if ln == 130:
                        break
        if best >= 3:
            flush()
            out.append(0x80 | (best - 3))
            out += bytes([(bo - 1) & 255, (bo - 1) >> 8])
            for k in range(best):
                idx.setdefault(bytes(b[i + k:i + k + 3]), []).append(i + k)
            i += best
        else:
            idx.setdefault(key, []).append(i)
            lit.append(b[i])
            i += 1
    flush()
    out.append(0x7F)
    return bytes(out)


def unpack(p):
    out = bytearray()
    i = 0
    while True:
        c = p[i]
        i += 1
        if c == 0x7F:
            return bytes(out)
        if c < 0x80:
            out += p[i:i + c + 1]
            i += c + 1
        else:
            o = (p[i] | (p[i + 1] << 8)) + 1
            i += 2
            for _ in range((c & 0x7F) + 3):
                out.append(out[-o])
