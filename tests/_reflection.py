assert type(1) is int
assert type(1.0) is float
assert type(object) is type
assert type(type) is type

assert hasattr(object, '__base__')
assert hasattr(1, '__add__')
assert hasattr(int, '__add__')

assert type(1).__add__(1, 2) == 3
assert getattr(1, '__add__')(2) == 3

a = {}
setattr(a, 'b', 1)
assert a.b == 1
assert getattr(a, 'b') == 1


def f1():
    "docstring"
assert help(f1) == "docstring"

def f2():
    """hello, worl

d"""
assert help(f2) == "hello, worl\n\nd"

