#!/bin/bash
ROOT="$(pwd)/.."
PRELOAD="$ROOT/build/arm-fp-emu.so"
LIB_PATH="$ROOT/contrib/keystone/build/llvm/lib"
EXEC_BIN="./build/vadd"
CMP_BIN="./build/getpid"

function one_iteration() {
	filename=$1
	format=$2
	gpid_iters=$3
	vadd_instrs_per_loop=$4

	EXEC_BIN="./build/"$filename
	vadd_iters=$(( $gpid_iters / $vadd_instrs_per_loop ))
	vadd_times=()
	getpid_times=()

	# Take 10 measurements for each data point
	for i in {1..10}; do
		vadd_times+=($(command time -f $format bash -c "LD_LIBRARY_PATH=$LIB_PATH LD_PRELOAD=$PRELOAD $EXEC_BIN $vadd_iters" 2>&1))
        getpid_times+=($(command time -f $format bash -c "$CMP_BIN $gpid_iters" 2>&1))
	done

	echo "$gpid_iters ${vadd_times[*]} ${getpid_times[*]}"
}

function benchmark() {
	filename=$1
	format=$2
	increment=$3
	max_iters=$4
	vadd_instrs_per_loop=$5

	for iter in $(seq $increment $increment $max_iters); do
		one_iteration $filename $format $iter $vadd_instrs_per_loop
	done
}

increment=$(( 1 * ( 10 ** 4 ) ))
max_iters=$(( 5 * ( 10 ** 5 ) ))

for i in 10 100 1000; do
	echo "vadd"$i
	benchmark "vadd"$i "%U" $increment $max_iters $i
done
