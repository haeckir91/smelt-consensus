#!/usr/bin/python
import matplotlib
matplotlib.use('Agg')

import numpy
import matplotlib.pyplot as plt
from matplotlib.backends.backend_pdf import PdfPages

import brewer2mpl
import plotsetup

import sys
import os

import matplotlib
fontsize = 19

import matplotlib
matplotlib.rcParams['figure.figsize'] = 8.0, 4.5
plt.rc('legend',**{'fontsize':fontsize, 'frameon': 'false'})
matplotlib.rc('font', family='serif') 
matplotlib.rc('font', serif='Times New Roman') 
matplotlib.rc('text', usetex='true') 
matplotlib.rcParams.update({'font.size': fontsize, 'xtick.labelsize':fontsize})

def configure_plot(ax):
    ax.spines['top'].set_visible(False)
    ax.spines['right'].set_visible(False)
    ax.get_xaxis().tick_bottom()
    ax.get_yaxis().tick_left()

#
# Barchart
# 
def multi_bar_chart(plotdata, plotname, rt):


    bars = [
        "1Paxos sequential",
        "Broad sequential",
        "2PC sequential",
        "1Paxos smelt",
        "Broad smelt",
        "2PC smelt",
    ]
    
    protocols = [
        "1Paxos",
        "Broad",
        "TPC",
    ]

    labels = [
        "8",
        "12",
        "16",
        "20",
        "24",
        "28",
    ]

    N = len(plotdata)/6
    ind = numpy.arange(N)
    width = 1./(len(bars) + 1)

    # Plot the data
    if rt:
        plotname = '%s-rt.pdf' % plotname
    else: 
        plotname = '%s-tp.pdf' % plotname

    with PdfPages(plotname) as pdf:

        fig, ax = plt.subplots()
        colors = brewer2mpl.get_map('PuOr', 'diverging', 9).mpl_colors
        hs = [ '.', '/', '//', None,  '\\', '\\\\', '*', None, 'o' ]

        legends = []
        n = 0

        # One bar per algorithm

        for b in bars:
            v = []
            yerr = []

            for num_rep in range(0, 100):
                for item in plotdata:
                    if item:
                        if ((item[2] in b) and (item[0] == num_rep) and (item[1] in b)):
                            v.append(item[3])
                            yerr.append(item[4])
                
            if v:
                r = ax.bar(ind+n*width, v, width, \
                           color=colors[n], hatch=hs[n], yerr=yerr, \
                           error_kw=dict(ecolor='gray'))
                legends.append((r, b))
                n+=1

        ax.set_xlabel('Number of replicas')
        if rt:
            ax.set_ylabel('Response time [x1000 cycles]')
            ax.set_ylim([0,250])
        else:
            ax.set_ylim([0,850])
            ax.set_ylabel('Throughput [x1000 agreements/s]')

        ax.set_xticks(ind + n/2.0*width)
        ax.set_xticklabels(labels)
        ax.set_ylim(ymin=0)
        configure_plot(ax)
        lgnd_boxes, lgnd_labels = zip(*legends)

        ax.legend( lgnd_boxes, lgnd_labels, loc=2, ncol=2, borderaxespad=0., 
                  mode="expand",
                  bbox_to_anchor=(0.0, 1.01, 1., .102))
        pdf.savefig(bbox_inches='tight')

# Parse data
#
import csv
import argparse  
import collections  
parser = argparse.ArgumentParser()
#parser.add_argument('--path')
parser.add_argument('--plotname')
parser.set_defaults(plotname='plot')
args = parser.parse_args()

data = []
for i in range(0,100):
    fname = 'rt_r%d' % (i)
    if os.path.isfile(fname):
        with open(fname, 'r') as f:
            reader = csv.reader(f, dialect='excel', delimiter='\t')
            for row in reader:
                if row:
                    data.append((int(row[0]), row[1], row[2], int(row[3]), int(row[4])));
multi_bar_chart(data, args.plotname, True)
print(data)

data = []
for i in range(0,100):
    fname = 'tp_r%d' % (i)
    if os.path.isfile(fname):
        with open(fname, 'r') as f:
            reader = csv.reader(f, dialect='excel', delimiter='\t')
            for row in reader:
                if row:
                    data.append((int(row[0]), row[1], row[2], int(row[3]), int(row[4])));
multi_bar_chart(data, args.plotname, False)
print(data)

exit(0)

