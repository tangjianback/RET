# coding = utf-8
import random


f  = open("flow.txt","w")
str_in = " 1 3 10000 2.000"
str_out = " 39.5 "
line_num = input("多少流")


#rate = (10/int(line_num)) * 0.95
rate = 8.0

rate = '%.2f' % rate
f.write(line_num+"\n")
f.write("2"+str_in+str(random.randint(0,0))+str_out+str(rate)+ "\n")
for i in range(3,int(line_num)+2):
	f.write(str(random.randint(3,64))+str_in+str(random.randint(0,0))+str_out+str(rate)+ "\n")
f.write("First line: flow \n# src dst priority packet# start_time end_time initial_speed")
f.close()