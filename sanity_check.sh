#!/bin/bash

BESSCTL='./bessctl/bessctl'
SCRIPTS='./bessctl/conf/samples'
OUTFILE='sanity_check.out'

TESTS='exactmatch.bess
	flowgen.bess
	generic_encap.bess
	hash_lb.bess
	igate.bess
	iplookup.bess
	l2_forward.bess
	multicore.bess
	queue.bess
	roundrobin.bess
	s2s.bess
	unix_port.bess
	update.bess
	vlantest.bess
	wildcardmatch.bess'

function fail
{
  echo "Test failed. Sorry."
  $BESSCTL daemon stop
  exit 2
}

echo "This script runs a collection of BESS sample scripts and makes sure \
nothing blows up. Sit back and relax."
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
  sleep 25
  $BESSCTL daemon stop
  success=$?
  if [ $success -ne 0 ] 
  then
    fail
  fi
done

echo "Tests complete!"
exit 0
