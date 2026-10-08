#!/usr/bin/env python3
"""gbrecomp.py - static recompiler for Game Boy / Game Boy Color ROMs (SM83 -> C).

    gbrecomp.py game.gb -o build/game [--trace game.trace] [--interp-only]

Pipeline
  1. Recursive-descent discovery of code, per ROM bank, from the interrupt vectors /
     entry point plus (optionally) every instruction a tracing run actually executed.
  2. Each bank becomes one C function containing a `switch(pc)` with a label per
     instruction; jumps inside the bank are plain gotos, everything else goes through
     the runtime dispatcher.
  3. A generated interpreter (same instruction semantics, emitted from the same tables)
     handles anything not statically found: code in RAM/HRAM, pointer tables, etc.

The output C contains translated game code. Build it for yourself; don't redistribute it.
"""
import argparse, os, sys

R8 = ['b', 'c', 'd', 'e', 'h', 'l', None, 'a']
CC = ['!(gb.f&FZ)', '(gb.f&FZ)', '!(gb.f&FC)', '(gb.f&FC)']
ALU = ['add8', 'adc8', 'sub8', 'sbc8', 'and8', 'xor8', 'or8', 'cp8']
ROT = ['cb_rlc', 'cb_rrc', 'cb_rl', 'cb_rr', 'cb_sla', 'cb_sra', 'cb_swap', 'cb_srl']
INVALID = {0xD3, 0xDB, 0xDD, 0xE3, 0xE4, 0xEB, 0xEC, 0xED, 0xF4, 0xFC, 0xFD}


def rd8(i): return 'RD(HL())' if i == 6 else 'gb.' + R8[i]
def wr8(i, v): return f'WR(HL(),{v});' if i == 6 else f'gb.{R8[i]}={v};'
def getrr(i): return ['BC()', 'DE()', 'HL()', 'gb.sp'][i]
def setrr(i, v): return ['SETBC', 'SETDE', 'SETHL', 'SETSP'][i] + f'({v});'


def oplen(op):
    if op == 0xCB: return 2
    if op in (0x10, 0x18, 0x20, 0x28, 0x30, 0x38, 0xE0, 0xF0, 0xE8, 0xF8, 0xC6, 0xCE, 0xD6, 0xDE, 0xE6, 0xEE, 0xF6, 0xFE) or (op & 0xC7) == 0x06:
        return 2
    if (op & 0xCF) == 0x01 or op in (0x08, 0xC3, 0xCD, 0xEA, 0xFA) or (op & 0xE7) in (0xC2, 0xC4):
        return 3
    return 1


