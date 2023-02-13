a = open('123.txt', 'wt')
a.write('123')
a.write('456')
a.close()

with open('123.txt', 'rt') as f:
    assert f.read() == '123456'

with open('123.txt', 'a') as f:
    f.write('测试')

with open('123.txt', 'r') as f:
    assert f.read() == '123456' + '测试'
