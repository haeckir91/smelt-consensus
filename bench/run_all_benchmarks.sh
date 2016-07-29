#!/bin/bash

function error() {
	echo $1
	exit 1
}

while [[ $RUN -eq 1 ]]; do

	echo "checking argument .. $1"

	case "$1" in

		"-d")
			echo "Executing in debug mode"
			DEBUG=1
			shift
			;;

		*)
			RUN=0 # Stop loop execution
			echo "No more arguments to parse .. "

	esac
done

declare -a arr2=(config_files/config_r815_8_smlt \
                 config_files/config_r815_12_smlt \
                 config_files/config_r815_16_smlt \
                 config_files/config_r815_20_smlt \
                 config_files/config_r815_24_smlt \
                 config_files/config_r815_28_smlt )

# run shared memory hybrid
# for i in {0..3}
# do
#     for j in "${arr[@]}"
#     do
#         ./start_bench $i 4 "$j"
#     done
# done

export LD_LIBRARY_PATH=.:$LD_LIBRARY_PATH

# run single tier
for j in "${arr2[@]}"
do
    ./start_bench 0 6 "$j"       || error "Failed to execute ./start bench 0 6"
    ./start_bench_smelt 0 6 "$j" || error "Failed to execute ./start bench_smelt 0 6"
    ./start_bench 2 6 "$j"       || error "Failed to execute ./start bench 2 6"
    ./start_bench_smelt 2 6 "$j" || error "Failed to execute ./start bench_smelt 2 6"
done

exit 0
