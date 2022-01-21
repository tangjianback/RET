# -*- coding: utf-8 -*-
import os
from subprocess import Popen, PIPE
input("make sure you are at python3\n")
flow_num = input('process num\n')
iters = input('iters for every process\n')
base_port = 8880
process_list = []

for i in range(int(flow_num)): 
    p = Popen(['./rc_pingpong_server', '-p',str(base_port+ i), '-i',str(iters)], stdout=PIPE)
    process_list.append(p)

for i in process_list:
    i.wait()
    for line in i.stdout.readlines():
        print(line)
    print("............................\n")