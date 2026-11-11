; Authored fixed-conclusion optimization example, not native solver output.
(primo-conflicts
  (version 3)
  (logic QF_LIA)
  (signature (declare-fun x () Int) (declare-fun y () Int))
  (cnf
    (clause (<= (* 31 x) 0))
    (clause (<= (* 31 y) 0))
    (clause (<= (+ (* (- 7) x) (* (- 20) y)) 0))
    (clause (<= (+ (* (- 34) x) (* (- 13) y)) (- 1))))
  (conflicts
    (conflict-lia
      (premises
        (a (<= (* 31 x) 0))
        (b (<= (* 31 y) 0))
        (c (<= (+ (* (- 7) x) (* (- 20) y)) 0))
        (d (<= (+ (* (- 34) x) (* (- 13) y)) (- 1))))
      (proof
        (cut impossible 9 (a 11) (b 7) (c 5) (d 9))
        (contradiction impossible)))))
