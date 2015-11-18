#!/bin/bash

BASE_PATH=${HOME}/mtyiu
PLIO_PATH=${BASE_PATH}/plio
HOSTNAME=$(hostname)

coding='raid0 raid1 raid5 rdp cauchy rs evenodd'
threads=64 # '16 32 64 128 256 512 1000'
workloads='load workloada workloadb workloadc workloadf workloadd'

for c in $coding; do
	echo "Preparing for the experiments with coding scheme = $c..."

	sed -i "s/^scheme=.*$/scheme=$c/g" ${PLIO_PATH}/bin/config/ncs_exp/global.ini

	${BASE_PATH}/scripts/util/rsync.sh

	mkdir -p ${BASE_PATH}/results/workloads/$c

	for t in $threads; do
		mkdir -p ${BASE_PATH}/results/workloads/$c/$t
		echo "Running experiment with coding scheme = $c and thread count = $t..."

		# Run workload A, B, C, F, D first
		screen -S manage -p 0 -X stuff "${BASE_PATH}/scripts/util/start.sh $1$(printf '\r')"
		sleep 30

		for w in $workloads; do
			if [ $w == "load" ]; then
				echo "-------------------- Load --------------------"
			else; then
				echo "-------------------- Run ($w) --------------------"
			fi

			for n in 3 4 8 9; do
				ssh testbed-node$n "screen -S ycsb -p 0 -X stuff \"${BASE_PATH}/scripts/experiments/master/workloads.sh $c $t $w $(printf '\r')\"" &
			done

			pending=0
			for n in 3 4 8 9; do
				read -p "Pending: ${pending} / 4"
				pending=$(expr $pending + 1)
			done

			for n in 3 4 8 9; do
				scp testbed-node$n:${BASE_PATH}/results/workloads/$c/$t/$w.txt ${BASE_PATH}/results/workloads/$c/$t/node$n/$w.txt
			done
		done

		screen -S manage -p 0 -X stuff "$(printf '\r\r')"
		sleep 10
		echo "Finished experiment with coding scheme = $c and thread count = $t..."
	done
done

sed -i "s/^scheme=.*$/scheme=raid0/g" ${PLIO_PATH}/bin/config/ncs_exp/global.ini

${BASE_PATH}/scripts/util/rsync.sh
