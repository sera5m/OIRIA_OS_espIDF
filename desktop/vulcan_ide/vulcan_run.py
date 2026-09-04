#!/usr/bin/env python3
"""Desktop Vulcan runner (Wayland host). Same LUT wrap as firmware opcodes.

i32 angles are degrees. uint32 wrap: turns = deg * (2^32/360).
"""
from __future__ import annotations
import math, re, sys
SIN_Q15 = [int(round(math.sin(2 * math.pi * i / 256) * 32767)) for i in range(256)]

def _phase_from_deg(deg):
    return (int(deg) * 11930465) & 0xFFFFFFFF

def _lut_lerp(table, ph):
    i = (ph >> 24) & 255
    j = (i + 1) & 255
    f = (ph >> 16) & 255
    a, b = table[i], table[j]
    return a + (((b - a) * f) >> 8)

def lut_sin_deg(deg):
    return _lut_lerp(SIN_Q15, _phase_from_deg(int(deg)))

def lut_cos_deg(deg):
    return _lut_lerp(SIN_Q15, _phase_from_deg(int(deg) + 90))

def lut_sin_amp(deg, amp):
    return (lut_sin_deg(deg) * int(amp)) >> 15

class RunError(Exception):
    pass

def _strip_comments(src):
    out = []
    for line in src.splitlines():
        if '//' in line:
            line = line[:line.index('//')]
        out.append(line)
    return '\n'.join(out)

class Runner:
    def __init__(self):
        self.env = {}
        self.out = []
    def run(self, src):
        src = re.sub(r'set_\w+[^\n;]*;', '', _strip_comments(src))
        self._exec_block(src)
        return ''.join(self.out)
    def _exec_block(self, src):
        i, n = 0, len(src)
        while i < n:
            while i < n and src[i].isspace():
                i += 1
            if i >= n:
                break
            if src.startswith('fn ', i):
                i = self._skip_fn(src, i); continue
            if src.startswith('struct ', i):
                i = src.find('}', i)
                i = src.find(';', i) + 1 if i >= 0 else n
                continue
            if src.startswith('if[', i):
                i = self._exec_if(src, i); continue
            if src.startswith('do n[', i) or src.startswith('do ', i):
                i = self._exec_do(src, i); continue
            end = src.find(';', i)
            stmt = (src[i:] if end < 0 else src[i:end]).strip()
            i = n if end < 0 else end + 1
            if stmt:
                self._stmt(stmt)
    def _skip_fn(self, src, i):
        b = src.find('{', i)
        if b < 0:
            return len(src)
        depth, j = 0, b
        while j < len(src):
            if src[j] == '{': depth += 1
            elif src[j] == '}':
                depth -= 1
                if depth == 0:
                    j += 1
                    while j < len(src) and src[j] not in ';\n':
                        j += 1
                    return j + (1 if j < len(src) and src[j] == ';' else 0)
            j += 1
        return len(src)
    def _matching_brace(self, src, l):
        depth, j = 0, l
        while j < len(src):
            if src[j] == '{': depth += 1
            elif src[j] == '}':
                depth -= 1
                if depth == 0:
                    return j, j + 1
            j += 1
        return len(src) - 1, len(src)
    def _exec_if(self, src, i):
        lb, rb = src.find('[', i), src.find(']', src.find('[', i))
        cond = self._eval(src[lb+1:rb])
        body_l = src.find('{', rb)
        body_r, nxt = self._matching_brace(src, body_l)
        body = src[body_l+1:body_r]
        rest = src[body_r+1:].lstrip()
        if rest.startswith('else'):
            el = src.find('{', body_r+1)
            er, nxt = self._matching_brace(src, el)
            self._exec_block(body if cond else src[el+1:er])
            return nxt
        if cond:
            self._exec_block(body)
        return nxt
    def _exec_do(self, src, i):
        lb, rb = src.find('[', i), src.find(']', src.find('[', i))
        count = int(self._eval(src[lb+1:rb]))
        body_l = src.find('{', rb)
        body_r, nxt = self._matching_brace(src, body_l)
        for _ in range(max(0, count)):
            self._exec_block(src[body_l+1:body_r])
        return nxt
    def _stmt(self, stmt):
        if stmt.startswith('print(') and stmt.endswith(')'):
            self.out.append(str(self._eval(stmt[6:-1].strip())) + '\n'); return
        if stmt.startswith('return'):
            return
        if re.match(r'(?:i32|f32|bool)\s+[A-Za-z_]\w*$', stmt):
            self.env[stmt.split()[-1]] = 0; return
        m = re.match(r'(?:i32|f32|bool)?\s*([A-Za-z_]\w*)\s*=\s*(.*)$', stmt)
        if m and m.group(2) != '':
            self.env[m.group(1)] = self._eval(m.group(2)); return
        m = re.match(r'(.+)=\s*([A-Za-z_]\w*)$', stmt)
        if m:
            self.env[m.group(2)] = self._eval(m.group(1)); return
        self._eval(stmt)
    def _eval(self, expr):
        expr = expr.strip()
        if not expr:
            return 0
        if expr[0] == '"' and expr[-1] == '"':
            return expr[1:-1]
        expr = re.sub(r'\bsin_amp\s*\(', ' _sa(', expr)
        expr = re.sub(r'\bsin\s*\(', ' _s(', expr)
        expr = re.sub(r'\bcos\s*\(', ' _c(', expr)
        expr = re.sub(r'\btan\s*\(', ' _t(', expr)
        safe = {'_s': lut_sin_deg, '_c': lut_cos_deg,
                '_t': lambda d: (lut_sin_deg(d) * 32767) // (lut_cos_deg(d) or 1),
                '_sa': lut_sin_amp}
        safe.update(self.env)
        try:
            return eval(expr, {'__builtins__': {}}, safe)
        except Exception as e:
            raise RunError('%s: %s' % (expr, e)) from e

def run_source(src):
    return Runner().run(src)

def main(argv=None):
    argv = list(sys.argv[1:] if argv is None else argv)
    if not argv:
        sys.stderr.write('usage: vulcan_run.py file.vul\n'); return 2
    sys.stdout.write(run_source(open(argv[0], encoding='utf-8').read())); return 0

if __name__ == '__main__':
    raise SystemExit(main())
