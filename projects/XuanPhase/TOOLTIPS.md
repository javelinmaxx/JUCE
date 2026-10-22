# Xuan Phase hover hints

| Control | Tooltip |
| --- | --- |
| Low Phase | Rotates frequencies below the crossover relative to the fixed 50 ms compensated reference. |
| Crossover | Sets the smooth transition between the low and high manual phase controls. |
| High Phase | Rotates frequencies above the crossover relative to the fixed 50 ms compensated reference. |
| Auto | Saves CPU when Off; uses lookahead and transient spectrogram phases to align narrow frequency bands. |
| Auto Depth | Sets automatic phase correction from reverse alignment at negative values to full alignment at positive values. |
| Transient | Sets how quickly automatic phase correction releases after a transient, with longer times retaining it longer. |
| Stereo Link | Links automatic correction at +100%, processes channels independently at 0%, and separates their transients toward -100%. |
| Bypass | Bypasses phase processing while preserving the fixed 50 ms latency. |
| Phase Field | Shows three seconds of input transients moving left to right, with early bands green, late bands red, and applied left phase in white and right phase in gold wrapped to plus or minus 180 degrees. |
