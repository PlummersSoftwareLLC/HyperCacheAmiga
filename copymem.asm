	nolist
	XDEF _CopyMemQuicker

_CopyMemQuicker
	moveq	#12,d1
	cmp.l	d1,d0
	bcs.s	tinycpy		; too small to gain anything
	move.w	a0,d1
	lsr.b	#1,d1		; fastest test for evenness
	bcc.s	evena0
	move.b	(a0)+,(a1)+
	subq.l	#1,d0
evena0	move.w	a1,d1
	lsr.b	#1,d1
	bcc.s	CopyMemEvenQuicker

	moveq	#36*3,d1
	cmp.l	d1,d0
	bcs.s	tinycpy

* This is tricky!  They said it couldn't be done...
unevcpy	movem.l	a2-a4/d2-d7,-(sp)
	moveq	#32,d1		; 8 registers of 4 bytes
	move.w	d1,a3
	moveq	#36,d1		; as above plus 4 "roundoff" bytes
	move.w	d1,a4
	sub.l	d1,d0
	move.l	d0,a2
uloop	movem.l	(a0)+,d0-d7
	rol.l	#8,d0
	rol.l	#8,d1
	rol.l	#8,d2
	rol.l	#8,d3
	rol.l	#8,d4
	rol.l	#8,d5
	rol.l	#8,d6
	rol.l	#8,d7
	move.b	d0,(a1)+
	move.b	d1,d0
	move.b	d2,d1
	move.b	d3,d2
	move.b	d4,d3
	move.b	d5,d4
	move.b	d6,d5
	move.b	d7,d6
	move.b	(a0)+,d7
	movem.l	d0-d7,(a1)
	adda.w	a3,a1
	move.b	(a0)+,(a1)+	; even up to next longword
	move.b	(a0)+,(a1)+
	move.b	(a0)+,(a1)+
	move.l	a2,d0
	sub.l	a4,d0
	movea.l	d0,a2
	bcc.s	uloop
	add.w	a4,d0
	movem.l	(sp)+,a2-a4/d2-d7
	subq.b	#1,d0
	bcs.s	tdone

tloop	move.b	(a0)+,(a1)+
tinycpy	dbf	d0,tloop
tdone	rts

CopyMemEvenQuicker
	dc.w	$0c80		; cmpi.l #nnnn,d0
	dc.w	0		; Need 8 loops to be economical on 68010
CmpValS	dc.w	44*8		; (on 68000 this will be set to 44*2
				; and on 68020+ this will be 44*4
	bcs.s	smlmov
	moveq	#44,d1		; 11 registers of 4 bytes
	sub.l	d1,d0
	movem.l	d2-d7/a2-a6,-(sp)
bigmov	movem.l	(a0)+,d2-d7/a2-a6
	movem.l	d2-d7/a2-a6,(a1)
	adda.w	d1,a1
	sub.l	d1,d0
	bcc.s	bigmov
	add.w	d1,d0
	movem.l	(sp)+,d2-d7/a2-a6

smlmov	lsr.w	#1,d0
	beq.s	even01
	bcs.s	sm13
	lsr.w	#1,d0
	beq.s	even2
	bcs.s	sm2
sm0	subq.w	#1,d0
loop0	move.l	(a0)+,(a1)+
	dbf	d0,loop0
even0	rts
sm2	subq.w	#1,d0
loop2	move.l	(a0)+,(a1)+
	dbf	d0,loop2
even2	move.w	(a0),(a1)
	rts
sm13	lsr.w	#1,d0
	beq.s	even3
	bcs.s	sm3
sm1	subq.w	#1,d0
loop1	move.l	(a0)+,(a1)+
	dbf	d0,loop1
even1	move.b	(a0),(a1)
	rts
sm3	subq.w	#1,d0
loop3	move.l	(a0)+,(a1)+
	dbf	d0,loop3
even3	move.w	(a0)+,(a1)+
	move.b	(a0),(a1)
	rts
even01	bcs.s	even1
	rts

CopyMemQuickest
	dc.w	$0c80		; cmpi.l #nnnn,d0
	dc.w	0		; Need 8 loops to be economical on 68010
CmpValQ	dc.w	44*8		; (on 68000 this will be set to 44*2
				; and on 68020+ this will be 44*4
	bcs.s	smlmovQ
	moveq	#44,d1		; 11 registers of 4 bytes
	sub.l	d1,d0
	movem.l	d2-d7/a2-a6,-(sp)
bigmovQ	movem.l	(a0)+,d2-d7/a2-a6
	movem.l	d2-d7/a2-a6,(a1)
	adda.w	d1,a1
	sub.l	d1,d0
	bcc.s	bigmovQ
	add.w	d1,d0
	movem.l	(sp)+,d2-d7/a2-a6
smlmovQ	lsr.w	#2,d0
	beq.s	done
	subq.w	#1,d0
qloop	move.l	(a0)+,(a1)+
	dbf	d0,qloop
done	rts
CopyEnd
	even
	end
