## Function Tests.
#a  := 1
#_b = (1, 2)
#(d := 4) + 6
#(D := 4)
#assert D == 4
#x = D + 1
x = (a := 3,4)
y = (a := 3)
print(y)
#assert False # (D := 4)==4
# b = (d := 4) + (c := 4)
# c = (d := 2) + 1
# b = (x := a + 1) + 2
def f1():
    return 'f1'
assert f1() == 'f1'
def f2(a, b, c, d):
    return c
assert f2('a', 'b', 'c', 'd') == 'c'
def f3(a,b):
    return a - b
assert f3(1,2) == -1

def fact(n):
    if n == 1:
        return 1
    return n * fact(n - 1)
assert fact(5)==120

def f(a=1, b=-1):
    return a + b

assert f() == 0
assert f(1, 2) == 3
assert f(-5) == -6
assert f(b=5) == 6
assert f(a=5) == 4
assert f(b=5, a=5) == 10

def f(a, b, *c, d=2, e=5):
    return a + b + d + e + sum(c)

assert f(1, 2, 3, 4, 5, 6, 7, 8, 9, 10) == 62
assert f(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, d=1, e=2) == 58
assert f(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, e=1, d=2) == 58
assert f(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, d=1) == 61
assert f(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, e=1) == 58
assert f(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20) == 217
assert f(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, d=1, e=2) == 213

a = 1
b = 2

def f():
    global a, b
    a = 3
    b = 4

f()
assert a == 3
assert b == 4