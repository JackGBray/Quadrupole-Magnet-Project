# Test code to log MV2 data to CSV. Run this while the MV2 is running the "simple_test" sketch.
import serial
import csv
import time
from pathlib import Path

PORT = "/dev/cu.usbmodem1101"  # match this to my port
BAUD = 115200
OUTFILE = Path("MV2_vals.csv") 

def main():
    with serial.Serial(PORT, BAUD, timeout=1) as ser, OUTFILE.open("w", newline="") as f:
        writer = csv.writer(f)

        print("Logging. Press Ctrl+C to stop.")

        header_written = False

        try:
            while True:
                line = ser.readline().decode(errors="ignore").strip()

                if not line:
                    continue

                if line.startswith("#"):
                    print(line)
                    continue

                parts = line.split(",")

                if parts[0] == "t_ms":
                    writer.writerow(["pc_time_s"] + parts)
                    header_written = True
                    continue

                if not header_written:
                    writer.writerow([
                        "pc_time_s",
                        "t_ms",
                        "Bx_raw",
                        "By_raw",
                        "Bz_raw",
                        "T_raw",
                        "Bx_mT",
                        "By_mT",
                        "Bz_mT",
                        "Bmag_mT",
                    ])
                    header_written = True

                writer.writerow([time.time()] + parts)
                f.flush()

                print(line)

        except KeyboardInterrupt:
            print(f"\nSaved to {OUTFILE}")

if __name__ == "__main__":
    main()