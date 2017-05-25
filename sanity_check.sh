#!/bin/bash

BESSCTL='./bessctl/bessctl'
SCRIPTS='./bessctl/conf'
OUTFILE='sanity_check.out'

TESTS='samples/exactmatch.bess
  samples/flowgen.bess
  samples/generic_encap.bess
  samples/hash_lb.bess
  samples/igate.bess
  samples/iplookup.bess
  samples/l2_forward.bess
  samples/multicore.bess
  samples/queue.bess
  samples/roundrobin.bess
  samples/s2s.bess
  samples/tc/complextree.bess
  samples/unix_port.bess
  samples/update.bess
  samples/vlantest.bess
  samples/wildcardmatch.bess
  samples/nat.bess
  samples/worker_split.bess
  samples/qtest.bess
  testing/test_constraint_checker.bess'

function fail
{
  echo "Test failed. Sorry."
  $BESSCTL daemon stop
  exit 2
}

echo "This script runs a collection of BESS sample scripts and makes sure nothing blows up. Sit back and relax."
rm -f $OUTFILE

for file in $TESTS
do
  echo "Running $file..."
  $BESSCTL daemon start -- run file $SCRIPTS/$file 2>&1 >>$OUTFILE
  success=$?
  if [ $success -ne 0 ] 
  then
    fail
  fi
  sleep 15
  $BESSCTL daemon stop
  success=$?
  if [ $success -ne 0 ] 
  then
    fail
  fi
done

echo "Tests complete!"
exit 0
