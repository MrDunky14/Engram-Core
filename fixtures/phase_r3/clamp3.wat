(module
  (func (export "clamp3") (param i32) (result i32)
    (if (result i32) (i32.gt_s (local.get 0) (i32.const 2))
      (then (i32.const 2))
      (else
        (if (result i32) (i32.lt_s (local.get 0) (i32.const 0))
          (then (i32.const 0))
          (else (local.get 0)))))))
