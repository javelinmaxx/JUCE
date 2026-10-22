import dawdreamer as daw
import numpy as np
import soundfile as sf

# macOS arm64 build from build-macos.sh; adjust for your platform and architecture.
PLUGIN = (
   "build-yinyang-mac-arm64/YinyangCompressor_artefacts/"
    "Release/VST3/YinYang Compressor.vst3"
)

audio, sample_rate = sf.read(
    "input.wav", dtype="float32", always_2d=True
)

# Convert mono to stereo.
if audio.shape[1] == 1:
    audio = np.repeat(audio, 2, axis=1)
if audio.shape[1] != 2:
    raise ValueError("Use a mono or stereo WAV file.")

engine = daw.RenderEngine(sample_rate, 256)
source = engine.make_playback_processor(
    "input", np.ascontiguousarray(audio.T)
)
compressor = engine.make_plugin_processor("compressor", PLUGIN)

print(compressor.get_parameters_description())

# On a desktop, uncomment to adjust controls.
# Close the editor window to continue rendering.
# compressor.open_editor()

engine.load_graph([
    (source, []),
    (compressor, ["input"]),
])

# Extra time preserves the end of the processed audio.
engine.render(len(audio) / sample_rate + 0.1)
sf.write("compressed.wav", engine.get_audio().T, sample_rate,
         subtype="PCM_24")