def main():
    a = []
    for i in range(1000000):
        a.append(i)
    total = 0
    for r in range(10):
        for x in a:
            total += x
    m = {}
    for i in range(1000):
        m["k" + str(i)] = i
    for r in range(1000):
        for k, v in m.items():
            total += v
    return total
print(main())
