#!/bin/sh
set -e
if test "$CONFIGURATION" = "Custom"; then :
  cd "/Users/andy/git repos/JUCE/build-character-arm64/JUCE/tools"
  make -f /Users/andy/git\ repos/JUCE/build-character-arm64/JUCE/tools/CMakeScripts/ReRunCMake.make
fi

