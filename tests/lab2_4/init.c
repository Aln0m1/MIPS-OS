#include <bitops.h>
#include <mmu.h>
#include <pmap.h>
#include <printk.h>

void assert_valid_va_for_map(u_long va) {
	if (va % PAGE_SIZE != 0) {
		panic("va: 0x%08lx not align to PAGE_SIZE: %u", va, PAGE_SIZE);
	}

	if ((va >= KSEG0 && va < KSEG2)) {
		panic("va: 0x%08lx in direct mapped area: [0x%08lx, 0x%08lx]", va, KSEG0, KSEG2);
	}
}

void assert_valid_asid(u_int asid) {
	if (asid >= NASID) {
		panic("asid: %u exceeded limit: %u", asid, NASID);
	}
}

void assert_valid_hardware_flags(u_long flags) {
	assert((flags & ~(u_long)0xFC0) == 0); /* flags must only use hardware PTE bits [11:6] */
}

void check_tlb_entry(u_long entry, u_long expected_pa, u_long expected_flags) {
	assert(expected_pa % PAGE_SIZE == 0);
	assert_valid_hardware_flags(expected_flags);

	u_long actual_pa = (entry >> 6) << PGSHIFT;
	u_long expected_hardware_flags = expected_flags >> 6;
	u_long actual_hardware_flags = (entry & GENMASK(5, 0));

	if (expected_pa != actual_pa) {
		panic("TLB entry check failed, expected pa = 0x%08lx, actual pa = 0x%08lx",
		      expected_pa, actual_pa);
	}

	if (expected_hardware_flags != actual_hardware_flags) {
		panic("TLB entry check failed, expected hardware flags = 0x%08lx, actual hardware "
		      "flags = 0x%08lx",
		      expected_hardware_flags, actual_hardware_flags);
	}
}

u_long build_entryhi(u_long va, u_int asid) {
	return (va & ~GENMASK(PGSHIFT, 0)) | (asid & (NASID - 1));
}

struct TlbEntryResult {
	int valid;	 /* 1 if a matching TLB entry was found, 0 otherwise */
	u_long entrylo0; /* CP0_ENTRYLO0 of the matched entry (even page) */
	u_long entrylo1; /* CP0_ENTRYLO1 of the matched entry (odd page)  */
};

/* Look up the TLB entry for the VPN2 containing even page 'va' with 'asid'.
 * Returns a TlbEntryResult with valid=1 and the EntryLo0/Lo1 values on hit,
 * or valid=0 on miss.  CP0_ENTRYHI is saved and restored.
 */
struct TlbEntryResult lookup_tlb_entry(u_long va, u_int asid) {
	assert_valid_va_for_map(va);
	assert_valid_asid(asid);

	struct TlbEntryResult result = {0, 0, 0};

	u_long entryhi = build_entryhi(va, asid);

	long idx;
	u_long saved_entryhi;
	asm volatile("mfc0  %0, $10\n\t" /* save CP0_ENTRYHI */
		     "mtc0  %2, $10\n\t" /* CP0_ENTRYHI = entryhi */
		     "nop\n\t"
		     "tlbp\n\t" /* probe TLB */
		     "nop\n\t"
		     "mfc0  %1, $0\n\t" /* idx = CP0_INDEX */
		     : "=&r"(saved_entryhi), "=r"(idx)
		     : "r"(entryhi));

	if (idx >= 0) {
		u_long lo0, lo1;
		asm volatile("tlbr\n\t"		/* read TLB[CP0_INDEX] into CP0_ENTRYLO* */
			     "mfc0  %0, $2\n\t" /* lo0 = CP0_ENTRYLO0 */
			     "mfc0  %1, $3\n\t" /* lo1 = CP0_ENTRYLO1 */
			     : "=r"(lo0), "=r"(lo1));
		result.valid = 1;
		result.entrylo0 = lo0;
		result.entrylo1 = lo1;
	}

	asm volatile("mtc0 %0, $10\n\t" : : "r"(saved_entryhi)); /* restore CP0_ENTRYHI */

	return result;
}

/* Write a TLB entry mapping a pair of consecutive pages starting at even page 'va' to 'pa'.
 * Both the even page (va → pa) and the odd page (va+PAGE_SIZE → pa+PAGE_SIZE) are mapped
 * with the same hardware permission bits 'flags'.
 * 'flags' uses PTE-style bit positions: PTE_G (bit 6), PTE_V (bit 7),
 * PTE_D (bit 8), PTE_C_* (bits 9-10).
 *
 * Validity checks:
 *   - va must be page-aligned, and an even page (bit 12 == 0)
 *   - asid must be in [0, NASID)
 *   - pa must be page-aligned, refer to valid physical pages for both pa and pa+PAGE_SIZE,
 *     and be an even page (bit 12 == 0)
 *   - flags must only contain hardware PTE flag bits [11:6]
 */
