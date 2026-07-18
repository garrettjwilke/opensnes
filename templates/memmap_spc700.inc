; Memory map for SPC700 (APU) code assembly
; Used by wla-spc700 to assemble APU-side programs into flat binaries
; (two-stage build like the GSU: wla-spc700 -> wlalink -b -> .incbin).
; The image is uploaded to APU RAM by the lib's apuUpload(); addresses
; inside the program are absolute APU RAM addresses — the .ORG below
; positions code relative to the upload base ($0200, mirroring krom's
; SPCRAM convention). One 64KB bank covers the whole APU address space.

.MEMORYMAP
    SLOTSIZE $10000
    DEFAULTSLOT 0
    SLOT 0 START $0000 SIZE $10000
.ENDME

.ROMBANKSIZE $10000
.ROMBANKS 1
