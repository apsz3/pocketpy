class A:
    def __getitem__(self, i):
        raise KeyError(i)

try:
    a = A()
    b = a[1]
    exit(1)
except:
    pass

try:
    a = {'1': 3, 4: None}
    x = a[1]
    exit(1)
except:
    pass
assert True

def f():
    try:
        raise KeyError('foo')
    except A:   # will fail to catch
        exit(1)
    except:
        pass
    assert True

f()

def f1():
    try:
        assert 1 + 2 == 3
        try:
            a = {1: 2, 3: 4}
            x = a[0]
        except A:
            exit(1)
    except B:
        exit(1)
    exit(1)

try:
    f1()
    exit(1)
except KeyError:
    pass


assert True, "Msg"
try:
    assert False, "Msg"
    exit(1)
except AssertionError:
    pass
