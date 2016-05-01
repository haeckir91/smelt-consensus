#!/bin/bash
#All non libsync benchmarks


declare -a arr=(config_files/config_c1.txt \
                config_files/config_c2.txt \
                config_files/config_c3.txt \
                config_files/config_c4.txt )

declare -a arr2=(config_files/config_libsync_c1.txt \
                 config_files/config_libsync_c2.txt \
                 config_files/config_libsync_c3.txt \
                 config_files/config_libsync_c4.txt )

# run shared memory hybrid
for i in {0..3}
do
    for j in "${arr[@]}"
    do
        ./start_bench $i 4 "$j"
    done
done

#run single tier
for i in {0..3}
do
    for j in "${arr2[@]}"
    do
        ./start_bench $i 5 "$j" 
    done
done

#run with libsync
#run for cluste tree for now (most likely correct model)
for i in {0..2}
do
        for z in "${arr2[@]}"
        do
          ./start_bench_libsync $i 5 "$z" 0
        done
done
