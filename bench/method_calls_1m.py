class Counter:
    def __init__(self):
        self.n = 0
    def inc(self):
        self.n = self.n + 1
c = Counter()
for i in range(1000000):
    c.inc()
print(c.n)
