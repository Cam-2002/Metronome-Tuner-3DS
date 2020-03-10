@echo off
echo ==================================================================
make
if ["%errorlevel%"]==["0"] 3dslink -r 5 -a 192.168.1.92 metronome.3dsx