# --------------------------------------------------------------------------- semantics
def sem(op, C):
    """Return (M-cycles, [C statements]) for a non-CB opcode, or None if invalid.
    C supplies operand expressions (n, nn, e, next) and control-flow emitters."""
    if op in INVALID: return None
    x, y, z = op >> 6, (op >> 3) & 7, op & 7
    p, q = y >> 1, y & 1
    S = []; m = 1
    if x == 0:
        if z == 0:
            if y == 0: pass
            elif y == 1: m = 5; S.append(f'{{uint16_t t={C.nn};WR(t,gb.sp&0xFF);WR((uint16_t)(t+1),gb.sp>>8);}}')
            elif y == 2: S.append('gb_stop();')
            elif y == 3: m = 3; S.append(C.jr(None, 0))
            else: m = 2; S.append(C.jr(CC[y - 4], 1))
        elif z == 1:
            if q == 0: m = 3; S.append(setrr(p, C.nn))
            else: m = 2; S.append(f'add_hl({getrr(p)});')
        elif z == 2:
            m = 2
            if q == 0: S.append([ 'WR(BC(),gb.a);', 'WR(DE(),gb.a);', 'WR(HL(),gb.a);SETHL(HL()+1);', 'WR(HL(),gb.a);SETHL(HL()-1);'][p])
            else: S.append(['gb.a=RD(BC());', 'gb.a=RD(DE());', 'gb.a=RD(HL());SETHL(HL()+1);', 'gb.a=RD(HL());SETHL(HL()-1);'][p])
        elif z == 3:
            m = 2; S.append(setrr(p, f'{getrr(p)}{"+" if q == 0 else "-"}1'))
        elif z in (4, 5):
            fn = 'inc8' if z == 4 else 'dec8'
            if y == 6: m = 3; S.append(f'{{uint8_t t={fn}(RD(HL()));WR(HL(),t);}}')
            else: S.append(f'gb.{R8[y]}={fn}(gb.{R8[y]});')
        elif z == 6:
            if y == 6: m = 3
            else: m = 2
            S.append(wr8(y, C.n))
        else:
            S.append(['rlca();', 'rrca();', 'rla();', 'rra();', 'daa();', 'gb.a=~gb.a;gb.f|=FN|FH;', 'gb.f=(gb.f&FZ)|FC;', 'gb.f=(gb.f&FZ)|((gb.f&FC)?0:FC);'][y])
    elif x == 1:
        if op == 0x76: S.append(C.halt())
        else:
            m = 2 if (y == 6 or z == 6) else 1
            S.append(wr8(y, rd8(z)))
    elif x == 2:
        m = 2 if z == 6 else 1
        S.append(f'{ALU[y]}({rd8(z)});')
    else:
        if z == 0:
            if y < 4: m = 2; S.append(C.ret(CC[y], 3))
            elif y == 4: m = 3; S.append(f'WR(0xFF00+{C.n},gb.a);')
            elif y == 5: m = 4; S.append(f'SETSP(add_sp({C.e}));')
            elif y == 6: m = 3; S.append(f'gb.a=RD(0xFF00+{C.n});')
            else: m = 3; S.append(f'SETHL(add_sp({C.e}));')
        elif z == 1:
            if q == 0:
                m = 3
                if p == 3: S.append('{uint16_t t=POP16();gb.a=t>>8;gb.f=t&0xF0;}')
                else: S.append(['SETBC', 'SETDE', 'SETHL'][p] + '(POP16());')
            else:
                if p == 0: m = 4; S.append(C.ret(None, 0))
                elif p == 1: m = 4; S.append(C.reti())
                elif p == 2: S.append(C.jphl())
                else: m = 2; S.append('SETSP(HL());')
        elif z == 2:
            if y < 4: m = 3; S.append(C.jp(CC[y], 1))
            elif y == 4: m = 2; S.append('WR(0xFF00+gb.c,gb.a);')
            elif y == 5: m = 4; S.append(f'WR({C.nn},gb.a);')
            elif y == 6: m = 2; S.append('gb.a=RD(0xFF00+gb.c);')
            else: m = 4; S.append(f'gb.a=RD({C.nn});')
        elif z == 3:
            if y == 0: m = 4; S.append(C.jp(None, 0))
            elif y == 6: S.append('gb.ime=0;')
            elif y == 7: S.append('gb.ime=1;gb.deadline=gb.cyc;')
        elif z == 4:
            m = 3; S.append(C.call(CC[y], 3))
        elif z == 5:
            if q == 0:
                m = 4
                S.append('PUSH16((gb.a<<8)|gb.f);' if p == 3 else f'PUSH16({["BC", "DE", "HL"][p]}());')
            else: m = 6; S.append(C.call(None, 0))
        elif z == 6:
            m = 2; S.append(f'{ALU[y]}({C.n});')
        else:
            m = 4; S.append(C.rst(op & 0x38))
    return m, S


def sem_cb(op):
    x, y, z = op >> 6, (op >> 3) & 7, op & 7
    h = z == 6
    if x == 0:
        fn = ROT[y]
        return (4 if h else 2), [f'{{uint8_t t={fn}(RD(HL()));WR(HL(),t);}}' if h else f'gb.{R8[z]}={fn}(gb.{R8[z]});']
    if x == 1:
        return (3 if h else 2), [f'bit8({y},{rd8(z)});']
    mask = f'~(1<<{y})' if x == 2 else f'(1<<{y})'
    op_ = '&' if x == 2 else '|'
    return (4 if h else 2), [f'WR(HL(),RD(HL()){op_}{mask});' if h else f'gb.{R8[z]}{op_}=(uint8_t){mask};']


