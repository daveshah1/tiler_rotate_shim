#!/bin/bash
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"
export LD_LIBRARY_PATH=$DIR:$LD_LIBRARY_PATH
export LIBGL_FB=1
export LD_PRELOAD=$DIR/tiler_shim.so
$@
