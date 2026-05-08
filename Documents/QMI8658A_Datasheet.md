# QMI8658A Datasheet — Reference Notes

**Source:** https://files.waveshare.com/upload/5/5f/QMI8658A_Datasheet_Rev_A.pdf  
**Local copy:** D:\Downloads\QMI8658A_Datasheet_Rev_A.pdf

---

## Key register map (used in firmware)

| Register | Address | Description |
|----------|---------|-------------|
| CTRL2 | 0x03 | Accelerometer ODR and range |
| CTRL8 | 0x08 | Motion/tap feature enable bits |
| CTRL9 | 0x09 | Host command register |
| CAL1_L–CAL4_H | 0x0B–0x12 | Configuration payload for CTRL9 commands |
| STATUS1 | 0x2F | Motion event flags (read to check tap / any-motion) |

## STATUS1 event flags

| Bit | Mask | Event |
|-----|------|-------|
| 7 | 0x80 | Significant Motion |
| 6 | 0x40 | No Motion |
| 5 | 0x20 | Any Motion |
| 4 | 0x10 | Pedometer step |
| 2 | 0x04 | Wake-on-Motion |
| 1 | 0x02 | Tap |

## I²C addresses (SensorLib 0.1.6 mapping)

| Constant | Value | When |
|----------|-------|------|
| `QMI8658_L_SLAVE_ADDRESS` | 0x6B | SA0 low — **this board** |
| `QMI8658_H_SLAVE_ADDRESS` | 0x6A | SA0 high |

## Tap detection (CTRL9 command 0x04)

Configured via `SensorQMI8658::configTap()`.  
Parameters at ±4 G / 500 Hz ODR used in this project:

| Parameter | Value | Meaning |
|-----------|-------|---------|
| priority | PRIORITY0 | X > Y > Z axis priority |
| peakWindow | 20 | Max 40 ms for valid peak |
| tapWindow | 50 | Quiet time between taps |
| dTapWindow | 250 | Max double-tap interval |
| alpha | 16 | 1/16 acceleration average ratio |
| gamma | 64 | 1/4 magnitude average ratio |
| peakMagThr | 0x0320 | 0.8 g² peak threshold |
| UDMThr | 0x0190 | 0.4 g² undefined-motion threshold |

## Any-motion detection (CTRL9 command 0x09)

Configured via `SensorQMI8658::configMotion()`.  
Parameters used in this project:

| Parameter | Value | Meaning |
|-----------|-------|---------|
| AnyMotion XYZ threshold | 100 mg | Per-axis trigger level |
| modeCtrl | 0x07 | Enable X, Y, Z axes |
| AnyMotionWindow | 5 samples | 10 ms @ 500 Hz |
