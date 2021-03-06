#!/bin/bash

BASE_PATH=${HOME}/mtyiu
MEMEC_PATH=${BASE_PATH}/memec
CONTROL_NODE=testbed-node10

c=$1 # coding
t=$2 # threads
w=$3 # workload

mkdir -p ${BASE_PATH}/results/encoding/$c/$t

echo "Running experiment with coding scheme = $c and thread count = $t..."

if [ $w == "load" ]; then
	echo "-------------------- Load (workloada) --------------------"
	${MEMEC_PATH}/scripts/ncs-10g/ycsb/memec/load.sh $t 2>&1 | tee ${BASE_PATH}/results/encoding/$c/$t/$w.txt
else
	echo "-------------------- Run ($w) --------------------"
	${MEMEC_PATH}/scripts/ncs-10g/ycsb/memec/run.sh $t $w 2>&1 | tee ${BASE_PATH}/results/encoding/$c/$t/$w.txt
fi

# Tell the control node that this iteration is finished
ssh testbed-node10 "screen -S experiment -p 0 -X stuff \"$(printf '\r')\""

echo "Finished experiment with coding scheme = $c and thread count = $t..."
