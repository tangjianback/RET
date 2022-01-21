# coding = utf-8
import random

# 2 1 3 1000 2.0000 39.5 1.00

f  = open("flow_temp.txt","w")

str_in = " 1 3 15 2.000"
str_out = " 39.5 "
line_num = input("多少流")


rate = (10/int(line_num)) * 0.95
rate = 8.0

rate = '%.2f' % rate
f.write(line_num+"\n")
f.write("2"+str_in+str(random.randint(0,0))+str_out+str(rate)+ "\n")
for i in range(3,int(line_num)+2):
	if random.randint(1,10) < 0.1:
		f.write(str(random.randint(3,64))+" 1 3 "+str(random.randint(5000,100000))+ " "+str(random.randint(2,6))+"."+str(random.randint(0,999999))+str(random.randint(0,0))+str_out+str(rate)+ "\n")
	else:
		f.write(str(random.randint(3,64))+" 1 3 "+str(random.randint(10,100))+ " "+str(random.randint(2,2))+".0"+str(random.randint(0,999999))+str(random.randint(0,0))+str_out+str(rate)+ "\n")
f.write("First line: flow \n# src dst priority packet# start_time end_time initial_speed")
f.close()