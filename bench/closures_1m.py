def make():
    n = 0
    def inc():
        nonlocal n
        n = n + 1
        return n
    return inc
f = make()
r = 0
for i in range(1000000):
    r = f()
print(r)
