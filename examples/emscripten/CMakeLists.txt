#
# This file is part of AtomVM.
#
# Copyright 2023 Paul Guyot <pguyot@kallisys.net>
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#    http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# SPDX-License-Identifier: Apache-2.0 OR LGPL-2.1-or-later
#

project(examples_emscripten)

include(BuildErlang)

pack_runnable(run_script run_script estdlib eavmlib)
pack_runnable(call_cast call_cast eavmlib)
pack_runnable(html5_events html5_events estdlib eavmlib)
pack_runnable(echo_websocket echo_websocket estdlib eavmlib)
pack_runnable(wasm_webserver wasm_webserver estdlib eavmlib)
