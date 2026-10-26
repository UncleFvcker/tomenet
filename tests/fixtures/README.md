# Randart compatibility fixtures

Both references originate from commit `a79d1b5` before the Chaos Artifact changes.

- `randart-legacy.txt`: 1,184 native generator results across 37 real base kinds
  and 32 seeds each. The final column identifies aggravation from the old power
  compensation rule. Every stored field must stay identical, except that flag
  in the 75 identified cases. Prices and levels remain the original values.
- `item-4.9.26.bin`: one encrypted item record followed by marker `0x12345678`,
  written by the unchanged `wr_item` from `a79d1b5:src/server/save.c` with its
  original primitive writers and zero initial XOR byte. It contains known boots,
  legacy seed `0x89abcdef`, ownership, a physical rune and history fields. It
  deliberately has no new artifact version byte. The current reader must consume
  the complete old record and preserve the following marker and item fields.

Keep these fixtures fixed; regenerating them with the current writer/generator
would erase their value as independent compatibility references.
