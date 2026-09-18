# minimp3

Single-header MP3 decoder from https://github.com/lieff/minimp3, released to
the public domain under CC0 (see `LICENSE`).

Vendored from commit `ea99364f61c14656440e8d77e9c233ccf3124633`.

## Local changes

- `mp3dec_decode_frame()` uses `MINIMP3_SCRATCH_PTR` for its scratch memory
  if defined, instead of a 16 KB local variable. Some hubs don't have enough
  stack for it.
