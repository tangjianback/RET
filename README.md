# RET
We provide both the simulation and test-bed projects. More detail about these two projects can be found in our paper. Here we make a few notes on building and running these two projects.

Simulation: We implement the code based on the previous work. The original code can be found at https://github.com/bobzhuyb/ns3-rdma. We appreciate the author (Yibo Zhu) for the open-source code. So, if readers would like to build our simulation project, the tutorials are the same as the original project and can be found on the website above. Noted that we mainly modified the broadcom-egress-queue.cc and qbb-net-device.cc files to customize the switch discussed in our paper. And, RET/SWIFT/DX designs can be found in timley.cc file. 

Test-Bed: We provide the code of RSwift in test-bed-rswift folder. Readers can get familiar with how to start a flow with RSwift congestion control module by reading the scripts supplied in multy_flow_client.py/multy_flow_server.py files. The main design of RWift can be found in rdmacc.cpp and readers can modify the the file to customize their congestion design.
