#!/bin/bash

BASE_PATH=${HOME}/mtyiu
PLIO_PATH=${BASE_PATH}/plio

t=64
w=$1 # worklaod

mkdir -p ${BASE_PATH}/results/degraded

if [ $w == "load" ]; then
	echo "-------------------- Load (workloada) --------------------"
	${BASE_PATH}/scripts/ycsb/plio/load.sh $t 2>&1 | tee ${BASE_PATH}/results/degraded/load.txt
else
	echo "-------------------- Run ($w) --------------------"
	${BASE_PATH}/scripts/ycsb/plio/run.sh $t $w 2>&1 | tee ${BASE_PATH}/results/degraded/$w.txt
fi

# Tell the control node that this iteration is finished
ssh coordinator "screen -S experiment -p 0 -X stuff \"$(printf '\r')\""
