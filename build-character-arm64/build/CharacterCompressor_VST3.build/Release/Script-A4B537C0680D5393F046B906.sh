#!/bin/sh
set -e
if test "$CONFIGURATION" = "Debug"; then :
  cd "/Users/andy/git repos/JUCE/build-character-arm64"
  /Applications/CMake.app/Contents/bin/cmake -E copy "/Users/andy/git repos/JUCE/build-character-arm64/CharacterCompressor_artefacts/JuceLibraryCode/CharacterCompressor_VST3/PkgInfo" "/Users/andy/git repos/JUCE/build-character-arm64/CharacterCompressor_artefacts/Debug/VST3/Character Compressor.vst3/Contents"
  cd "/Users/andy/git repos/JUCE/build-character-arm64"
  /Applications/CMake.app/Contents/bin/cmake "-Dsrc=/Users/andy/git repos/JUCE/build-character-arm64/CharacterCompressor_artefacts/Debug/VST3/Character Compressor.vst3" -P "/Users/andy/git repos/JUCE/extras/Build/CMake/checkBundleSigning.cmake"
  cd "/Users/andy/git repos/JUCE/build-character-arm64"
  /Applications/CMake.app/Contents/bin/cmake -E echo removing\ moduleinfo.json
  /Applications/CMake.app/Contents/bin/cmake -E remove -f /Users/andy/git\ repos/JUCE/build-character-arm64/CharacterCompressor_artefacts/Debug/VST3/Character\ Compressor.vst3/Contents/moduleinfo.json /Users/andy/git\ repos/JUCE/build-character-arm64/CharacterCompressor_artefacts/Debug/VST3/Character\ Compressor.vst3/Contents/Resources/moduleinfo.json
  cd "/Users/andy/git repos/JUCE/build-character-arm64"
  /Applications/CMake.app/Contents/bin/cmake -E echo creating\ /Users/andy/git\ repos/JUCE/build-character-arm64/CharacterCompressor_artefacts/Debug/VST3/Character\ Compressor.vst3/Contents/Resources/moduleinfo.json
  /Applications/CMake.app/Contents/bin/cmake -E make_directory /Users/andy/git\ repos/JUCE/build-character-arm64/CharacterCompressor_artefacts/Debug/VST3/Character\ Compressor.vst3/Contents/Resources
  "/Users/andy/git repos/JUCE/build-character-arm64/vst3_helpers/CharacterCompressor/vst3_helper" > /Users/andy/git\ repos/JUCE/build-character-arm64/CharacterCompressor_artefacts/Debug/VST3/Character\ Compressor.vst3/Contents/Resources/moduleinfo.json
fi
if test "$CONFIGURATION" = "Release"; then :
  cd "/Users/andy/git repos/JUCE/build-character-arm64"
  /Applications/CMake.app/Contents/bin/cmake -E copy "/Users/andy/git repos/JUCE/build-character-arm64/CharacterCompressor_artefacts/JuceLibraryCode/CharacterCompressor_VST3/PkgInfo" "/Users/andy/git repos/JUCE/build-character-arm64/CharacterCompressor_artefacts/Release/VST3/Character Compressor.vst3/Contents"
  cd "/Users/andy/git repos/JUCE/build-character-arm64"
  /Applications/CMake.app/Contents/bin/cmake "-Dsrc=/Users/andy/git repos/JUCE/build-character-arm64/CharacterCompressor_artefacts/Release/VST3/Character Compressor.vst3" -P "/Users/andy/git repos/JUCE/extras/Build/CMake/checkBundleSigning.cmake"
  cd "/Users/andy/git repos/JUCE/build-character-arm64"
  /Applications/CMake.app/Contents/bin/cmake -E echo removing\ moduleinfo.json
  /Applications/CMake.app/Contents/bin/cmake -E remove -f /Users/andy/git\ repos/JUCE/build-character-arm64/CharacterCompressor_artefacts/Release/VST3/Character\ Compressor.vst3/Contents/moduleinfo.json /Users/andy/git\ repos/JUCE/build-character-arm64/CharacterCompressor_artefacts/Release/VST3/Character\ Compressor.vst3/Contents/Resources/moduleinfo.json
  cd "/Users/andy/git repos/JUCE/build-character-arm64"
  /Applications/CMake.app/Contents/bin/cmake -E echo creating\ /Users/andy/git\ repos/JUCE/build-character-arm64/CharacterCompressor_artefacts/Release/VST3/Character\ Compressor.vst3/Contents/Resources/moduleinfo.json
  /Applications/CMake.app/Contents/bin/cmake -E make_directory /Users/andy/git\ repos/JUCE/build-character-arm64/CharacterCompressor_artefacts/Release/VST3/Character\ Compressor.vst3/Contents/Resources
  "/Users/andy/git repos/JUCE/build-character-arm64/vst3_helpers/CharacterCompressor/vst3_helper" > /Users/andy/git\ repos/JUCE/build-character-arm64/CharacterCompressor_artefacts/Release/VST3/Character\ Compressor.vst3/Contents/Resources/moduleinfo.json
