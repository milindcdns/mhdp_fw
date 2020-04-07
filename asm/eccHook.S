	.begin	literal_prefix	.text
	.section		.text, "ax"

	.global _xtos_merr_hook_nonfatal
	.align 4

_xtos_merr_hook_nonfatal:
	extui	a3, a2, 24, 8
	extui	a2, a3, 0, 4
	movi.n	a3, 0
	beq	a3, a2, .L1
	beqi	a2, 4, .L1
	beqi	a2, 5, .L2
	beqi	a2, 1, .L2
	ret

.L1:
        movi	a3, 0xb0
	movi.n	a2, 1
	memw
	s32i.n	a2, a3, 0
	ret

.L2:                          
	movi	a3, 0xb0
	movi.n	a2, 2
	memw
	s32i.n	a2, a3, 0
	ret


	.size	_xtos_merr_hook_nonfatal, . - _xtos_merr_hook_nonfatal
	.text
	.end	literal_prefix

