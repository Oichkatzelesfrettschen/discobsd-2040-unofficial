(list 1e-4 0.0001 1e-5 2e-7 1e-10 1e-38 1e38 1e37)
(list 9.99999e-5 1.00001e-4 0.00099999)
(random 1)
(random 0)
(random -5)
(random -2147483648)
(list -2147483648 (- -2147483647 1) -5)
(list (rem -2147483648 -1) (mod -2147483648 -1) (rem 7 -1) (rem -7 2))
(- -2147483648)
(list (- 5) (- 2.5) (- 0))
(make-array '(50000 50000))
(make-array '(50000 50000) :element-type 'bit)
(make-array 2147483647)
(array-dimensions (make-array '(65536 32768 2 2 2)))
(make-array 1500000000)
(array-dimensions (make-array '(100000 100000 0)))
(array-dimensions (make-array '(0 100000 100000)))
(list (aref (make-array '(3 5) :initial-element 7) 2 4) (aref (make-array 70 :element-type 'bit :initial-element 1) 69))
(list (mod 13 4) (mod -13 4) (mod 13 -4) (mod -13 -4) (mod 5 -1) (mod 6 -3) (mod 0 -3) (mod -6 3))
(list (rem 13 4) (rem -13 4) (rem 13 -4) (rem -13 -4) (rem 5 -1) (rem 6 -3))
(list (mod 13.4 1) (mod -13.5 1) (mod 4.0 -2) (mod 5.5 -2) (mod -5.5 2) (rem -13.5 1) (rem 4.0 -2))
(list (round 2.5) (round 3.5) (round -2.5) (round 0.5) (round 1.5) (round 7 2) (round 5 2) (round -7 2) (round -5 2) (round 6 4))
(list (floor 5 -3) (floor -5 3) (ceiling 5 -3) (ceiling -5 3) (truncate 5 -3) (truncate -5 3) (round 5 -3) (floor 4 2) (ceiling -4 2))
(floor 5 0)
(ceiling 5 0)
(truncate 5 0)
(round 5 0)
(floor 5.0 0)
(round 1.5 0.0)
(floor -2147483648 -1)
(floor 1e10)
(round -3e9)
(list (ash 1 31) (ash 1 32) (ash 1 40) (ash -1 40) (ash 5 -31) (ash -5 -31) (ash -5 -40) (ash 5 -40) (ash -1 1) (ash -2147483648 1) (ash 1 -2147483648))
(list (list 100000000000 -100000000000 2147483647 2147483648 4294967295 4294967296 12345678901234 -2147483648 -2147483649))
(list 123456789012345678901234567890 #x7fffffff #xffffffff)
(let ((l nil)) (list (pop l) l))
(list (keywordp :a) (keywordp :b) (keywordp :initial-element) (keywordp :foo) (keywordp 'a) (keywordp nil))
(list 0.0 (- 0.0) (* 0.0 5) (+ 1 -1.0) (sin 0) (list 0.5))
; A semicolon comment ends at the end of its line, whatever it contains ( ) ;;
1
(list 1 ; two (
 2 ; ) three
 3)
(list 1 2) ; trailing comment, a form on the same line follows: (list 3)
#| a block comment |# 4
(list 1 #| inline |# 2 #| ; ( |# 3)
#| outer #| inner |# still outer ; ( |# (list 5)
#| unbalanced # | |# 6
(list 'a;b
 'c)
(list #\; #\( #\) #\" #\a)
#*101
(list 1)
(list #*1 #*10)
#\065
#\Space
#\Nul
#\x41
(list #\000 #\065 #\a #\newline)
(list (get-key) (get-key) (get-key))
