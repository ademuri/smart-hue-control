#!/bin/bash

set -euo pipefail

pushd motion-sensor
constants_exists=

if [ ! -f "lib/constants/constants.h" ]; then
  cp lib/constants/constants.sample.h lib/constants/constants.h
  constants_exists=true
fi

platformio run
if [ "$constants_exists" = true]; then
  rm lib/constants/constants.h
fi

popd
