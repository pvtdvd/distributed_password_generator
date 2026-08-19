#!/bin/bash
#Script per compilare tutti i sorgenti
g++ -std=c++17 master.cpp -o master -lzmq -pthread #Compila il master
g++ -std=c++17 worker.cpp -o worker -lzmq -pthread #Compila il worker
g++ -std=c++17 local.cpp -o local -lzmq -pthread #Compila il local

