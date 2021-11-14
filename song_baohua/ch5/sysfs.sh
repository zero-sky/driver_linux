#!/bin/bash

# populate block devices

for i in /sys/block/*/dev /sys/block/*/*/dev
do
	if [ -f $i ]
	then
		MAJOR=