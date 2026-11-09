#ifndef I6X_CALIBRATION_H
#define I6X_CALIBRATION_H

// anaIn() is 0..2047. A span at/below the mixer denominator floor
// indicates absent or incomplete travel calibration, not a usable stick.
static inline bool i6xStickCalibrationValid(const CalibData &calib)
{
  return calib.mid > 100 && calib.mid < 1947 &&
         calib.spanNeg > 100 && calib.spanNeg <= 2048 &&
         calib.spanPos > 100 && calib.spanPos <= 2048;
}

#endif
