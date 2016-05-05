#!/bin/bash
#All non libsync benchmarks

declare -a arr2=(config_files/config_r815_8_smlt \
                 config_files/config_r815_12_smlt \
                 config_files/config_r815_16_smlt \
                 config_files/config_r815_20_smlt \
                 config_files/config_r815_24_smlt \ 
                 config_files/config_r815_28_smlt )

# run shared memory hybrid
<<comment
for i in {0..3}
do
    for j in "${arr[@]}"
    do
        ./start_bench $i 4 "$j"
    done
done
comment

#run single tier
for j in "${arr2[@]}"
do
    ./start_bench 0 6 "$j" 
    ./start_bench_smelt 0 6 "$j" 
    ./start_bench 2 6 "$j" 
    ./start_bench_smelt 2 6 "$j" 
done
