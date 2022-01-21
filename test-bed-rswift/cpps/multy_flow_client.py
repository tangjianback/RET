# -*- coding: utf-8 -*-
import os
import signal
import time
import datetime
from subprocess import Popen, PIPE,signal

input("make sure you are at python3\n")
flow_num = input('process num')
base_port = 8880
k = Popen('make', stdout=PIPE, shell=True)
k.wait()

for line in k.stdout.readlines():
    print(line)


process_list = []
for i in range(int(flow_num)): 
    p = Popen(['./rc_pingpong_client', '-p', str(base_port+ i),'-z',str(i)] , stdout= PIPE)
    #p = Popen(['./cc_test','-p','88'], stdout=PIPE)
    process_list.append(p)

for i in process_list:
    print("id is "+ str(i.pid))

# input('wait for the signal point')

# for line in i.stdout.readlines():
#     print(line)

y = input("any key to send(for accuacy ,please wait a moment)....")

t = time.time()
begin = int(round(t * 1000000))
for i in process_list:
    os.system('kill -64 ' + str(i.pid))

for i in process_list:
    i.wait()
    for line in i.stdout.readlines():
        print(line)
    #print("............................\n")
t = time.time()
end = int(round(t * 1000000))

print( (4096 * 4 * 100000 * 8 * int(flow_num)) / ((end - begin)*1000))
