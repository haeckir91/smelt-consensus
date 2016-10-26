
import os.path
import csv

def parse_file(name, rt):
    if os.path.isfile(name):
        print(name)
        f = open(name, 'r')
        reader = csv.reader(f, dialect='excel', delimiter='\t')
        for row in reader:
            if (row[0] == '||'):
                if rt:
                    return [int(float(row[1])/1000), int(float(row[2])/1000), 
                            int(float(row[5])/1000), int(float(row[6])/1000)]
                else:
                    return [int(float(row[3])/1000), int(float(row[4])/1000), 
                            int(float(row[7])/1000), int(float(row[8])/1000)]
    else:
        return []

def aggregate_clients(directory, num_clients, libsync, rt):
    aggr = []
    for n_c in range(0, num_clients):
        if libsync:
            fname = '%sclient_kvs_adaptivetree_id_%d_num_%d' \
                    %(directory, n_c, num_clients) 
            tmp = parse_file(fname, rt)
            if tmp:
                aggr.append(tmp)
        else:
            fname = '%sclient_kvs_id_%d_num_%d' \
                    %(directory, n_c, num_clients) 
            tmp = parse_file(fname, rt)
            if tmp:
                aggr.append(tmp)

    if not aggr:
        return []

    w_stdv = 0 
    w_avg = 0
    r_stdv = 0 
    r_avg = 0

    for item in aggr:
        w_avg+= item[0]      
        w_stdv+= item[1]
        r_avg+= item[2]
        r_stdv+= item[3]

    if rt:
        w_stdv = w_stdv/len(aggr)
        w_avg = w_avg/len(aggr)
        r_stdv = r_stdv/len(aggr)
        r_avg = r_avg/len(aggr)
    else:
        w_stdv = w_stdv/len(aggr)
        r_stdv = r_stdv/len(aggr)

    return [w_avg, w_stdv, r_avg, r_stdv]


def parse_log(directory, num_clients, rt):
    data = []
    for i in range(0, num_clients+1):
        aggr = aggregate_clients(directory, i, False, rt)
        if aggr:
            data.append([i, 'sequential']+aggr)
     
    for i in range(0, num_clients+1):
        aggr = aggregate_clients(directory, i, True, rt)
        if aggr:
            data.append([i, 'smelt']+aggr)

    if rt:
        output = 'rt_numc%d' %num_clients
    else:
        output = 'tp_numc%d' %num_clients

    print('Printing output to %s ' %output)

    f = open(output, 'w')
    for item in data:
        tmp_line = '%s\t"%s"\t%d\t%d\t%d\t%d\n' %(item[0], item[1], item[2], item[3], item[4], item[5])
        f.write(tmp_line)

    print(data)
    return data    

                                             
import argparse
import os
parser = argparse.ArgumentParser()
parser.add_argument('--fpath')
parser.add_argument('--max_clients')
parser.set_defaults(fpath=os.path.dirname(os.path.realpath(__file__)))
arg = parser.parse_args()

print('Parsing raw output from directory %s' %arg.fpath)
    
num_clients = int(arg.max_clients)

parse_log(arg.fpath, num_clients, False)
parse_log(arg.fpath, num_clients, True)