# --------------------------------------------------------------------------- C contexts
class CompileCtx:
    def __init__(s, rom, bank, addr, ln, insts):
        s.bank, s.a, s.ln, s.insts = bank, addr, ln, insts
        off = addr if addr < 0x4000 else bank * 0x4000 + addr - 0x4000
        b = rom[off:off + 3] + b'\0\0'
        s.nxt = (addr + ln) & 0xFFFF
        s.n = f'0x{b[1]:02X}'
        s.nn_v = b[1] | (b[2] << 8)
        s.nn = f'0x{s.nn_v:04X}'
        s.e_v = b[1] - 256 if b[1] >= 128 else b[1]
        s.e = f'({s.e_v})'
        s.next = f'0x{s.nxt:04X}'

    def local(s, T):
        if s.bank == 0: ok = T < 0x4000
        else: ok = 0x4000 <= T < 0x8000
        return ok and T in s.insts

    def goto(s, T):
        T &= 0xFFFF
        if s.local(T):
            return f'SYNC_LOCAL(L_{T:04X},0x{T:04X});' if T <= s.a else f'goto L_{T:04X};'
        return f'SYNC_GO(0x{T:04X});'

    def _cond(s, cond, extra, body):
        if cond is None: return body
        return f'if({cond}){{gb.cyc+={extra * 4};{body}}}'

    def jr(s, cond, x): return s._cond(cond, x, s.goto(s.nxt + s.e_v))
    def jp(s, cond, x): return s._cond(cond, x, s.goto(s.nn_v))
    def call(s, cond, x): return s._cond(cond, x, f'PUSH16({s.next});' + s.goto(s.nn_v))
    def rst(s, T): return f'PUSH16({s.next});' + s.goto(T)
    def ret(s, cond, x): return s._cond(cond, x, 'SYNC_GO(POP16());')
    def reti(s): return 'gb.ime=1;gb.deadline=gb.cyc;SYNC_GO(POP16());'
    def jphl(s): return 'SYNC_GO(HL());'
    def halt(s): return f'gb_halt();SYNC_GO({s.next});'


class InterpCtx:
    n = 'RD(pc+1)'
    nn = '(RD(pc+1)|(RD(pc+2)<<8))'
    e = '((int8_t)RD(pc+1))'
    next = 'gb.pc'
    def _cond(s, cond, extra, body):
        if cond is None: return body
        return f'if({cond}){{gb.cyc+={extra * 4};{body}}}'
    def jr(s, cond, x): return s._cond(cond, x, 'gb.pc=(uint16_t)(pc+2+(int8_t)RD(pc+1));')
    def jp(s, cond, x): return s._cond(cond, x, f'gb.pc={s.nn};')
    def call(s, cond, x): return s._cond(cond, x, f'{{uint16_t t={s.nn};PUSH16(gb.pc);gb.pc=t;}}')
    def rst(s, T): return f'PUSH16(gb.pc);gb.pc=0x{T:04X};'
    def ret(s, cond, x): return s._cond(cond, x, 'gb.pc=POP16();')
    def reti(s): return 'gb.ime=1;gb.deadline=gb.cyc;gb.pc=POP16();'
    def jphl(s): return 'gb.pc=HL();'
    def halt(s): return 'gb_halt();'


# --------------------------------------------------------------------------- discovery
def flow(op, addr, ln, rom_bytes):
    """-> (targets, falls_through)"""
    b = rom_bytes
    nn = b[1] | (b[2] << 8) if ln == 3 else 0
    if op == 0xC3: return [nn], False
    if (op & 0xE7) == 0xC2: return [nn], True
    if op == 0x18: return [(addr + 2 + (b[1] - 256 if b[1] > 127 else b[1])) & 0xFFFF], False
    if op in (0x20, 0x28, 0x30, 0x38): return [(addr + 2 + (b[1] - 256 if b[1] > 127 else b[1])) & 0xFFFF], True
    if op == 0xCD or (op & 0xE7) == 0xC4: return [nn], True
    if (op & 0xC7) == 0xC7: return [op & 0x38], True
    if op in (0xC9, 0xD9, 0xE9): return [], False
    return [], True


