def main():
    total = 0
    for i in range(100000):
        line = f"item {i}: x={i * 2} y={i + 1}"
        parts = line.split(" ")
        back = ",".join(parts)
        total += len(back) + len(parts)
    return total
print(main())
