#!/bin/bash
# Live view of the spike extensions' own log messages. Leave this running in a
# second terminal while poking at Finder.
#
# Note: /usr/bin/log explicitly -- some shells have a `log` builtin or alias.
exec /usr/bin/log stream \
  --predicate 'subsystem == "io.github.bizmar.exr-quicklook"' \
  --info --debug --style compact