def discover(rom, nbanks, seeds):
    insts = {}   # bank -> {addr: (op, len)}   (bank 0 = fixed region 0000-3FFF, bank n = window n)
    work = list(seeds)
    def off_of(b, a): return a if a < 0x4000 else b * 0x4000 + a - 0x4000
    while work:
        b, a = work.pop()
        while True:
            if a >= 0x8000 or (b == 0 and a >= 0x4000): break
            if a >= 0x4000 and b >= nbanks: break
            kb = 0 if a < 0x4000 else b
            d = insts.setdefault(kb, {})
            if a in d: break
            off = off_of(kb, a)
            if off >= len(rom): break
            op = rom[off]
            if op in INVALID: break
            ln = oplen(op)
            lim = 0x4000 if a < 0x4000 else 0x8000
            if a + ln > lim or off + ln > len(rom): break
            d[a] = (op, ln)
            tg, fall = flow(op, a, ln, rom[off:off + 3] + b'\0\0')
            for T in tg:
                if T < 0x4000: work.append((0, T))
                elif T < 0x8000 and kb > 0: work.append((kb, T))
            if not fall: break
            a += ln
    return insts


def gen_bank_file(rom, bank, insts, out):
    d = insts[bank]
    addrs = sorted(d)
    name = 'gb_blk0' if bank == 0 else f'gb_blkw_{bank}'
    L = ['#include "gb.h"', f'/* bank {bank}: {len(addrs)} instructions */', f'int {name}(void) {{', 'top: ;', '{', '  uint16_t pc = gb.pc;']
    L.append('  if (pc >= 0x4000) return 2;' if bank == 0 else f'  if (pc < 0x4000 || pc >= 0x8000 || gb.rom_bank != {bank}) return 2;')
    L.append('  switch (pc) {')
    for i, a in enumerate(addrs):
        op, ln = d[a]
        C = CompileCtx(rom, bank, a, ln, d)
        if op == 0xCB:
            m, S = sem_cb(rom[(a if a < 0x4000 else bank * 0x4000 + a - 0x4000) + 1])
        else:
            m, S = sem(op, C)
        S = [f'gb.cyc+={m * 4};'] + S
        # fall through to physically next emitted instruction, else explicit jump
        ends = op in (0xC3, 0x18, 0xC9, 0xD9, 0xE9) or op == 0x76
        if not ends:
            nxt = a + ln
            nxt_emitted = addrs[i + 1] if i + 1 < len(addrs) else None
            if nxt != nxt_emitted: S.append(f'SYNC_GO(0x{nxt & 0xFFFF:04X});')
        L.append(f'  case 0x{a:04X}: L_{a:04X}: ' + ''.join(S))
    L += ['  default: return 0;', '  }', '}', 'return 0;', '}']
    open(out, 'w').write('\n'.join(L) + '\n')


def gen_interp(path):
    L = ['#include "gb.h"', 'void gb_interp_step(void) {', '  uint16_t pc = gb.pc; uint8_t op = RD(pc);', '  gb_trace_mark(pc);', '  switch (op) {']
    C = InterpCtx()
    for op in range(256):
        if op == 0xCB:
            L.append('  case 0xCB: { uint8_t o2 = RD(pc+1); gb.pc = pc + 2; switch (o2) {')
            for o2 in range(256):
                m, S = sem_cb(o2)
                L.append(f'    case 0x{o2:02X}: gb.cyc+={m * 4};' + ''.join(S) + ' break;')
            L.append('  } break; }')
            continue
        r = sem(op, C)
        if r is None:
            L.append(f'  case 0x{op:02X}: gb_invalid(op); break;'); continue
        m, S = r
        L.append(f'  case 0x{op:02X}: gb.pc = (uint16_t)(pc+{oplen(op)}); gb.cyc+={m * 4};' + ''.join(S) + ' break;')
    L += ['  }', '}']
    open(path, 'w').write('\n'.join(L) + '\n')


def fnv(d):
    h = 2166136261
    for c in d: h = ((h ^ c) * 16777619) & 0xFFFFFFFF
    return h or 1


