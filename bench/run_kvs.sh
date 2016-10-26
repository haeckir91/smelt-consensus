#!/bin/bash
#All non libsync benchmarks


declare -a arr=(config_files/config_kvs_c1.txt \
                config_files/config_kvs_c4.txt \
                config_files/config_kvs_c8.txt \
                config_files/config_kvs_c12.txt\
                config_files/config_kvs_c16.txt\
                config_files/config_kvs_c20.txt\
                config_files/config_kvs_c24.txt)

# single tier
for j in "${arr[@]}"
do
    ./start_bench 0 6 "$j"
done

# single tier
for j in "${arr[@]}"
do
    ./start_bench_smelt 0 6 "$j" 0
done

