#!/bin/bash

BASE_PATH=${HOME}/mtyiu
MEMEC_PATH=${BASE_PATH}/memec

coding=raid0
threads=64
workload=load

function set_overload {
	bd=10
	for n in 23; do
		echo "Lowering the bandwidth of node $n to $bd Mbps"
		ssh testbed-node$n "screen -S ethtool -p 0 -X stuff \"sudo ethtool -s eth0 speed $bd duplex full $(printf '\r')\""
		sleep 2
	done
}

function restore_overload {
	obd=1000
	for n in 23; do
		echo "Restoring the bandwidth of node $n to $obd Mbps"
		ssh testbed-node$n "screen -S ethtool -p 0 -X stuff \"sudo ethtool -s eth0 speed $obd duplex full $(printf '\r')\""
		sleep 2
	done
}

function set_config {
	sed -i "s/^smoothingFactor=.*$/smoothingFactor=$1/g" ${MEMEC_PATH}/bin/config/ncs/global.ini
	sed -i "s/^updateInterval=.*$/updateInterval=$2/g" ${MEMEC_PATH}/bin/config/ncs/coordinator.ini
	sed -i "s/^updateInterval=.*$/updateInterval=$2/g" ${MEMEC_PATH}/bin/config/ncs/client.ini
}

if [ $# != 2 ]; then
	echo "Usage: $0 [Smoothing factor] [Statistics exchange frequency]"
	exit 1
fi

smoothing_factor=$1
stat_exchange_freq=$2
# for smoothing_factor in 0.1 0.2 0.3 0.4 0.5; do
# 	for stat_exchange_freq in 10 50 100 500 1000; do
		set_config $smoothing_factor $stat_exchange_freq
		${BASE_PATH}/scripts/util/rsync.sh

		for iter in {1..1}; do
			echo "Preparing for the experiment (smoothing factor = ${smoothing_factor}, statistics exchange frequency = ${stat_exchange_freq})..."

			screen -S manage -p 0 -X stuff "${BASE_PATH}/scripts/util/start.sh $(printf '\r')"
			read -p "Press Enter when ready..."

			for n in 3 4 8 9; do
				ssh testbed-node$n "screen -S ycsb -p 0 -X stuff \"${BASE_PATH}/scripts/experiments/client/workloads-memec.sh $coding $threads $workload $(printf '\r')\"" &
			done

			sleep 3
			ssh testbed-node1 "screen -S coordinator -p 0 -X stuff \"time $(printf '\r')\""
			set_overload

			read -p "Press Enter when completed..."

			restore_overload

			for n in 3 4 8 9; do
				ssh testbed-node$n "killall -9 python"
			done

			echo "Done"

			screen -S manage -p 0 -X stuff "$(printf '\r\r')"
			sleep 10
		done
# 	done
# done
