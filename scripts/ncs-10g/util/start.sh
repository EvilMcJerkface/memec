#!/bin/bash

BASE_PATH=${HOME}/mtyiu
MEMEC_PATH=${BASE_PATH}/memec
BOOTSTRAP_SCRIPT_PATH=${MEMEC_PATH}/scripts/ncs-10g/bootstrap

SLEEP_TIME=2

if [ $# -gt 0 ]; then
	echo "*** Warning: Debug mode is enabled. ***"
fi

# ./set_config.sh

ssh testbed-node30 "screen -S coordinator -p 0 -X stuff \"$(printf '\r\r')${BOOTSTRAP_SCRIPT_PATH}/start-coordinator.sh ${1}$(printf '\r\r')\""

sleep ${SLEEP_TIME}

for i in {11..20}; do
	ssh testbed-node$i "screen -S server -p 0 -X stuff \"$(printf '\r\r')${BOOTSTRAP_SCRIPT_PATH}/start-server.sh ${1}$(printf '\r\r')\""
done

sleep ${SLEEP_TIME}

for i in {31..34}; do
	ssh testbed-node$i "screen -S client -p 0 -X stuff \"$(printf '\r\r')${BOOTSTRAP_SCRIPT_PATH}/start-client.sh ${1}$(printf '\r\r')\""
done

sleep ${SLEEP_TIME}

echo 1 > RUNNING
read -p "Press Enter to terminate all instances..."

for i in {11..20} {30..34}; do
	ssh testbed-node$i 'killall -9 application coordinator client server ycsb 1>&2 2> /dev/null' &
done

sleep ${SLEEP_TIME}

if [ $# -gt 0 ]; then
	# Debug mode
	TERM_COMMAND="$(printf '\r\r')quit$(printf '\r')clear$(printf '\r')"
else
	TERM_COMMAND="$(printf '\r\r')clear$(printf '\r')"
fi

for i in {11..20}; do
	ssh testbed-node$i "screen -S server -p 0 -X stuff \"${TERM_COMMAND}\"" &
done
for i in {31..34}; do
	ssh testbed-node$i "screen -S client -p 0 -X stuff \"${TERM_COMMAND}\"" &
	ssh testbed-node$i "screen -S ycsb -p 0 -X stuff \"${TERM_COMMAND}\"" &
done
ssh testbed-node30 "screen -S coordinator -p 0 -X stuff \"${TERM_COMMAND}\"" &

sleep ${SLEEP_TIME}

echo 0 > RUNNING

clear
