#!/bin/bash
dependencies=$(cat ./dependencies.txt | tr '\r' - | tr '\n' ' ')

sudo apt update
echo "Installing dependencies"
echo "sudo apt install -y $dependencies"
sudo apt install -y $dependencies
