# DK IMU performance evaluation

Use real DK + LSM6DSOX measurements for calibration and adaptive rate. Mobile recording reports evaluate beat detection against edited beats. A motion CSV alone cannot measure Bluetooth latency or battery current.

## Build and capture

From the firmware repository in an NCS 3.4.0 terminal, build the evaluation image for the DK with its normal phone-controlled practice flow.

```sh
west build --no-sysbuild -d build-evaluation -b nrf52840dk/nrf52840 -- '-DDTC_OVERLAY_FILE=nrf52840dk_nrf52840.overlay;boards/evaluation.overlay'
```

Flash build-evaluation/zephyr/zephyr.hex onto the DK. Leave its USB UART connected. Close the serial terminal before opening the port from Python. The DK streams continuously at 1 Mbaud. Starting the script opens the port and clears queued input. Capture stops after the specified duration or Ctrl-C and produces plots from the saved CSV.

```sh
python3 -m venv .venv-eval
source .venv-eval/bin/activate
pip install pyserial numpy matplotlib plotly
python -m serial.tools.list_ports
python tools/imu_evaluation.py --port /dev/cu.usbmodemXXXX --seconds 180 --out runs/stationary-01
python tools/imu_evaluation.py --analyze runs/stationary-01/samples.csv
```

Replace the example port with the DK serial port. Use a fresh output directory per acquisition. Capture and reanalysis open interactive Matplotlib windows and save sampling-rate.png and statistics.json beside the CSV. When the run includes practice samples, they also save motion.html. Open or share this standalone file to zoom, pan, and click legend entries to toggle IMU traces offline. Install Plotly in the capture/analysis Python environment with `python -m pip install plotly`. Recipients only need a browser. The motion window requires practice samples. Use the toolbar magnifier and drag a rectangle over the interval and amplitude of interest. Hold X or Y while dragging to zoom only that axis. Use Pan to move around and Home to restore the full view.

Use the motion checkboxes to show X, Y or Z across the axis panels, Raw or Corrected across all panels, and each sensitivity floor independently. Magnitudes always use all three axes. The sampling-rate window has separate Measured and Target controls. Toggling a line preserves your zoom and the statistics. For the original PNG exports or a computer without a graphical desktop, run:

```sh
python tools/imu_evaluation.py --analyze runs/stationary-01/samples.csv --png
```

## Trials

1. Start capture disconnected with the DK flat and untouched. Hold each state about 30 seconds. Connect the phone, start practice, stop practice, then disconnect. Keep the IMU still through calibration and the remainder of practice. Repeat for reproducibility.
2. For conducting, start a separate run, hold still through calibration, then conduct normally. Keep stationary and conducting results separate when comparing means and noise.
3. Save the CSV, plots and statistics together with the firmware commit, sensitivity, DK/IMU identity, posture and trial conditions.

## Interpreting results

- **sampling-rate.png** counts fresh six-axis fetches in trailing one-second windows every 100 ms. Dashed lines show off, idle and practice targets of 0, 12.5 and 416 Hz. The evaluation build polls the data-ready bits, briefly wakes every 5 ms in idle, and emits state heartbeats even when off. This measures acquired samples and includes instrumentation overhead. Off means no acquisitions. Use independent current measurements for power claims.
- **motion.png** shows gyro XYZ in deg/s, accelerometer XYZ in mg and both magnitudes. The corrected axes come from the same detector update as the raw sample. Gray spans mark samples awaiting calibration. Compare raw and corrected values over the same subsequent stationary interval. The detector estimates gyro bias during stillness, initializes gravity from normalized acceleration and tracks it during motion. The angular floors apply to the detector’s smoothed signal. Their lines on the magnitude plot provide context. Stationary raw data can already fall below all three floors.
- **statistics.json** reports per-state mean and population SD of rates. Settling is the start of five consecutive windows within 5% or 1 Hz of target, whichever is larger. Rates need at least one full window. Practice gaps above 1.5 nominal periods flag potential missed acquisitions. Sequence gaps count missing UART sample records between received records. Neither statistic counts exact hardware overruns. Time is MCU uptime in milliseconds. Resetting the DK during a capture invalidates that run.
- Paired axis means, population SDs and mean magnitudes use only practice samples with valid corrections. The JSON suffix 0 means raw and 1 means corrected. Calibration removes offsets. Random axis noise can remain similar. Conducting trials show movement and should not be interpreted as stationary bias.
