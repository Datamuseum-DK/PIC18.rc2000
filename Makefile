
# port sdcc-2.5.4.2005.11.15 is not new enough

SDCC	=	/usr/local/bin/sdcc

OPTS	=
OPTS	= --optimize-df --fomit-frame-pointer 
OPTS	+= --calltree

PIC	=	pic18f25j50
PIC_F	=	pic18f86j50

PROG	=	phk_rc2000

all:	${PROG}.hex

${PROG}.hex:	${PROG}.c usb.h usb.c serial.c usb_desc.c
	${SDCC} -Wl-m \
		-I${.CURDIR} \
		-I/usr/local/share/sdcc/non-free/include/pic16 \
		-L/usr/local/share/sdcc/non-free/lib/pic16 \
		${OPTS} \
		-mpic16 -p${PIC} ${PROG}.c -llibc18f.lib -llibsdcc.lib
	tail -8 ${PROG}.lst | cut -c40-1000 | head -5

h55:	${PROG}.hex
	scp ${PROG}.hex root@h55:/tmp

clean:
	rm -f _* *.hex *.map *.o *.calltree *.cod *.lst *.asm

flint:	
	flint9 \
		-I${.CURDIR} \
		-I/usr/local/share/sdcc/include/pic16 \
		-D${PIC_F} \
		-DFLEXELINT \
		${.CURDIR}/flint.lnt \
		${PROG}.c

.include <bsd.obj.mk>

