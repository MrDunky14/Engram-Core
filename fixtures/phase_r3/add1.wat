(module
  (func (export "add1") (param i32) (result i32)
    (i32.add (local.get 0) (i32.const 1))))
