def main():
    n = 100000
    keys = ["k" + str(i) for i in range(n)]
    m = {}
    for i in range(n):
        m[keys[i]] = i
    total = 0
    for r in range(5):
        for i in range(n):
            total += m[keys[i]]
    for i in range(0, n, 2):
        del m[keys[i]]
    ints = {}
    for i in range(n):
        ints[i] = i * 2
    for i in range(n):
        total += ints[i]
    return total + len(m)
print(main())
