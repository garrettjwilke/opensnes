# Chantier #132 — a `const` array of structs loses its bank byte

Status: **root cause found, fix written, NOT merged.** Applying it
uncovers a second, pre-existing emitter defect that miscompiles. The
tree is back on the pinned compiler; nothing is shipped.

## The reported symptom

A `static const` array of structs indexed at runtime is read from bank
$00, whatever bank it is actually in. The build's bank-blind check
refuses it, so no ROM ships broken — but it forces users into parallel
scalar arrays. `tmx2snes -e` currently emits that workaround shape,
which is a tool accommodating a compiler bug and should be undone once
this lands.

## Root cause (found)

`compiler/cproc/qbe.c`, the pointer-arithmetic operand widening:

```c
if (e->type->kind == TYPEPOINTER) {
    if (e->u.binary.l->type->kind != TYPEPOINTER)
        l = funcinst(f, IEXTUW, ptrclass, l, NULL);
    ...
}
```

`IEXTUW` means *zero-extend a 16-bit word to 32 bits*. cproc types a
struct member's address computation as **`long` (size 4)**, not as a
pointer — confirmed by instrumenting the site:

```
PTRARITH: lkind=12 lsize=4 …   (tab + i      — left IS a pointer)
PTRARITH: lkind=7  lsize=4 …   (… + offsetof — left is TYPELONG!)
```

so the guard fires on a value that is already 4 bytes wide and
**truncates it to its low word, discarding the bank**:

```
%.7 =l add $tab, %.6     ; full 24-bit address, bank in the high half
%.8 =l extuw %.7         ; <-- bank destroyed here
%.9 =l extuw 0
%.10 =l add %.8, %.9
```

## The fix that works (kept at `/tmp` — re-derive, do not trust a path)

Widen only operands **narrower** than a pointer. With it, the IR becomes
`add $tab, %off` / `add %.6, 0` and every access form is correct:

| form | before | after |
|---|---|---|
| `tab[i].a` | `lda [tcc__r9]` with `lda.w #0` bank | `lda.l tab,x` |
| `tab[i].b` (offset) | idem | `lda.l tab+1,x` |
| `ents[i].y` (u16) | idem | `lda.l ents+2,x` |
| `ents[i].s` (ptr) | idem | `adc.w #:ents` — bank carried |
| `p[i].b` (param) | `lda [tcc__r9]` | unchanged (correct: runtime bank) |

A size-based guard (`type->size < 4`) is **wrong** — an array's `size`
is its byte length, so `char buf[6]` would skip widening while
`char buf[2]` would not. The guard must key on the operand already
being of pointer class, not on its size.

## Why it is not merged: it exposes an emitter defect

With the fix, `make tests` gives 14 visual regressions. They are not
timing shifts. Minimal reproducer:

```c
typedef struct { u16 addr; u16 tile; u8 pal; u8 prio; } Cfg;
Cfg cfg;
u16 build(void) {
    return cfg.tile | ((u16)cfg.pal << 10) | ((u16)cfg.prio << 13);
}
```

IR is correct (`add $cfg, 2`, `add $cfg, 4`, `add $cfg, 5`), but the
emitted code contains **one** load:

```asm
lda.w cfg+2      ; cfg.tile — right
sta 2,s
lda.w #0         ; cfg.pal  — the load is GONE
and.w #$00FF
```

The `and.w #$00FF` is `Oloadub`'s own tail, so the load instruction is
reached; what fails is its address. Both the `Oadd Kl` producing the
address temp and the `Oloadub` consuming it degrade to `lda.w #0`,
which is what `emitload` emits for a temp with **no slot and no
register**. So the address temp is being left unallocated.

Real-world impact of that miscompilation: `build_tile_entry` in
`lib/source/text.c` reads `text_config.palette` and `.priority` from
`text_config+2` (the font_tile field) instead of `+4` / `+5` — which is
why the RPG's HUD digit vanished.

The defect is **pre-existing**. It is unreachable today only because
cproc's spurious `extuw` turns these addresses into runtime values,
hiding the folded-constant-address path.

## Next step

Find why an addr-only `Kl` temp holding `$sym + const` gets no slot.
Suspects, in order: `mark_addr_only_kl` narrowing interacting with slot
allocation; `is_nop_instruction` (which carries a written warning about
exactly this class of bug — see its header comment and the audio v2
regression it records); QBE's own dead-code pass.

Do not merge the cproc change before that is closed: on its own it turns
a build-time refusal into a silent miscompilation, which is strictly
worse.

## Follow-up once it lands

- `tmx2snes -e` should emit an array of structs, and its README should
  drop the paragraph explaining the parallel-table workaround.
- `examples/games/rpg` can drop `npc_tx` / `npc_ty` / `npc_line` for a
  single `Npc[]`.
- Two compiler pattern tests (`farptr_field_copy`, `test_rmw_ptr_reread`)
  pin one particular spill spelling and will need re-pinning on the
  invariant instead — the fix changes a stack-slot spill into `pha`/`pla`,
  which preserves the value but not the regex.