fi
if test "$CONFIGURATION" = "MinSizeRel"; then :
  cd "/Users/andy/git repos/JUCE/build-character-arm64"
  /Applications/CMake.app/Contents/bin/cmake -E copy "/Users/andy/git repos/JUCE/build-character-arm64/CharacterCompressor_artefacts/JuceLibraryCode/CharacterCompressor_VST3/PkgInfo" "/Users/andy/git repos/JUCE/build-character-arm64/CharacterCompressor_artefacts/MinSizeRel/VST3/Character Compressor.vst3/Contents"
  cd "/Users/andy/git repos/JUCE/build-character-arm64"
  /Applications/CMake.app/Contents/bin/cmake "-Dsrc=/Users/andy/git repos/JUCE/build-character-arm64/CharacterCompressor_artefacts/MinSizeRel/VST3/Character Compressor.vst3" -P "/Users/andy/git repos/JUCE/extras/Build/CMake/checkBundleSigning.cmake"
  cd "/Users/andy/git repos/JUCE/build-character-arm64"
  /Applications/CMake.app/Contents/bin/cmake -E echo removing\ moduleinfo.json
  /Applications/CMake.app/Contents/bin/cmake -E remove -f /Users/andy/git\ repos/JUCE/build-character-arm64/CharacterCompressor_artefacts/MinSizeRel/VST3/Character\ Compressor.vst3/Contents/moduleinfo.json /Users/andy/git\ repos/JUCE/build-character-arm64/CharacterCompressor_artefacts/MinSizeRel/VST3/Character\ Compressor.vst3/Contents/Resources/moduleinfo.json
  cd "/Users/andy/git repos/JUCE/build-character-arm64"
  /Applications/CMake.app/Contents/bin/cmake -E echo creating\ /Users/andy/git\ repos/JUCE/build-character-arm64/CharacterCompressor_artefacts/MinSizeRel/VST3/Character\ Compressor.vst3/Contents/Resources/moduleinfo.json
  /Applications/CMake.app/Contents/bin/cmake -E make_directory /Users/andy/git\ repos/JUCE/build-character-arm64/CharacterCompressor_artefacts/MinSizeRel/VST3/Character\ Compressor.vst3/Contents/Resources
  "/Users/andy/git repos/JUCE/build-character-arm64/vst3_helpers/CharacterCompressor/vst3_helper" > /Users/andy/git\ repos/JUCE/build-character-arm64/CharacterCompressor_artefacts/MinSizeRel/VST3/Character\ Compressor.vst3/Contents/Resources/moduleinfo.json
fi
if test "$CONFIGURATION" = "RelWithDebInfo"; then :
  cd "/Users/andy/git repos/JUCE/build-character-arm64"
  /Applications/CMake.app/Contents/bin/cmake -E copy "/Users/andy/git repos/JUCE/build-character-arm64/CharacterCompressor_artefacts/JuceLibraryCode/CharacterCompressor_VST3/PkgInfo" "/Users/andy/git repos/JUCE/build-character-arm64/CharacterCompressor_artefacts/RelWithDebInfo/VST3/Character Compressor.vst3/Contents"
  cd "/Users/andy/git repos/JUCE/build-character-arm64"
  /Applications/CMake.app/Contents/bin/cmake "-Dsrc=/Users/andy/git repos/JUCE/build-character-arm64/CharacterCompressor_artefacts/RelWithDebInfo/VST3/Character Compressor.vst3" -P "/Users/andy/git repos/JUCE/extras/Build/CMake/checkBundleSigning.cmake"
  cd "/Users/andy/git repos/JUCE/build-character-arm64"
  /Applications/CMake.app/Contents/bin/cmake -E echo removing\ moduleinfo.json
  /Applications/CMake.app/Contents/bin/cmake -E remove -f /Users/andy/git\ repos/JUCE/build-character-arm64/CharacterCompressor_artefacts/RelWithDebInfo/VST3/Character\ Compressor.vst3/Contents/moduleinfo.json /Users/andy/git\ repos/JUCE/build-character-arm64/CharacterCompressor_artefacts/RelWithDebInfo/VST3/Character\ Compressor.vst3/Contents/Resources/moduleinfo.json
  cd "/Users/andy/git repos/JUCE/build-character-arm64"
  /Applications/CMake.app/Contents/bin/cmake -E echo creating\ /Users/andy/git\ repos/JUCE/build-character-arm64/CharacterCompressor_artefacts/RelWithDebInfo/VST3/Character\ Compressor.vst3/Contents/Resources/moduleinfo.json
  /Applications/CMake.app/Contents/bin/cmake -E make_directory /Users/andy/git\ repos/JUCE/build-character-arm64/CharacterCompressor_artefacts/RelWithDebInfo/VST3/Character\ Compressor.vst3/Contents/Resources
  "/Users/andy/git repos/JUCE/build-character-arm64/vst3_helpers/CharacterCompressor/vst3_helper" > /Users/andy/git\ repos/JUCE/build-character-arm64/CharacterCompressor_artefacts/RelWithDebInfo/VST3/Character\ Compressor.vst3/Contents/Resources/moduleinfo.json
fi

