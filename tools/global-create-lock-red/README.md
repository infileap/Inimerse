# global-create-lock-red

A re-runnable red control for the cross-lock deadlock between this VM's two
ways of creating a global slot.  It reads the built interpreter; it does not
build, patch or measure anything else, and it never writes outside a tempfile.

## The cycle it reproduces

| side | code | holds | then wants |
|---|---|---|---|
| grower | `posix_atomic_find` -> `vm_global_grow` | `global_lock` | all 16 shards, ascending |
| interner | `L_STORE_GLOBAL` -> `vm_intern` | one shard | `global_lock` |

No residue class and no shared slot is needed: the two sides are on two
different locks, so they can be in the same slot's neighbourhood or nowhere
near each other and the cycle is the same.

## The three rows

Measured on this machine, Linux/POSIX build, `-DCMAKE_BUILD_TYPE=Release`:

| probe | tree | rc | stdout |
|---|---|---|---|
| `probe.im` (string store: the intern edge exists) | unfixed `e8803b8` | `124` | `<none>` -- hung |
| `probe.im` | fixed `3913a65` | `0` | `no-deadlock` |
| `probe_int.im` (integer store: no intern edge) | unfixed `e8803b8` | `0` | `no-deadlock` |

Row 3 is the discrimination control.  It is byte-for-byte row 1 except that the
stored value is an integer, and it is green on the same tree -- so row 1's red
comes from the `shard -> global_lock` edge, not from "seventeen threads are a
lot of threads".

`tools/global-create-lock-red/name-cycle.patch` instruments a scratch tree so
that the hang names itself instead of only being a timeout.  Applied to the
unfixed tree it prints, and then stops:

```
[CYCLE] grower: holds global_lock, WANTS shard 0
[CYCLE] interner: holds a shard, WANTS global_lock (call 5)
[CYCLE] interner: GOT global_lock (call 5)
```

The first line has no matching `GOT shard 0`.  That is the cycle, named: which
two threads, which two locks, and which shard the grower died on.  Apply the
patch to a throwaway checkout only -- it is an instrument, not a fix.

## The empty control

The first version of this probe was green on **both** trees and proved nothing.
Its payload assignments were written inside the thread bodies without a
`global` declaration, so they compiled to thread **locals**: instrumenting
`L_STORE_GLOBAL` showed the instruction executed 4 times in the whole run (the
file's own top-level lines) and `vm_intern` was reached **0** times.

A control that is green because the cycle is absent and a control that is green
because it measures nothing produce the same line of output.  The `global
payloadN` declarations are what make this file measure the cycle; deleting them
silently returns the probe to its empty state.

## Boundaries

- **Only the Linux/POSIX build was measured.**  The deadlock is cross-thread
  and does not depend on the mutex being non-recursive, so the reasoning says
  it dies on WIN32 too -- but that is reasoning, not a reading.  No Windows
  number is claimed here.
- The gate is `need_lock = (vm->active_threads > 1)`, and `active_threads`
  counts **spawned** threads.  The `start` lines are what open it; the grower's
  `wait 0.5` is what keeps it open until the interners are running.
- The timeout must stay above the green runtime (~4 s here).  A timeout below
  it reports red on a healthy tree -- that mistake has been made once already.
- `vm_global_grow` fires only while the table is being **grown**, so the grower
  gets a handful of chances, not four hundred.  That is why there are sixteen
  interners: the grow takes all sixteen shards at once, so it only has to meet
  one holder.
