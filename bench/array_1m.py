a = []
for i in range(1000000):
    a.append(i)
total = 0
for i in range(len(a)):
    total = total + a[i]
print(total)
