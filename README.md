# Quadrupole-Magnet-Project
This repo has code for my final year engineering project.

## What does it do?
There are 3 scripts in folder _MV2_stepper_tests_: MV2.ino, stepper_4_MV2.ino, MV2_stepper.py

The Arduino scripts control a **3-axis magnetometer** and **4 steppermotors**.

The python script lets you **rotate the permanent magnets (PM)**, **take measurements**, and **setup automatic sweeps** of the four PM angle parameters and **logging this into a csv** file for analysis. 

## Assumed hardware
- 1x MV2 3-axis magnetometer from Metrolab.
- 4x Nema 23 stepper motors.
- 4x TB6600 stepper motor drivers.
- 2x arduino UNO boards.
- 1x Power Supply Unit.
- Connecting wires and breadboards.


## How to use it
In the arduino IDE:
- Compile and upload stepper_4_MV2.ino to the stepper motor arduino.
- Compile and upload MV2.ino to the MV2 magnetometer.

In your Python IDE:
- Run MV2_stepper.py

In CLI:
There are three modes:
  - setup
  - manual
  - auto sweep

 ### Setup mode
 Use this mode to align and adjust the PMs to the zero position you want. 
 Commands:
 - ```ZERO```: set the zero position for all four PMs
 - ```GOTO {motor no. or all} {angle}```: from the current zero position, move a specific one or all four PMs to the angle entered. (absolute angle)
 - ```MOVE {motor no. or all} {angle}```: move a specific one or all four PMs by the angle entered. (relative angle)

### Manual mode
Use this to interactively measure the magnetic field as you adjust the PMs.
Commands:
- ```read {seconds}```: can read out the magnetic field vector for n number of measurements across n seconds.
- ```motor {commands}```: using same instructions as the setup mode, can rotate PMs to different angles.

### Auto sweep mode
Use this to have the setup sweep through a bunch of parameters taking measurements from the MV2 and saving them to a csv file.
Enter sequence as: 
```
> Enter sequence (4 values per line):
m1,m2,m3,m4
m1,m2,m3,m4
m1,m2,m3,m4

example:
0,0,0,0
0,0,45,45
0,0,90,90
0,0,135,135
0,0,180,180
```
Enter a blank line to finish the sequence.

Set how many measurements you want for each parameter setting in seconds (MV2 takes measureemnts every second)
```
> Measurement time per step (s) [default 60]:

```
Select if you want the PMs to go back to their original zero positions.
```
> Return to zero at end? [Y/n]: 
```
This will all write to a csv file that is created in a folder: mv2_logs.






























