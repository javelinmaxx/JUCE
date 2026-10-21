# Velvet audio reference

`kuo-stereo-velvet.csv` was rendered from Kuo Stereo at commit `b5297a76ba`,
before extracting `VelvetStage.h`, using its original processor and DSP.
It contains all output samples, not just summary measurements.

The fixture covers 8,192 stereo samples at each of 44.1, 48 and 96 kHz in
127-sample blocks (with a shorter final block). Haas Center and Variability are
zero; Width is 85%, Transient is 100%, band levels are 175/65/140%, and Output
is 0 dB. Attack starts at 0.7 ms and changes to 5 ms for blocks starting after
sample 4,096. Input is a 220 Hz sine plus deterministic noise, with impulses
on the left every 2,000 samples and different left/right base levels.

The exact input generator and parameter schedule are retained in
`../ProcessorTests.cpp` under `referenceTests()`. The test compares both output
channels sample by sample with a 1e-6 absolute tolerance, allowing floating-point
compiler differences and the CSV's ten-decimal serialization. Keep this fixture
independent of the extracted implementation when updating the shared DSP.
