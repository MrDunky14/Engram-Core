#!/usr/bin/env python3
"""Emit tiny WebAssembly binaries for Phase 14 gates (no external tools)."""
from __future__ import annotations

import os
import shutil
import subprocess


def leb128_u(n: int) -> bytes:
    out = bytearray()
    while True:
        b = n & 0x7F
        n >>= 7
        if n:
            out.append(b | 0x80)
        else:
            out.append(b)
            break
    return bytes(out)


def vec(elems: list[bytes]) -> bytes:
    body = b"".join(elems)
    return leb128_u(len(elems)) + body


def name(s: str) -> bytes:
    raw = s.encode("utf-8")
    assert len(raw) < 256
    return leb128_u(len(raw)) + raw


def section(sid: int, payload: bytes) -> bytes:
    return bytes([sid]) + leb128_u(len(payload)) + payload


def emit_hello_sensor_const(path: str, const_val: int) -> None:
    # Types: ([] -> i32)
    ty = bytes([0x60, 0x00, 0x01, 0x7F])
    types_sec = vec([ty])

    func_sec = vec([bytes([0x00])])  # func 0 uses type 0

    export_entry = name("sensor_read") + bytes([0x00, 0x00])
    export_sec = vec([export_entry])

    # Func body (no locals) : i32.const <imm> ; end
    expr = bytes([0x41]) + leb128_u(const_val & 0xFFFFFFFF) + bytes([0x0B])
    func_body = leb128_u(len(expr)) + expr
    code_sec = vec([func_body])

    wasm = b"\0asm\x01\0\0\0" + section(1, types_sec) + section(3, func_sec) + section(7, export_sec) + section(10, code_sec)

    os.makedirs(os.path.dirname(path), exist_ok=True)
    path = os.path.abspath(path)
    with open(path, "wb") as f:
        f.write(wasm)


def emit_runaway_hostgate(path: str) -> None:
    # Type: [] -> []
    ty = bytes([0x60, 0x00, 0x00])
    types_sec = vec([ty])

    import_ent = name("host") + name("gate") + bytes([0x00, 0x00])
    imports_sec = vec([import_ent])

    func_sec = vec([bytes([0x00])])  # stack: import idx0, func idx1

    export_ent = name("_start") + bytes([0x00, 0x01])
    export_sec = vec([export_ent])

    # loop {}; call 0; br 0; end ; end — extra trailing end for function??
    expr = bytes([0x03, 0x40, 0x10, 0x00, 0x0C, 0x00, 0x0B, 0x0B])
    func_body = leb128_u(len(expr)) + expr
    code_sec = vec([func_body])

    wasm = b"\0asm\x01\0\0\0" + section(1, types_sec) + section(2, imports_sec) + section(3, func_sec) + section(7, export_sec) + section(10, code_sec)

    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(wasm)


def main() -> None:
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    out = os.path.join(root, "fixtures", "phase14")
    hello = os.path.join(out, "hello_sensor.wasm")
    hello2 = os.path.join(out, "hello_sensor_v2.wasm")
    rw = os.path.join(out, "runaway.wasm")
    emit_hello_sensor_const(hello, 42)
    emit_hello_sensor_const(hello2, 77)
    emit_runaway_hostgate(rw)
    print("Wrote:", hello)
    print("Wrote:", hello2)
    print("Wrote:", rw)

    wv = shutil.which("wasm-validate")
    if wv:
        for p in (hello, hello2, rw):
            subprocess.check_call([wv, os.path.abspath(p)])


if __name__ == "__main__":
    main()
