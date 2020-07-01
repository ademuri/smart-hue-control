#!/bin/bash
# Copyright 2020 Google LLC
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.


set -euo pipefail
constants_exists=
if [ ! -f "constants/constants.h" ]; then
  cp constants/constants.sample.h constants/constants.h
  constants_exists=true
fi

pushd motion-sensor
platformio run
popd

pushd daylight-simulator
platformio run
popd

if [ "$constants_exists" = true ]; then
  rm constants/constants.h
fi
