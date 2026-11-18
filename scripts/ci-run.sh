#!/bin/bash
# Usage: ci-run.sh "<title>" <command...>
# Runs a command, keeps its output, and on failure publishes the log tail as a GitHub annotation
# (annotations are visible without logging in, unlike raw logs). BSD/macOS-safe (no GNU sed tricks).
title="$1"; shift
"$@" > ci-step.log 2>&1
code=$?
cat ci-step.log
enc() { awk '{ gsub(/%/, "%25"); gsub(/\r/, ""); printf "%s%%0A", $0 }'; }
if [ $code -ne 0 ]; then
  echo "::error title=${title} (exit ${code})::$(tail -n 60 ci-step.log | enc)"
fi
if grep -q "FAIL" ci-step.log; then
  echo "::error title=${title} failed checks::$(grep -E 'FAIL|^\[' ci-step.log | grep -B1 FAIL | enc)"
fi
[ $code -eq 0 ] && echo "::notice title=${title}::OK"
exit 0
