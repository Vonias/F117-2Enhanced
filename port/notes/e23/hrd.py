"""Reader of the port's --hr-dump files (DIR/<id>.hrd; layout in f117port/src/enhanced/hr_pass.c write_dump)."""
import struct
import numpy as np


class Hrd:
    def __init__(self, path):
        raw = open(path, 'rb').read()
        h = struct.unpack_from('<12i', raw, 0)
        assert h[0] == 0x31445248, 'not an HRD1 file: %s' % path
        (_, self.n, self.fx, self.fy, self.lw, self.lh, fine, self.x0, self.y0, self.x1, self.y1, tn) = h
        self.fine = bool(fine)
        o = 48
        self.trace = np.frombuffer(raw, dtype='<i4', count=tn, offset=o)
        o += 4 * tn
        self.has_win = self.x1 >= self.x0
        self.ww = self.x1 - self.x0 + 1 if self.has_win else 0
        self.wh = self.y1 - self.y0 + 1 if self.has_win else 0
        cnt = self.ww * self.wh
        self.layer = np.frombuffer(raw, dtype='<u2', count=cnt, offset=o).reshape(self.wh, self.ww) if cnt else np.zeros((0, 0), np.uint16)
        o += 2 * cnt
        self.page = np.frombuffer(raw, dtype=np.uint8, count=64000, offset=o).reshape(200, 320)
        o += 64000
        self.dac = np.frombuffer(raw, dtype=np.uint8, count=768, offset=o).reshape(256, 3)
        o += 768
        self.sh3d = np.frombuffer(raw, dtype=np.uint8, count=64000, offset=o).reshape(200, 320)

    def palette(self):
        """DAC (6-bit) -> 8-bit RGB"""
        d = self.dac.astype(np.uint16)
        return ((d << 2) | (d >> 4)).astype(np.uint8)

    def events(self):
        """yield (type, args list) over the trace"""
        t, i, n = self.trace, 0, len(self.trace)
        sizes = {0: 4, 1: 4, 2: 3, 3: 1, 4: 7, 6: 0}
        while i < n:
            ty = int(t[i])
            if ty == 5:
                m = int(t[i + 4])
                yield 5, t[i + 1:i + 5 + 2 * m]
                i += 5 + 2 * m
            else:
                k = sizes[ty]
                yield ty, t[i + 1:i + 1 + k]
                i += 1 + k

    def full_layer(self):
        """the window region placed in a layer-sized u16 array"""
        out = np.zeros((self.lh, self.lw), np.uint16)
        if self.has_win:
            out[self.y0:self.y1 + 1, self.x0:self.x1 + 1] = self.layer
        return out

    def compose3d(self):
        """The 3D-only compositor of DESIGN 2.5 for the render test (no vector layer): per page pixel, a 3D write still on the page shows its
        N x N layer block (the layer value, or the page value where the block is not written); anything else shows the page pixel N x N.
        Returns (index image lh x lw, mask lh x lw: 0 = original page pixel, 1 = hr layer pixel, 2 = stamped 1x pixel, 3 = 3D block not written)."""
        n, fx, fy = self.n, self.fx, self.fy
        base = np.repeat(np.repeat(self.page, n, 0), n, 1)
        sh = np.repeat(np.repeat(self.sh3d, n, 0), n, 1).astype(bool)
        lay = self.full_layer()[fy:fy + 200 * n, fx:fx + 320 * n]
        wr = (lay & 0x100) != 0
        st = (lay & 0x200) != 0
        img = base.copy()
        use = sh & wr
        img[use] = (lay[use] & 0xFF).astype(np.uint8)
        mask = np.zeros(base.shape, np.uint8)
        mask[sh & wr & ~st] = 1
        mask[sh & wr & st] = 2
        mask[sh & ~wr] = 3
        return img, mask
