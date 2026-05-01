# This code controls the stepper motors and reads the MV2 data through separate serial ports. 

# Functions
# setup mode: manual terminal inputs to control the stepper motors.
# run mode: runs a predefined sequence of stepper motor movements while logging MV2 data to a new CSV each run.
# manual mode: allows the user to input stepper motor commands in real-time while logging MV2 data.(not to CSV, just print to terminal)

import csv
import time
from datetime import datetime
from pathlib import Path

import serial



STEPPER_PORT = "/dev/cu.usbmodem101"  # match this to my port
MV2_PORT = "/dev/cu.usbmodem11101"  # match this to my port
BAUD = 115200


OUTPUT_DIR = Path("mv2_logs")
OUTPUT_DIR.mkdir(exist_ok=True)


# ---------- Serial ----------

def open_serial(port):
    ser = serial.Serial(port, BAUD, timeout=1)
    time.sleep(2)
    ser.reset_input_buffer()
    return ser


def send_stepper(ser, cmd):
    ser.write((cmd + "\n").encode())
    return ser.readline().decode(errors="ignore").strip()


# ---------- MV2 ----------

def read_mv2_line(ser):
    line = ser.readline().decode(errors="ignore").strip()

    if not line or line.startswith("#") or line.startswith("t_ms"):
        return None

    parts = line.split(",")

    if len(parts) != 9:
        return None

    try:
        return {
            "pc_time_s": time.time(),
            "t_ms": float(parts[0]),
            "Bx_raw": int(parts[1]),
            "By_raw": int(parts[2]),
            "Bz_raw": int(parts[3]),
            "T_raw": int(parts[4]),
            "Bx_mT": float(parts[5]),
            "By_mT": float(parts[6]),
            "Bz_mT": float(parts[7]),
            "Bmag_mT": float(parts[8]),
        }
    except:
        return None


def collect_mv2_samples(ser, duration_s, metadata, print_hz=1.0):
    rows = []
    start = time.time()
    last_print = 0.0
    print_interval = 1.0 / print_hz

    while time.time() - start < duration_s:
        row = read_mv2_line(ser)

        if row is None:
            continue

        row.update(metadata)
        rows.append(row)

        now = time.time()

        if now - last_print >= print_interval:
            print(
                f"t={now-start:6.1f}s | "
                f"|B|={row['Bmag_mT']:8.3f} mT | "
                f"Bx={row['Bx_mT']:7.3f} "
                f"By={row['By_mT']:7.3f} "
                f"Bz={row['Bz_mT']:7.3f}"
            )
            last_print = now

    return rows


# ---------- Stepper ----------

def wait_until_idle(ser):
    while True:
        if send_stepper(ser, "BUSY") == "IDLE":
            return
        time.sleep(0.05)


def move_to_angles(ser, angles):
    for i, angle in enumerate(angles, start=1):
        send_stepper(ser, f"GOTO {i} {angle}")
    wait_until_idle(ser)


# ---------- Parsing ----------

def parse_sequence(text):
    lines = text.strip().splitlines()
    seq = []

    for line in lines:
        parts = [float(x.strip()) for x in line.split(",")]
        if len(parts) != 4:
            raise ValueError("Each row must have 4 angles")
        seq.append(parts)

    return seq


# ---------- Modes ----------

def setup_mode(stepper_ser):
    print("\nSETUP MODE (motor only)")
    print("Type commands like: ZERO, GOTO 1 90, MOVE 2 -10")
    print("Type 'back' to exit\n")

    while True:
        cmd = input("stepper> ").strip()

        if cmd in {"back", "exit", "q"}:
            return

        if not cmd:
            continue

        print(send_stepper(stepper_ser, cmd))


def manual_mode(stepper_ser, mv2_ser):
    print("\nMANUAL MODE")
    print("Commands:")
    print("  read [seconds]")
    print("  motor <command>")
    print("  back\n")

    while True:
        cmd = input("manual> ").strip()

        if cmd in {"back", "exit", "q"}:
            return

        if cmd.startswith("read"):
            parts = cmd.split()
            duration = float(parts[1]) if len(parts) > 1 else 2.0

            collect_mv2_samples(
                mv2_ser,
                duration_s=duration,
                metadata={"mode": "manual"},
            )

        elif cmd.startswith("motor "):
            print(send_stepper(stepper_ser, cmd[6:]))

        else:
            print("Unknown command")


def auto_sweep_mode(stepper_ser, mv2_ser):
    print("\nAUTO SWEEP MODE")

    print("Enter sequence (4 values per line):")
    text = ""
    while True:
        line = input()
        if line.strip() == "":
            break
        text += line + "\n"

    sequence = parse_sequence(text)

    dwell = input("Measurement time per step (s) [default 60]: ").strip()
    dwell = float(dwell) if dwell else 60.0

    return_zero = input("Return to zero at end? [Y/n]: ").lower() not in {"n", "no"}

    run_id = datetime.now().strftime("%Y%m%d_%H%M%S")
    filename = OUTPUT_DIR / f"mv2_sweep_{run_id}.csv"

    print(f"\nRunning sweep ({len(sequence)} steps)\n")

    all_rows = []

    send_stepper(stepper_ser, "ZERO")

    for i, angles in enumerate(sequence):
        print(f"\nStep {i+1}: {angles}")

        move_to_angles(stepper_ser, angles)

        rows = collect_mv2_samples(
            mv2_ser,
            duration_s=dwell,
            metadata={
                "run_id": run_id,
                "step_index": i,
                "m1_deg": angles[0],
                "m2_deg": angles[1],
                "m3_deg": angles[2],
                "m4_deg": angles[3],
            },
        )

        all_rows.extend(rows)

    if return_zero:
        print("\nReturning to zero...")
        move_to_angles(stepper_ser, [0, 0, 0, 0])

    write_csv(all_rows, filename)

    print("\nSweep complete\n")


# ---------- CSV ----------

def write_csv(rows, filename):
    if not rows:
        print("No data collected")
        return

    with filename.open("w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=rows[0].keys())
        writer.writeheader()
        writer.writerows(rows)

    print(f"Saved to {filename}")


# ---------- Main ----------

def main():
    print("Connecting...")

    with open_serial(STEPPER_PORT) as stepper_ser, open_serial(MV2_PORT) as mv2_ser:
        print("Connected")

        while True:
            print("\nSelect mode:")
            print("1: setup")
            print("2: manual")
            print("3: auto sweep")
            print("q: quit")

            choice = input("> ").strip().lower()

            if choice == "1":
                setup_mode(stepper_ser)

            elif choice == "2":
                manual_mode(stepper_ser, mv2_ser)

            elif choice == "3":
                auto_sweep_mode(stepper_ser, mv2_ser)

            elif choice in {"q", "quit"}:
                print("Exiting")
                return

            else:
                print("Invalid option")


if __name__ == "__main__":
    main()