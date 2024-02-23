(set increment (* 20 19 18)) ; Speed up search by using a large increment
(set answer increment)
(set keep-looking 1)
(while keep-looking
       (progn
         (set keep-looking 0)
         ; We only really need to check 11..20, 1-10 are then covered
         (set divisor 11)
         (while (< divisor 21)
                (progn
                  (if (% answer divisor) (set keep-looking 1) 0)
                  (set divisor (+ divisor 1))))
         (if keep-looking
           (set answer (+ answer increment)) 0)))

; Assemble a string with the answer
(set str (alloc-static 20))
(set ptr (+ str 19))
(while answer
       (progn
         (write-mem ptr (+ 48 (% answer 10)))
         (set answer (/ answer 10))
         (set ptr (- ptr 1))))
(write-mem ptr (- (+ str 19) ptr))

(print-str ptr)