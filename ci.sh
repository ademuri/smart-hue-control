#!/bin/bash

set -euo pipefail

pushd motion-sensor

if [ ! -f "lib/constants/constants.h" ]; then
  cp lib/constants/constants.sample.h lib/constants/constants.h
fi

platformio run
rm lib/constants/constants.h

popd
