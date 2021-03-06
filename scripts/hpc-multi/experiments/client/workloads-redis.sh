#!/bin/bash

BASE_PATH=${HOME}/mtyiu
MEMEC_PATH=${BASE_PATH}/memec
HOSTNAME=$(hostname | sed 's/.cse.cuhk.edu.hk//g')
CONTROL_NODE=hpc15

c=$1 # coding
t=$2 # threads
w=$3 # worklaod

mkdir -p ${BASE_PATH}/results-multi/workloads/tmp/${HOSTNAME}/$c/$t

echo "Running experiment with coding scheme = $c and thread count = $t..."

# Run workload A, B, C, F, D first
if [ $w == "load" ]; then
	echo "-------------------- Load (workloada) --------------------"
	${BASE_PATH}/scripts-multi/ycsb/redis/load.sh $t 2>&1 | tee ${BASE_PATH}/results-multi/workloads/tmp/${HOSTNAME}/$c/$t/load.txt
else
	echo "-------------------- Run ($w) --------------------"
	${BASE_PATH}/scripts-multi/ycsb/redis/run.sh $t $w 2>&1 | tee ${BASE_PATH}/results-multi/workloads/tmp/${HOSTNAME}/$c/$t/$w.txt
fi

# Tell the control node that this iteration is finished
ssh ${CONTROL_NODE} "screen -S experiment -p 0 -X stuff \"$(printf '\r')\""

echo "Finished experiment with coding scheme = $c and thread count = $t..."
