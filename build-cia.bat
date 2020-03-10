@echo off
echo === Building Minesweeper ===
make
bannertool makebanner -i banner.png -a empty.wav -o banner.bin
makerom -f cia -o metronome.cia -rsf cia.rsf -target t -exefslogo -elf metronome.elf -icon metronome.smdh -banner banner.bin
echo === Done ===
pause