void write_tlb_entry(u_long va, u_int asid, u_long pa, u_long flags) {
	/* Validity checks */
	assert_valid_va_for_map(va);
	assert((va & PAGE_SIZE) == 0); /* va must be an even page (bit 12 == 0) */
	assert_valid_asid(asid);
	assert((pa % PAGE_SIZE) == 0); /* pa must be page-aligned */
	assert((pa & PAGE_SIZE) == 0); /* pa must be an even page (bit 12 == 0) */
	assert_valid_hardware_flags(flags);

	/* Construct EntryHi: VPN2 in bits[31:13], ASID in bits[7:0] */
	u_long entryhi = build_entryhi(va, asid);

	/* Invalidate any existing TLB entry for this VPN2+ASID.
	 * Mirrors tlb_out logic: probe with tlbp, then overwrite the matched slot with
	 * zeros via tlbwi.  We avoid calling tlb_out() directly so that this function
	 * remains independent of the student's tlb_out implementation.
	 */
	{
		long idx;
		/* Set CP0_ENTRYHI and probe the TLB */
		asm volatile("mtc0 %1, $10\n\t" /* CP0_ENTRYHI = entryhi */
			     "nop\n\t"
			     "tlbp\n\t" /* probe: result written to CP0_INDEX */
			     "nop\n\t"
			     "mfc0 %0, $0\n\t" /* idx = CP0_INDEX */
			     : "=r"(idx)
			     : "r"(entryhi));
		if (idx >= 0) {
			/* A matching entry was found; zero it out */
			asm volatile("mtc0 $zero, $10\n\t" /* CP0_ENTRYHI  = 0 */
				     "mtc0 $zero, $2\n\t"  /* CP0_ENTRYLO0 = 0 */
				     "mtc0 $zero, $3\n\t"  /* CP0_ENTRYLO1 = 0 */
				     "nop\n\t"
				     "tlbwi\n\t"); /* write zeros to TLB[CP0_INDEX] */
		}
	}

	/* Construct EntryLo0 (even page) and EntryLo1 (odd page).
	 * PTE format: pa[31:12] holds PFN, flags[11:6] hold C/D/V/G.
	 * EntryLo = PTE >> 6: shifts PFN to bits[25:6] and flags to bits[5:0].
	 */
	u_long entrylo0 = (pa | flags) >> PTE_HARDFLAG_SHIFT;
	u_long entrylo1 = ((pa + PAGE_SIZE) | flags) >> PTE_HARDFLAG_SHIFT;

	/* Save CP0_ENTRYHI, load new entry, write via tlbwr, then restore */
	u_long saved_entryhi;
	asm volatile("mfc0  %0, $10\n\t" /* save CP0_ENTRYHI */
		     "mtc0  %1, $10\n\t" /* CP0_ENTRYHI  = entryhi  */
		     "mtc0  %2, $2\n\t"	 /* CP0_ENTRYLO0 = entrylo0 */
		     "mtc0  %3, $3\n\t"	 /* CP0_ENTRYLO1 = entrylo1 */
		     "nop\n\t"
		     "tlbwr\n\t"	 /* write to random TLB slot */
		     "mtc0  %0, $10\n\t" /* restore CP0_ENTRYHI */
		     : "=&r"(saved_entryhi)
		     : "r"(entryhi), "r"(entrylo0), "r"(entrylo1));
}

/* Check that tlb_out correctly removes a TLB entry that was previously inserted.
 * Steps:
 *   1. Insert a mapping (va, asid) -> (pa, pa+PAGE_SIZE) via write_tlb_entry.
 *   2. Verify the entry is present in the TLB and that EntryLo0/Lo1 match the
 *      expected physical addresses and flags.
 *   3. Call tlb_out with the corresponding EntryHi.
 *   4. Verify the entry is no longer present in the TLB (tlbp returns a miss).
 */
int check_remove_inserted_entry(u_long va, u_int asid, u_long pa, u_long flags) {
	int passed = 1;

	printk(
	    "check_remove_inserted_entry: va = 0x%08lx, asid = %u, pa = 0x%08lx, flags = 0x%08lx\n",
	    va, asid, pa, flags);
	write_tlb_entry(va, asid, pa, flags);

	struct TlbEntryResult lookup_result = lookup_tlb_entry(va, asid);

	if (lookup_result.valid == 0) {
		panic("check_remove_inserted_entry: cannot get inserted entry from TLB!");
	}

	check_tlb_entry(lookup_result.entrylo0, pa, flags);
	check_tlb_entry(lookup_result.entrylo1, pa + PAGE_SIZE, flags);

	printk("check_remove_inserted_entry: [+] insert succeeded\n");

	tlb_out(build_entryhi(va, asid));

	lookup_result = lookup_tlb_entry(va, asid);

	if (lookup_result.valid == 1) {
		printk("check_remove_inserted_entry: [-] remove failed\n");
		passed = 0;
	} else {
		printk("check_remove_inserted_entry: [+] remove succeeded\n");
	}

	return passed;
}

