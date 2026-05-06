# This code controls the stepper motors and reads the MV2 data through separate serial ports.
#
# Modes:
# setup mode: manual terminal inputs to control the stepper motors.
# manual mode: allows motor commands and averaged MV2 readouts.
# auto sweep mode: runs a predefined sequence of motor movements while logging averaged MV2 data to CSV.

import csv
import time
from datetime import datetime
from pathlib import Path

import serial


STEPPER_PORT = "/dev/cu.usbmodem11201"
MV2_PORT = "/dev/cu.usbmodem11101"
BAUD = 115200


OUTPUT_DIR = Path("mv2_logs")
OUTPUT_DIR.mkdir(exist_ok=True)

# ---- HARDCODED SETTINGS ----
AVG_WINDOW_S = 1.0
SETTLE_AFTER_MOVE_S = 0.5
FLUSH_TIME_S = 0.5


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


def flush_mv2_buffer(ser):
    end = time.time() + FLUSH_TIME_S
    while time.time() < end:
        ser.reset_input_buffer()
        time.sleep(0.02)


def average_rows(window):
    n = len(window)

    avg = {
        "pc_time_s": time.time(),
        "t_ms": window[-1]["t_ms"],
        "n_samples": n,
    }

    keys = [
        "Bx_raw", "By_raw", "Bz_raw", "T_raw",
        "Bx_mT", "By_mT", "Bz_mT", "Bmag_mT"
    ]

    for k in keys:
        avg[k] = sum(r[k] for r in window) / n

    return avg


def collect_one_average(ser):
    window = []
    start = time.time()

    while time.time() - start < AVG_WINDOW_S:
        row = read_mv2_line(ser)

        if row is not None:
            window.append(row)

    if not window:
        raise RuntimeError("No valid MV2 samples collected during averaging window")

    return average_rows(window)


def collect_mv2_samples(ser, duration_s, metadata):
    """
    duration_s is kept as the prompt meaning:
    Measurement time per step (s)

    Since AVG_WINDOW_S = 1.0, this gives one averaged measurement per second.
    Example:
      duration_s = 5
      -> 5 saved averaged rows
    """
    num_measurements = int(duration_s / AVG_WINDOW_S)
    rows = []

    flushed = collect_one_average(ser)
    print(f"flushed first window ({flushed['n_samples']} samples)")

    for measurement_index in range(num_measurements):
        avg_row = collect_one_average(ser)

        avg_row.update(metadata)
        avg_row["measurement_index"] = measurement_index

        rows.append(avg_row)

        print(
            f"measurement {measurement_index + 1}/{num_measurements} | "
            f"avg over {avg_row['n_samples']:3d} samples | "
            f"|B|={avg_row['Bmag_mT']:8.3f} mT | "
            f"Bx={avg_row['Bx_mT']:7.3f} "
            f"By={avg_row['By_mT']:7.3f} "
            f"Bz={avg_row['Bz_mT']:7.3f}"
        )

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
    print("Commands: ZERO, GOTO, MOVE, etc.")
    print("Type 'back' to exit\n")

    while True:
        cmd = input("stepper> ").strip()

        if cmd in {"back", "exit", "q"}:
            return

        if cmd:
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

            flush_mv2_buffer(mv2_ser)

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
    print("Enter sequence (4 values per line). Blank line to finish:\n")

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

        time.sleep(SETTLE_AFTER_MOVE_S)
        flush_mv2_buffer(mv2_ser)

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
    filtered_rows = []
    for i in range(len(all_rows)):
        if all_rows[i]["n_samples"] <= 70:
            filtered_rows.append(all_rows[i])

    write_csv(filtered_rows, filename)

    print("\nSweep complete\n")

def auto_sweep_from_file(stepper_ser, mv2_ser):
    print("\nAUTO SWEEP FROM FILE MODE")
    filepath = input("Enter path to sequence file: ").strip()
    if not filepath:
        print("No file path provided")
        return

    try:
        with open(filepath, "r") as f:
            text = f.read()
    except Exception as e:
        print(f"Error reading file: {e}")
        return

    try:
        sequence = parse_sequence(text)
    except Exception as e:
        print(f"Error parsing sequence: {e}")
        return

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

        time.sleep(SETTLE_AFTER_MOVE_S)
        flush_mv2_buffer(mv2_ser)

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
    
    filtered_rows = []
    for i in range(len(all_rows)):
        if all_rows[i]["n_samples"] <= 70:
            filtered_rows.append(all_rows[i])
    write_csv(filtered_rows, filename)
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
            print("4: auto sweep sequence from file")
            print("q: quit")

            choice = input("> ").strip().lower()

            if choice == "1":
                setup_mode(stepper_ser)

            elif choice == "2":
                manual_mode(stepper_ser, mv2_ser)

            elif choice == "3":
                auto_sweep_mode(stepper_ser, mv2_ser)
            elif choice == "4":
                auto_sweep_from_file(stepper_ser, mv2_ser)

            elif choice in {"q", "quit"}:
                print("Exiting")
                return

            else:
                print("Invalid option")


if __name__ == "__main__":
    main()