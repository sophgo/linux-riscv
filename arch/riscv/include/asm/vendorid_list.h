/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2021 SiFive
 */
#ifndef ASM_VENDOR_LIST_H
#define ASM_VENDOR_LIST_H

#define ANDESTECH_VENDOR_ID	0x31e
#define SIFIVE_VENDOR_ID	0x489
#define THEAD_VENDOR_ID		0x5b7

#ifdef CONFIG_ERRATA_ANDES
#define ERRATA_ANDES_NO_IOCP 0
#define ERRATA_ANDES_NUMBER 1
#endif

#ifdef CONFIG_ERRATA_SIFIVE
#define	ERRATA_SIFIVE_CIP_453 0
#define	ERRATA_SIFIVE_CIP_1200 1
#define	ERRATA_SIFIVE_NUMBER 2
#endif

#ifdef CONFIG_ERRATA_THEAD
#define        ERRATA_THEAD_PBMT 0
#define        ERRATA_THEAD_CMO 1
#define        ERRATA_THEAD_PMU 2
#define        ERRATA_THEAD_VECTOR 3
#define        ERRATA_THEAD_WRITE_ONCE 4
#define        ERRATA_THEAD_NUMBER 5
#endif

#endif
