# gbrecomp – static recompiler for Game Boy / Game Boy Color

Translates SM83 machine code from a ROM into C, compiled together with a small hardware
runtime (MBC1/3/5, timer, DMG+CGB PPU, 4-channel APU, OAM/HDMA, double speed, battery saves).

    sh build.sh "Wario_Land_3__World_.gbc" wl3
    build/wl3/wl3 "Wario_Land_3__World_.gbc"

Needs: python3, gcc, make, SDL2 dev headers (`sdl2-config`). `HEADLESS=1 ./build.sh ...` builds without SDL.
Controls: arrows, Z=A, X=B, Enter=Start, Backspace/RShift=Select, Esc quits. Saves go to `<rom>.sav`.

## How it works
* `gbrecomp.py` discovers code per ROM bank (vectors/entry + recursive descent + every instruction a
  tracing run executed) and emits one C function per bank: `switch(pc)` with a label per instruction,
  in-bank jumps as `goto`, everything else via the dispatcher in `runtime/gb.c`.
* A generated interpreter (`gen_interp.c`, same semantics tables) covers whatever static analysis can't
  see (code in WRAM/HRAM, computed jumps into un-found code). Correctness never depends on coverage.
* `build.sh` auto-traces with scripted random input. For better coverage play with
  `<bin> rom --trace game.trace` (interpreter mode, merges into the file), then rerun
  `gbrecomp.py rom -o build/x --trace game.trace` and `make`.
* The binary needs the ROM at runtime (no game data is embedded) and refuses to use generated code
  if the ROM hash differs.

## Status
Verified here on all four ROMs: recompiled output is bit-identical to the pure interpreter
(framebuffer compare after 2500 scripted frames). Not tested: the SDL2 frontend (no SDL in my sandbox)
and audio output quality; no RTC; timing is instruction-granular, not cycle-exact; no link cable.
The generated C is a translation of the game's code – build it for personal use, don't redistribute it.

## Windows: generic player exe (drag-and-drop)
Push this folder to GitHub. The `windows-exe` workflow (Actions tab, or on every push) builds `gbplayer.exe` + `SDL2.dll`
(artifact `gbplayer-windows`). It is ROM-independent: drag any .gb/.gbc onto the exe, or run it and pick a ROM in the
dialog. It runs in interpreter mode, so no per-game recompile is needed. Untested on Windows.

## Windows: per-game recompile (optional, faster)
1. Install MSYS2 (https://www.msys2.org, default path `C:\msys64`).
2. Drag a ROM onto `windows\play.bat`. First run installs gcc/SDL2/python via pacman, recompiles the ROM, and starts it.
   Later runs of the same ROM launch immediately. (Untested on real Windows - written from the MSYS2 docs.)

## Frontend smoke test (no SDL needed)
`tests/sdl_stub/` holds a stub `SDL2/SDL.h` and stub functions covering only the calls `runtime/main.c` makes.
Compile `main.c` with `-Itests/sdl_stub` and link `sdl_stub.o` to check the frontend loop builds and runs
(it quits after ~180 frames). It is not a real SDL and cannot open a window or play sound.
