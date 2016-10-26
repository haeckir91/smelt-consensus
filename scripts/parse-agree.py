
import os.path
import csv

global label_lookup
label_lookup = {
    0: '1Paxos',
    1: 'TPC',
    2: 'Broad',
    3: 'Chain',
    4: 'SHM',
    5: 'NONE'
}   

global fname_lookup
fname_lookup = {
    0: 'one',
    1: 'tpc',
    2: 'broad',
    3: 'chain'
}   

global alg_below
alg_below = [4,5]

def parse_tp_file(name):   
    if os.path.isfile(name):
        print(name)
        f = open(name, 'r')
        reader = csv.reader(f, dialect='excel', delimiter='\t')
        for row in reader:
            if (row[0] == '||'):
                return [int(float(row[1])/1000), int(float(row[2])/1000)]
    else:
        return []

def aggregate_clients_rt(directory, key, alg, num_clients):
    aggr = []
    for n_c in range(0, num_clients):
        fname = '%sclient_id_%d_algo_%d_below_%d_num_%d' \
                %(directory, n_c, key, alg, num_clients) 
        tmp = parse_tp_file(fname)
        if tmp:
            aggr.append(tmp)

    if not aggr:
        return []

    stdv = 0 
    avg = 0
    for item in aggr:
        stdv+= item[1]
        avg+= item[0]
                
    stdv = stdv/len(aggr)
    avg = avg/len(aggr)
    return [avg, stdv]

def aggregate_clients_rt_libsync(directory, key, alg, num_clients, tree):
    aggr = []
    for n_c in range(0, num_clients):
        fname = '%sclient_id_%d_algo_%d_below_%d_%s_num_%d' \
                %(directory, n_c, key, alg, tree, num_clients)  
        tmp = parse_tp_file(fname)
        if tmp:
            aggr.append(tmp)
 
    if not aggr:
        return []     
    
    stdv = 0 
    avg = 0
#    max_stdv = 0 
#    min_stdv = 0 
#    min_avg = 0
#    max_avg = 0
    for item in aggr:
        stdv+= item[1]
#        max_stdv+= item[3]
#        min_stdv+= item[5]
        avg+= item[0]
#        max_avg+= item[2]
#        min_avg+= item[4]
                
    stdv = stdv/len(aggr)
    avg = avg/len(aggr)
#    max_stdv = max_stdv/len(aggr)
#    max_avg = max_avg/len(aggr)
#    min_stdv = min_stdv/len(aggr)
#    min_avg = min_avg/len(aggr)

    return [avg, stdv]


def parse_log(directory, num_rep, num_clients, rt, tree):
    print('Parsing raw output from directory %s' %directory)
    
    if rt:
        data = []
        for key in fname_lookup:
            for alg in alg_below:
                for num_replicas in range(0, num_rep+1):
                    dname = '%srep_%s/'%(directory,num_replicas)
                    aggr = aggregate_clients_rt(dname, key, alg, num_clients)
                    if aggr:
                        data.append([num_replicas, label_lookup.get(key), 'sequential']+aggr)
     
        for key in fname_lookup:
            for num_replicas in range(0, num_rep+1):
                dname = '%srep_%s/'%(directory,num_replicas)
                aggr = aggregate_clients_rt_libsync(dname, key, 5, num_clients, tree)
                if aggr:
                    data.append([num_replicas, label_lookup.get(key), 'smelt', aggr[0], aggr[1]])

        print('Printing output to rt_r%d' %num_rep)
        output = 'rt_r%d' %num_rep
        f = open(output, 'w')

        for item in data:
            tmp_line = '%d\t%s\t"%s"\t%d\t%d\n' %(item[0], item[1], item[2], item[3], item[4])
            f.write(tmp_line)
        
        return data    
    else:
        data = []
        # get all non libsync tp data
        for key in fname_lookup:
            for alg in alg_below:
                for num_replicas in range(0, num_rep+1):
                    fname = '%stp_%s_below_%d_num_%d_numc_%d' \
                    %(directory, fname_lookup.get(key), alg, num_replicas, num_clients)     
                    tmp = parse_tp_file(fname)
                    if tmp:
                        if alg == 4:
                            data.append([num_replicas, label_lookup.get(key), 'hybrid']+tmp)
                        else:
                            data.append([num_replicas, label_lookup.get(key), 'sequential']+tmp)
                            fname = '%stp_%s_%s_num_%d_numc_%d' \
                            %(directory, tree, fname_lookup.get(key), num_replicas, num_clients)     
                            tmp = parse_tp_file(fname)
                            if tmp:
                                data.append([num_replicas, label_lookup.get(key), 'smelt']+tmp)


        print(data)
        print('Printing output to tp_r%d ' %num_rep)
        output = 'tp_r%d' %num_rep
        f = open(output, 'w')
        for item in data:
            tmp_line = '%d\t%s\t"%s"\t%d\t%d\n' %(item[0], item[1], item[2], item[3], item[4])
            f.write(tmp_line)
        
        return data    
                                             
import argparse
import os
parser = argparse.ArgumentParser()
parser.add_argument('--fpath')
parser.add_argument('--max_replicas')
parser.add_argument('--tree')
parser.set_defaults(fpath=os.path.dirname(os.path.realpath(__file__)),
                    num_clients=1, tree='adaptivetree')
arg = parser.parse_args()

parse_log(arg.fpath, int(arg.max_replicas), 4, False, arg.tree)
parse_log(arg.fpath, int(arg.max_replicas), 4, True, arg.tree)