MAKEFILE = r'''# generated by gbrecomp.py
RT   := {rt}
NAME := {name}
CC   ?= gcc
OPT  ?= -O1
GEN  := $(wildcard gen_*.c)
OBJS := $(GEN:.c=.o) rt_gb.o rt_main.o rt_winpick.o
ifdef HEADLESS
  CFLAGS_X := -DHEADLESS
  LIBS :=
else
  CFLAGS_X := $(shell sdl2-config --cflags)
  LIBS := $(shell sdl2-config --libs)
endif
ifeq ($(OS),Windows_NT)
  LIBS += -lcomdlg32 -static-libgcc
endif
CFLAGS := $(OPT) -w -I$(RT) $(CFLAGS_X)
all: $(NAME)
$(NAME): $(OBJS)
	$(CC) -o $@ $^ $(LIBS)
%.o: %.c $(RT)/gb.h
	$(CC) $(CFLAGS) -c $< -o $@
rt_gb.o: $(RT)/gb.c $(RT)/gb.h
	$(CC) $(CFLAGS) -c $< -o $@
rt_main.o: $(RT)/main.c $(RT)/gb.h
	$(CC) $(CFLAGS) -c $< -o $@
rt_winpick.o: $(RT)/winpick.c
	$(CC) $(CFLAGS) -c $< -o $@
clean:
	rm -f *.o $(NAME)
'''


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('rom', nargs='?'); ap.add_argument('--player', action='store_true', help='ROM-independent player: interpreter only, no input ROM needed'); ap.add_argument('-o', '--out', required=True)
    ap.add_argument('--name', help='output binary name'); ap.add_argument('--trace', help='trace file produced by `<binary> --trace FILE` (seeds extra code)')
    ap.add_argument('--interp-only', action='store_true', help='emit only the interpreter (used for tracing runs)')
    ap.add_argument('--runtime', default=os.path.join(os.path.dirname(os.path.abspath(__file__)), 'runtime'))
    a = ap.parse_args()
    if a.player: a.interp_only = True; a.rom = a.rom or 'gbplayer.gb'; a.name = a.name or 'gbplayer'
    elif not a.rom: ap.error('rom required (or use --player)')
    rom = b'\0' * 0x8000 if a.player else open(a.rom, 'rb').read()
    nb = len(rom) // 0x4000
    os.makedirs(a.out, exist_ok=True)
    for f in os.listdir(a.out):
        if f.startswith('gen_') and f.endswith('.c'): os.remove(os.path.join(a.out, f))
    name = a.name or ''.join(ch if ch.isalnum() or ch in '_-' else '_' for ch in os.path.splitext(os.path.basename(a.rom))[0])
    gen_interp(os.path.join(a.out, 'gen_interp.c'))
    insts = {}
    if not a.interp_only:
        seeds = [(0, v) for v in (0x00, 0x08, 0x10, 0x18, 0x20, 0x28, 0x30, 0x38, 0x40, 0x48, 0x50, 0x58, 0x60, 0x100)]
        nseed = 0
        if a.trace and os.path.exists(a.trace):
            tr = open(a.trace, 'rb').read()
            for off in range(min(len(rom), len(tr) * 8)):
                if tr[off >> 3] >> (off & 7) & 1:
                    seeds.append((0, off) if off < 0x4000 else (off >> 14, 0x4000 + (off & 0x3FFF))); nseed += 1
            print(f'trace: {nseed} executed instruction addresses used as seeds')
        insts = discover(rom, nb, seeds)
        for b in sorted(insts):
            if insts[b]: gen_bank_file(rom, b, insts, os.path.join(a.out, f'gen_b{b:03d}.c'))
    T = ['#include "gb.h"']
    for b in sorted(insts):
        if insts[b]: T.append(f'int {"gb_blk0" if b == 0 else f"gb_blkw_{b}"}(void);')
    T.append(f'int (*const gb_blk0p)(void) = {"gb_blk0" if insts.get(0) else "0"};')
    T.append('int (*const gb_blkwin[GB_MAXBANKS])(void) = {' + ','.join(f'[{b}]=gb_blkw_{b}' for b in sorted(insts) if b > 0 and insts[b]) + '};')
    T.append(f'const uint32_t gb_gen_hash = {fnv(rom) if not a.interp_only else 0}u;')
    open(os.path.join(a.out, 'gen_tbl.c'), 'w').write('\n'.join(T) + '\n')
    open(os.path.join(a.out, 'Makefile'), 'w').write(MAKEFILE.format(rt=os.path.relpath(a.runtime, a.out), name=name))
    tot = sum(len(v) for v in insts.values())
    print(f'{a.rom}: {nb} banks, {tot} instructions recompiled in {sum(1 for v in insts.values() if v)} functions -> {a.out}/ (binary: {name})')


if __name__ == '__main__':
    main()
