# plotting MV2 sweep data
import matplotlib.pyplot as plt
import numpy as np

# load data from file .csv
data = np.loadtxt('mv2_logs/mv2_sweep_20260505_120434.csv', delimiter=',', skiprows=1)  

# extract columns
Bx_mT = data[:, 7]  # Bx in mT
By_mT = data[:, 8]  # By in mT
Bz_mT = data[:, 9]  # Bz in mT
B_mag_mT = data[:, 10]  # B magnitude in mT
pair_1_angle_deg = data[:, 11]  # Pair 1 angle in degrees
pair_2_angle_deg = data[:, 13]  # Pair 2 angle in degrees


# plot Bmag as a scalar field with pair angles as x and y
plt.figure()
plt.scatter(pair_1_angle_deg, pair_2_angle_deg, c=B_mag_mT, cmap='viridis')
plt.xlabel('Pair 1 Angle (degrees)')
plt.ylabel('Pair 2 Angle (degrees)')
plt.title('B Magnitude Sweep')
plt.colorbar(label='B Magnitude (mT)')
plt.show()