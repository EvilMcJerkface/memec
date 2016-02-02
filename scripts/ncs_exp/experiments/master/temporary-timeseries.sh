#!/bin/bash

BASE_PATH=${HOME}/mtyiu
PLIO_PATH=${BASE_PATH}/plio
HOSTNAME=$(hostname)
CONTROL_NODE=testbed-node10

t=64
w=$1 # worklaod

mkdir -p ${BASE_PATH}/results/temporary

if [ $w == "load" ]; then
	echo "-------------------- Load (workloada) --------------------"
	${BASE_PATH}/scripts/ycsb/plio/load.sh $t 2>&1 | tee ${BASE_PATH}/results/temporary/load.txt
else
	echo "-------------------- Run ($w) --------------------"
	${BASE_PATH}/scripts/ycsb/plio/run-hybrid-timeseries.sh $t $w 2>&1 | tee ${BASE_PATH}/results/temporary/$w.txt
fi

# Tell the control node that this iteration is finished
ssh testbed-node10 "screen -S experiment -p 0 -X stuff \"$(printf '\r')\""
