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
def multi_bar_chart(plotdata, plotname, num_clients, w, rt):


    bars = [
        "sequential",
        "smelt",
    ]
    
    labels = ['1',
              '4',
              '8',
              '12',
              '16',
              '20',
              '24']

    N = len(plotdata)/2
    ind = numpy.arange(N)
    width = 1./(len(bars) + 1)

    print(plotdata)
    # Plot the data
    if rt:
        if w:
            plotname = '%s-w-rt.pdf' % plotname
        else:
            plotname = '%s-r-rt.pdf' % plotname
    else: 
        if w:
            plotname = '%s-w-tp.pdf' % plotname
        else:
            plotname = '%s-r-tp.pdf' % plotname

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
            for (num_clients, data) in plotdata.items():
                if w:
                    for item in data:
                        if item:
                            if ((item[0]==b) and ('w' in num_clients)):
                                v.append(item[1])
                                yerr.append(item[2])
                else:
                    for item in data:
                        if item:
                            if ((item[0]==b) and ('r' in num_clients)):
                                v.append(item[1])
                                yerr.append(item[2])

            print(v)
            print(yerr)
            r = ax.bar(ind+n*width, v, width, \
                       color=colors[n], hatch=hs[n], yerr=yerr, \
                       error_kw=dict(ecolor='gray'))
            legends.append((r, b))
            n+=1

        ax.set_xlabel('Number of clients')
        if rt:
            if w:
                ax.set_ylabel('Set time [x1000 cycles]')
            else:
                ax.set_ylabel('Get time [x1000 cycles]')
        else:
            if w:
                ax.set_ylabel('Set throughput [x1000 sets/s]')
            else:
                ax.set_ylabel('Get throughput [x1000 gets/s]')

        ax.set_xticks(ind + n/2.0*width)
        ax.set_xticklabels(labels)# , rotation=90)
        ax.set_ylim(ymin=0)
        configure_plot(ax)
        lgnd_boxes, lgnd_labels = zip(*legends)

        ax.legend( lgnd_boxes, lgnd_labels, loc=2, ncol=3, borderaxespad=0., 
                  mode="expand",
                  bbox_to_anchor=(0.0, 1.01, 1., .101))
        pdf.savefig(bbox_inches='tight')

# Parse data
#
import csv
import argparse  
import collections  
parser = argparse.ArgumentParser()
#parser.add_argument('--path')
parser.add_argument('--plotname')
parser.add_argument('--max_clients')
parser.set_defaults(plotname='plot')
args = parser.parse_args()


plot_data = {}
fname = 'tp_numc%s'%args.max_clients
num_clients = int(args.max_clients)

con_look = {'1':'a',
            '4':'b',
            '8':'c',
            '12':'d',
            '16':'e',
            '20':'f',
            '24':'g'}

plot_data = {
             'a w':[],
             'a r':[],
             'b w':[],
             'b r':[],
             'c w':[],
             'c r':[],
             'd w':[],
             'd r':[],
             'e w':[],
             'e r':[],
             'f w':[],
             'f r':[],
             'g w':[],
             'g r':[],
            }


if os.path.isfile(fname):
    with open(fname, 'r') as f:
        reader = csv.reader(f, dialect='excel', delimiter='\t')
        for row in reader:
            if row:
                    plot_data[con_look[row[0]]+' w'].append((row[1], int(row[2]), int(row[3])));
                    plot_data[con_look[row[0]]+' r'].append((row[1], int(row[4]), int(row[5])));

plot_data = collections.OrderedDict(sorted(plot_data.items()))
multi_bar_chart(plot_data, args.plotname, num_clients, True, False)
multi_bar_chart(plot_data, args.plotname, num_clients, False, False)

plot_data = {
             'a w':[],
             'a r':[],
             'b w':[],
             'b r':[],
             'c w':[],
             'c r':[],
             'd w':[],
             'd r':[],
             'e w':[],
             'e r':[],
             'f w':[],
             'f r':[],
             'g w':[],
             'g r':[],
            }
fname = 'rt_numc%s'%args.max_clients
if os.path.isfile(fname):
    with open(fname, 'r') as f:
        reader = csv.reader(f, dialect='excel', delimiter='\t')
        for row in reader:
            if row:
                    plot_data[con_look[row[0]]+' w'].append((row[1], int(row[2]), int(row[3])));
                    plot_data[con_look[row[0]]+' r'].append((row[1], int(row[4]), int(row[5])));


plot_data = collections.OrderedDict(sorted(plot_data.items()))
multi_bar_chart(plot_data, args.plotname, num_clients, True, True)
multi_bar_chart(plot_data, args.plotname, num_clients, False, True)

exit(0)

