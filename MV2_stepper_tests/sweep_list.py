with open("full_sweep.txt", "w") as f:
    for a in range(0, 360, 10):
        for b in range(0, 360, 10):
            f.write(f"{a},{a},{b},{b}\n")