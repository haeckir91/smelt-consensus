#!/bin/bash

path=$1
machine=$2
echo $path

for i in {1..4}
do
    python parse_logs.py --fpath $path --num_clients $i 
    python parse_logs.py --fpath $path --num_clients $i --rt True
done

for i in {1..4}
do
    python plot-framework-linux.py --path tp_c$i --plotname $machine-tp-c$i 
    python plot-framework-linux.py --path rt_c$i --plotname $machine-rt-c$i --rt True
done
