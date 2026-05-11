;; 3-state MDP: 0->1->2->2 (Wasm ground truth for R3 closed-loop gate)
(module
  (func (export "step") (param i32) (result i32)
    (if (result i32) (i32.lt_u (local.get 0) (i32.const 2))
      (then (i32.add (local.get 0) (i32.const 1)))
      (else (i32.const 2)))))
