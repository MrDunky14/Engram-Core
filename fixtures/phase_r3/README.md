# Phase R3 Wasm fixtures

Small **wat** modules for world-model / Wasm closed-loop research. Build `.wasm` binaries with WABT:

```bat
scripts\build_phase_r3_wasm.bat
```

Or manually:

```bat
npx -p wabt wat2wasm micro_mdp.wat -o micro_mdp.wasm
```

| File | Export | Semantics |
|------|--------|-----------|
| `micro_mdp.wat` | `step(i32)->i32` | 0->1->2->2 MDP step |
| `add1.wat` | `add1(i32)->i32` | increment |
| `double.wat` | `double(i32)->i32` | x*2 |
| `parity.wat` | `parity(i32)->i32` | x & 1 |
| `clamp3.wat` | `clamp3(i32)->i32` | clamp to [0,2] |

The CI gate `r3_wasm_closed_loop_gate` loads `micro_mdp.wasm` from this directory (run from repo root).
