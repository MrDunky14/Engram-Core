(module
  (func (export "parity") (param i32) (result i32)
    (i32.and (local.get 0) (i32.const 1))))
