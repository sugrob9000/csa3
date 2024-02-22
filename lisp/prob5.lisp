(set answer 380) ; Use increments of 20*19 to speed up search
(set keep-looking 1)
(while keep-looking
       (progn
         (set keep-looking 0)
         ; We only really need to check 11..20, 1-10 are covered
         (set divisor 11)
         (while (< divisor 21)
                (progn
                  (set keep-looking
                       (if (% answer divisor) 1 keep-looking))
                  (set divisor (+ divisor 1))))
         (if keep-looking
           (set answer (+ answer 380)) 0)))

; Assemble a string with the answer
(set str (alloc-static 20))
(set ptr (+ str 19))
(set len 0)
(while answer
       (progn
         (write-mem ptr (+ 48 (% answer 10)))
         (set answer (/ answer 10))
         (set ptr (- ptr 1))
         (set len (+ len 1))))
(write-mem ptr len)

(print-str ptr)