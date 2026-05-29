/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * HAKC section name tables — used by the module loader to identify
 * compartmented sections (e.g. .text.hakc.RED_CLIQUE) and color them
 * at insmod time with the matching MTE tag.
 *
 * This header DEFINES arrays (with EXPORT_SYMBOL). It must be included
 * from exactly ONE translation unit (currently kernel/module/main.c).
 *
 * Ported from MTE-kernel 5.10 (HAKC NDSS '22 reference) verbatim.
 */

#ifndef MTE_KERNEL_HAKC_DEFS_H
#define MTE_KERNEL_HAKC_DEFS_H

#include <linux/export.h>
#include <linux/hakc.h>

#define HAKC_PREPEND ".hakc."

const char *const hakc_section_names[] = {
	HAKC_PREPEND "SILVER_CLIQUE",  HAKC_PREPEND "GREEN_CLIQUE",
	HAKC_PREPEND "RED_CLIQUE",     HAKC_PREPEND "ORANGE_CLIQUE",
	HAKC_PREPEND "YELLOW_CLIQUE",  HAKC_PREPEND "PURPLE_CLIQUE",
	HAKC_PREPEND "BLUE_CLIQUE",    HAKC_PREPEND "GREY_CLIQUE",
	HAKC_PREPEND "PINK_CLIQUE",    HAKC_PREPEND "BROWN_CLIQUE",
	HAKC_PREPEND "WHITE_CLIQUE",   HAKC_PREPEND "BLACK_CLIQUE",
	HAKC_PREPEND "TEAL_CLIQUE",    HAKC_PREPEND "VIOLET_CLIQUE",
	HAKC_PREPEND "CRIMSON_CLIQUE", HAKC_PREPEND "GOLD_CLIQUE"
};
EXPORT_SYMBOL(hakc_section_names);

const char *const hakc_pcpu_section_names[] = {
	".data..percpu..data" HAKC_PREPEND "SILVER_CLIQUE",
	".data..percpu..data" HAKC_PREPEND "GREEN_CLIQUE",
	".data..percpu..data" HAKC_PREPEND "RED_CLIQUE",
	".data..percpu..data" HAKC_PREPEND "ORANGE_CLIQUE",
	".data..percpu..data" HAKC_PREPEND "YELLOW_CLIQUE",
	".data..percpu..data" HAKC_PREPEND "PURPLE_CLIQUE",
	".data..percpu..data" HAKC_PREPEND "BLUE_CLIQUE",
	".data..percpu..data" HAKC_PREPEND "GREY_CLIQUE",
	".data..percpu..data" HAKC_PREPEND "PINK_CLIQUE",
	".data..percpu..data" HAKC_PREPEND "BROWN_CLIQUE",
	".data..percpu..data" HAKC_PREPEND "WHITE_CLIQUE",
	".data..percpu..data" HAKC_PREPEND "BLACK_CLIQUE",
	".data..percpu..data" HAKC_PREPEND "TEAL_CLIQUE",
	".data..percpu..data" HAKC_PREPEND "VIOLET_CLIQUE",
	".data..percpu..data" HAKC_PREPEND "CRIMSON_CLIQUE",
	".data..percpu..data" HAKC_PREPEND "GOLD_CLIQUE"
};
EXPORT_SYMBOL(hakc_pcpu_section_names);

const char *const hakc_ro_after_init_section_names[] = {
	".data..ro_after_init..data" HAKC_PREPEND "SILVER_CLIQUE",
	".data..ro_after_init..data" HAKC_PREPEND "GREEN_CLIQUE",
	".data..ro_after_init..data" HAKC_PREPEND "RED_CLIQUE",
	".data..ro_after_init..data" HAKC_PREPEND "ORANGE_CLIQUE",
	".data..ro_after_init..data" HAKC_PREPEND "YELLOW_CLIQUE",
	".data..ro_after_init..data" HAKC_PREPEND "PURPLE_CLIQUE",
	".data..ro_after_init..data" HAKC_PREPEND "BLUE_CLIQUE",
	".data..ro_after_init..data" HAKC_PREPEND "GREY_CLIQUE",
	".data..ro_after_init..data" HAKC_PREPEND "PINK_CLIQUE",
	".data..ro_after_init..data" HAKC_PREPEND "BROWN_CLIQUE",
	".data..ro_after_init..data" HAKC_PREPEND "WHITE_CLIQUE",
	".data..ro_after_init..data" HAKC_PREPEND "BLACK_CLIQUE",
	".data..ro_after_init..data" HAKC_PREPEND "TEAL_CLIQUE",
	".data..ro_after_init..data" HAKC_PREPEND "VIOLET_CLIQUE",
	".data..ro_after_init..data" HAKC_PREPEND "CRIMSON_CLIQUE",
	".data..ro_after_init..data" HAKC_PREPEND "GOLD_CLIQUE"
};
EXPORT_SYMBOL(hakc_ro_after_init_section_names);

#define for_each_hakc_color(idx, color)                                        \
	for (idx = 0, color = START_CLIQUE; idx < HAKC_COLOR_COUNT;            \
	     idx++, color++)

#endif /* MTE_KERNEL_HAKC_DEFS_H */
