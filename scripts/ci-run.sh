#!/bin/bash
# Usage: ci-run.sh "<title>" <command...>
# Runs a command, keeps its output, and on failure publishes the log tail as a GitHub annotation
# (annotations are visible without logging in, unlike raw logs).
title="$1"; shift
"$@" > ci-step.log 2>&1
code=$?
cat ci-step.log
if [ $code -ne 0 ]; then
  msg=$(tail -n 60 ci-step.log | sed -e 's/%/%25/g' -e ':a;N;$!ba;s/\r//g;s/\n/%0A/g')
  echo "::error title=${title} (exit ${code})::${msg}"
else
  echo "::notice title=${title}::OK"
fi
exit 0
