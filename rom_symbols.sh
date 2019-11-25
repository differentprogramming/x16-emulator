#!/bin/sh
for i in ndx keyd fa vartab fnlen fnadr status sa; do
	echo "#define" `echo $i | tr '[:lower:]' '[:upper:]'` 0x`grep -w $i ./rom.txt | head -n 1 | cut -d " " -f 2`;
done > rom_symbols.h