/* Check that tlb_out does not insert or corrupt any TLB entry when called for a
 * virtual address that has no existing TLB mapping (the miss / NO_SUCH_ENTRY path).
 * Steps:
 *   1. Confirm that (va, asid) has no TLB entry before the call.
 *   2. Call tlb_out with the corresponding EntryHi.
 *   3. Verify that (va, asid) still has no TLB entry after the call, i.e. tlb_out
 *      did not accidentally write a new entry via tlbwr or tlbwi.
 */
int check_no_side_effect_for_non_exist_page(u_long va, u_int asid) {
	int passed = 1;

	printk("check_no_side_effect_for_non_exist_page: va = 0x%08lx, asid = %u\n", va, asid);
	assert_valid_va_for_map(va);
	assert((va & PAGE_SIZE) == 0); /* va must be an even page (bit 12 == 0) */
	assert_valid_asid(asid);

	struct TlbEntryResult lookup_result = lookup_tlb_entry(va, asid);

	if (lookup_result.valid == 1) {
		panic("va: 0x%08lx, asid: %u has already mapped!", va, asid);
	}

	tlb_out(build_entryhi(va, asid));

	printk("check_no_side_effect_for_non_exist_page: [+] tlb_out returned successfully\n");

	lookup_result = lookup_tlb_entry(va, asid);

	if (lookup_result.valid == 1) {
		printk("check_no_side_effect_for_non_exist_page: [-] side effect detected, va: "
		       "0x%08lx, asid: %u has mapped, entrylo0 = 0x%08lx, entrylo1 = 0x%08lx\n",
		       va, asid, lookup_result.entrylo0, lookup_result.entrylo1);
		passed = 0;
	} else {
		printk("check_no_side_effect_for_non_exist_page: [+] tlb_out no side effect "
		       "checked\n");
	}

	return passed;
}

/* Check that tlb_out restores CP0_ENTRYHI to its original value after the call.
 * Tested in both the hit case (entry for va/asid exists) and
 * the miss case (entry for va/asid does not exist).
 */
int check_entryhi_restored_after_tlb_out(u_long va, u_int asid, u_long pa, u_long flags) {
	int passed = 1;
	printk("check_entryhi_restored_after_tlb_out: va = 0x%08lx, asid = %u, pa = 0x%08lx, flags "
	       "= 0x%08lx\n",
	       va, asid, pa, flags);
	assert_valid_va_for_map(va);
	assert((va & PAGE_SIZE) == 0);
	assert_valid_asid(asid);

	/* ---- hit case ---- */
	write_tlb_entry(va, asid, pa, flags);

	/* Use a sentinel that differs from entryhi so a failure is obvious */
	u_long sentinel = build_entryhi(va ^ 0x00200000, (asid + 1) & (NASID - 1));
	u_long restored;
	asm volatile("mtc0 %0, $10\n\t" : : "r"(sentinel)); /* plant sentinel in CP0_ENTRYHI */
	tlb_out(build_entryhi(va, asid));
	asm volatile("mfc0 %0, $10\n\t" : "=r"(restored)); /* read back CP0_ENTRYHI */

	if (restored == sentinel) {
		printk("check_entryhi_restored_after_tlb_out: [+] CP0_ENTRYHI restored correctly "
		       "on hit\n");
	} else {
		printk("check_entryhi_restored_after_tlb_out: [-] CP0_ENTRYHI corrupted on hit, "
		       "expected 0x%08lx, got 0x%08lx\n",
		       sentinel, restored);
		passed = 0;
	}

	/* ---- miss case ---- */
	/* entry was just removed by the hit-case tlb_out, so va/asid is no longer in TLB */
	sentinel = build_entryhi(va ^ 0x00400000, (asid + 2) & (NASID - 1));
	asm volatile("mtc0 %0, $10\n\t" : : "r"(sentinel));
	tlb_out(build_entryhi(va, asid));
	asm volatile("mfc0 %0, $10\n\t" : "=r"(restored));

	if (restored == sentinel) {
		printk("check_entryhi_restored_after_tlb_out: [+] CP0_ENTRYHI restored correctly "
		       "on miss\n");
	} else {
		printk("check_entryhi_restored_after_tlb_out: [-] CP0_ENTRYHI corrupted on miss, "
		       "expected 0x%08lx, got 0x%08lx\n",
		       sentinel, restored);
		passed = 0;
	}

	return passed;
}

void tlb_out_check(void) {
	printk("tlb_out_check() begin!\n");

	int passed = 1;

	passed &= check_remove_inserted_entry(0x40000000, 0, 0x00002000, PTE_V);
	passed &= check_no_side_effect_for_non_exist_page(0x40000000, 111);
	passed &= check_entryhi_restored_after_tlb_out(0x40002000, 1, 0x00004000, PTE_V);
	printk("tlb_out_check() end!\n");

	printk("TEST RESULT: ");

	if (passed == 1) {
		printk("[+] Accepted. Congratulations!\n");
	} else {
		printk("[-] Wrong Answer.\n");
	}
}
void mips_init(u_int argc, char **argv, char **penv, u_int ram_low_size) {
	printk("init.c:\tmips_init() is called\n");

	mips_detect_memory(ram_low_size);
	mips_vm_init();
	page_init();

	tlb_out_check();
	halt();
}
