#!/bin/sh
set -e
if test "$CONFIGURATION" = "Debug"; then :
  cd "/Users/andy/git repos/JUCE/build-character-arm64/vst3_helpers/CharacterCompressor"
  make -f /Users/andy/git\ repos/JUCE/build-character-arm64/vst3_helpers/CharacterCompressor/CMakeScripts/ReRunCMake.make
fi
if test "$CONFIGURATION" = "Release"; then :
  cd "/Users/andy/git repos/JUCE/build-character-arm64/vst3_helpers/CharacterCompressor"
  make -f /Users/andy/git\ repos/JUCE/build-character-arm64/vst3_helpers/CharacterCompressor/CMakeScripts/ReRunCMake.make
fi
if test "$CONFIGURATION" = "MinSizeRel"; then :
  cd "/Users/andy/git repos/JUCE/build-character-arm64/vst3_helpers/CharacterCompressor"
  make -f /Users/andy/git\ repos/JUCE/build-character-arm64/vst3_helpers/CharacterCompressor/CMakeScripts/ReRunCMake.make
fi
if test "$CONFIGURATION" = "RelWithDebInfo"; then :
  cd "/Users/andy/git repos/JUCE/build-character-arm64/vst3_helpers/CharacterCompressor"
  make -f /Users/andy/git\ repos/JUCE/build-character-arm64/vst3_helpers/CharacterCompressor/CMakeScripts/ReRunCMake.make
fi

