#include <linux/hakc.h>
#include <asm/mte.h>
#include <asm/memory.h>
#include <asm-generic/sections.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/percpu.h>
#include <linux/pgtable.h>
#include <linux/bitops.h>
#include <linux/math.h>
#include <uapi/linux/netlink.h>

#define HAKC_DEBUG IS_ENABLED(CONFIG_PAC_MTE_COMPART_DEBUG_PRINT)
#define HAKC_ALLOW IS_ENABLED(CONFIG_PAC_MTE_COMPART_ALLOW_FAILED)
#define HAKC_SIGN_PTR IS_ENABLED(CONFIG_PAC_MTE_COMPART_SIGN_PTR)

#define HAKC_INVALID_PTR (void *)0xDEADBEEF

#define HAKC_INFO(fmt, ...)                                                    \
	if (HAKC_DEBUG) {                                                      \
		/*pr_info(fmt, ##__VA_ARGS__); */                                  \
	}
#define HAKC_ERR(fmt, ...)                                                     \
	if (HAKC_DEBUG) {                                                      \
		/*pr_err(fmt, ##__VA_ARGS__);*/                                    \
	}

#if IS_ENABLED(CONFIG_PAC_MTE_EVAL_ENABLE_PAC)
#define PAC_SUB_INSTS                                                          \
	"mov %[mod], xzr\n\t"                                                  \
	"movk %[mod], #0xFF, lsl 48\n\t"                                       \
	"add x16, x16, #1\n\t"                                                 \
	"orr %[addr], %[addr], %[mod]\n\t"
#else
#define PAC_SUB_INSTS "nop"
#endif

struct percpu_info {
	void *signed_addr;
	bool is_percpu, is_dynamic;
	void *percpu_addr;
};

volatile bool mte_global_debug = false;

EXPORT_SYMBOL(mte_global_debug);

#if IS_ENABLED(CONFIG_PAC_MTE_EVAL_CODEGEN) &&                                 \
	!IS_ENABLED(PAC_MTE_MTE_MEMORY_BARRIER)
int tag_clobber_memory[4];
#endif

/*
 * pcpu_ptr_to_addr() was a HAKC addition to 5.10 mm/percpu.c.
 * In 6.6 we replicate the same formula inline using pcpu_base_addr
 * (declared in <linux/percpu.h>) and __per_cpu_start (<asm-generic/sections.h>).
 */
static inline void *hakc_pcpu_to_virt(void *ptr)
{
#ifdef CONFIG_SMP
	return (void *)((unsigned long)ptr +
			(unsigned long)pcpu_base_addr -
			(unsigned long)__per_cpu_start);
#else
	return ptr;
#endif
}

static inline bool is_userspace_addr(const void *addr)
{
	/* Bits 48:63 are one for kernel addresses */
	return ((UL(1) << VA_BITS) > (unsigned long)addr);
}

static inline bool addr_is_signed(const void *ptr)
{
	unsigned long p = (unsigned long)ptr;
	unsigned int upper_bits = (p >> HAKC_ADDRESS_BITS);
	return (upper_bits > 0 && upper_bits != 0xFFFF);
}

static inline bool get_percpu_info(struct percpu_info *info)
{
	if (addr_is_signed(info->signed_addr)) {
		info->percpu_addr = HAKC_GET_SAFE_PTR(info->signed_addr);
	} else {
		info->percpu_addr = info->signed_addr;
	}

	if (is_kernel_percpu_address((unsigned long)info->percpu_addr) ||
	    is_module_percpu_address((unsigned long)info->percpu_addr)) {
		info->is_percpu = true;
		info->is_dynamic = false;
		return true;
	}
	/* Dynamic (alloc_percpu) memory: is_dynamic_percpu_address() is now
	 * ported to 6.6 mm/percpu.c, so detect it and mark is_dynamic so the
	 * caller colors every CPU's instance (for_each_possible_cpu). */
	if (is_dynamic_percpu_address((unsigned long)info->percpu_addr)) {
		info->is_percpu = true;
		info->is_dynamic = true;
		return true;
	}

	info->is_percpu = false;
	info->percpu_addr = NULL;
	info->is_dynamic = false;
	return false;
}

const char *get_hakc_color_name(clique_color_t color)
{
	switch (color) {
	case SILVER_CLIQUE:
		return "SILVER_CLIQUE";
	case GREEN_CLIQUE:
		return "GREEN_CLIQUE";
	case RED_CLIQUE:
		return "RED_CLIQUE";
	case ORANGE_CLIQUE:
		return "ORANGE_CLIQUE";
	case YELLOW_CLIQUE:
		return "YELLOW_CLIQUE";
	case PURPLE_CLIQUE:
		return "PURPLE_CLIQUE";
	case BLUE_CLIQUE:
		return "BLUE_CLIQUE";
	case GREY_CLIQUE:
		return "GREY_CLIQUE";
	case PINK_CLIQUE:
		return "PINK_CLIQUE";
	case BROWN_CLIQUE:
		return "BROWN_CLIQUE";
	case WHITE_CLIQUE:
		return "WHITE_CLIQUE";
	case BLACK_CLIQUE:
		return "BLACK_CLIQUE";
	case TEAL_CLIQUE:
		return "TEAL_CLIQUE";
	case VIOLET_CLIQUE:
		return "VIOLET_CLIQUE";
	case CRIMSON_CLIQUE:
		return "CRIMSON_CLIQUE";
	case GOLD_CLIQUE:
		return "GOLD_CLIQUE";
	default:
		return "INVALID_CLIQUE";
	}
}

EXPORT_SYMBOL(get_hakc_color_name);

void hakc_init_tags(void)
{
	pr_info("Initializing tags for HAKC\n");
	mte_init_tags(END_CLIQUE - 1);
	/* Enable MTE Sync Mode for EL1. */
	sysreg_clear_set(sctlr_el1, SCTLR_EL1_TCF_MASK, SCTLR_EL1_TCF_NONE);
	isb();
}

void hakc_color_address(const void *addr_to_color, clique_color_t color,
			size_t size)
{
	void *ptr;
	if (!VALID_COLOR(color)) {
		color = INVALID_CLIQUE;
	}
	ptr = (void *)addr_to_color;
	ptr = (void *)round_down((unsigned long)ptr, COLOR_GRANULARITY);

	if (size > COLOR_GRANULARITY) {
		size = round_up(size + (addr_to_color - ptr),
				COLOR_GRANULARITY);
	} else {
		size = COLOR_GRANULARITY;
	}
	HAKC_INFO("Coloring %u bytes at 0x%lx %s (%d)\n", size, ptr,
		  get_hakc_color_name(color), color);
	/* 6.6: mte_set_mem_tag_range has an extra bool init parameter */
	mte_set_mem_tag_range(ptr, size, (u8)color, false);
	HAKC_INFO("%lx is colored %s (%s)\n", addr_to_color,
		  get_hakc_color_name(get_hakc_address_color(addr_to_color)),
		  get_hakc_color_name(color));
}

EXPORT_SYMBOL(hakc_color_address);

static inline clique_color_t _get_mte_tag(const void *addr)
{
	return (clique_color_t)mte_get_mem_tag((void *)addr);
}

clique_color_t get_hakc_address_color(const void *addr)
{
	unsigned long _addr = (unsigned long)addr;
	if (ZERO_OR_NULL_PTR(addr) || _addr < 0x20) {
		return INVALID_CLIQUE;
	}

	if ((_addr >= (unsigned long)KERNEL_START &&
	     _addr <= (unsigned long)KERNEL_END) ||
	    addr_is_signed(addr)) {
		_addr = (unsigned long)HAKC_KADDR(addr);
	}

	/* Guard: if the resolved address is still in user-space VA range
	 * (not a valid kernel address), skip the MTE tag read to avoid
	 * a translation fault. This can happen when PMCPass-generated
	 * signing code operates on uninitialized or differently-laid-out
	 * pointers in 6.6 vs 5.10. */
	if (_addr < PAGE_OFFSET)
		return INVALID_CLIQUE;

	return _get_mte_tag((void *)_addr);
}

EXPORT_SYMBOL(get_hakc_address_color);

static void *sign_data(const void *address, pac_salt_t modifier)
{
	void *result;
	HAKC_INFO("Signing data pointer %lx with salt %lx\n", address,
		  modifier);

	asm(
#if 0//IS_ENABLED(CONFIG_PAC_MTE_EVAL_CODEGEN)
		PAC_SUB_INSTS
#else
		// using pacia instead of pacda because AD keys can change during
		// context switch
		"pacia %[addr], %[mod]"
#endif
		: "=r"(result)
		: [addr] "0"(address), [mod] "r"(modifier)
		:);
	return result;
}

static void *sign_code(const void *address, pac_salt_t modifier)
{
	void *result;
	HAKC_INFO("Signing code pointer %lx with salt %lx\n", address,
		  modifier);

	asm(
#if 0//IS_ENABLED(CONFIG_PAC_MTE_EVAL_CODEGEN)
		PAC_SUB_INSTS
#else
		"pacia %[addr], %[mod]"
#endif
		: "=r"(result)
		: [addr] "0"(address), [mod] "r"(modifier)
		:);
	return result;
}

static void *compute_pac(const void *addr, clique_color_t color,
			 claque_id_t claque_id,
			 void *(sign_func)(const void *, pac_salt_t))
{
	pac_salt_t modifier = PAC_MODIFIER(claque_id, HAKC_MASK_COLOR(color));
	u64 ctx_addr = HAKC_CONTEXT_ADDR(addr);
	u64 claque_bits = HAKC_CLAQUE_ADDR(addr);
	void *signed_ptr;
	void *final_ptr;

	signed_ptr = sign_func((const void *)ctx_addr, modifier);
	final_ptr = (void *)((u64)signed_ptr | claque_bits);

	return (void *)((u64)signed_ptr | HAKC_CLAQUE_ADDR(addr));
}

u64 compute_data_pac(const void *addr, clique_color_t color,
		     claque_id_t claque_id)
{
	u64 result;
	result = (u64)compute_pac(addr, color, claque_id, sign_data);
	return result;
}

static uintptr_t compute_code_pac(const void *addr, clique_color_t color,
				  claque_id_t claque_id)
{
	u64 result;
	result = (u64)compute_pac(addr, color, claque_id, sign_code);
	return result;
}

clique_color_t get_hakc_color_by_name(const char *color_name)
{
	clique_color_t color = START_CLIQUE;
	while (color != END_CLIQUE) {
		const char *curr_name = get_hakc_color_name(color);
		if (strcasecmp(color_name, curr_name) == 0) {
			break;
		}
		color++;
	}

	return color;
}

EXPORT_SYMBOL(get_hakc_color_by_name);

static inline bool verify_and_set_auth_ptr(uint64_t auth_ptr, void **ptr)
{
	bool result = !addr_is_signed((void *)auth_ptr);
	HAKC_INFO("%lx is%s authenticated\n", auth_ptr, result ? "" : " not");
	if (result && ptr) {
		*ptr = (void *)auth_ptr;
	} else if (!result && ptr) {
		if (HAKC_ALLOW) {
			*ptr = (void *)HAKC_GET_SAFE_PTR(auth_ptr);
		} else {
			*ptr = HAKC_INVALID_PTR;
			hakc_debug_breakpoint();
		}
	}
	return result;
}

claque_id_t get_hakc_address_claque(const void *addr)
{
	unsigned long iaddr = (unsigned long)addr;
	claque_id_t id;
	id = upper_bit_claque(iaddr);
	return id;
}

EXPORT_SYMBOL(get_hakc_address_claque);

static inline pac_salt_t create_pac_context(claque_id_t claque_id,
					    u64 masked_color)
{
	return PAC_MODIFIER(claque_id, masked_color);
}

static inline pac_salt_t obtain_modifier_cert(clique_color_t address_color,
					      claque_id_t claque_id)
{
	pac_salt_t result;

	result = create_pac_context(claque_id, HAKC_MASK_COLOR(address_color));
	return result;
}


DEFINE_PER_CPU(unsigned long, hakc_last_chk_caller);
static __always_inline u64 pacia_mod(u64 ptr, u64 mod)
{
    u64 x = ptr;
    asm volatile(
        "pacia %0, %1"
        : "+&r"(x)
        : "r"(mod)
        : "memory"
    );
    return x;
}


void *untag_ptr(const void *p)
{
    unsigned long v = (unsigned long)p;
    v &= ~(0xFFUL << 56);
    v = (unsigned long)(((long)v << 16) >> 16);
    return (void *)v;
}
EXPORT_SYMBOL_GPL(untag_ptr);

static __always_inline void *canon_kernel_va(const void *p) {
    unsigned long v = (unsigned long)untag_ptr(p);
    v |= 0xFFFF000000000000UL;
    return (void *)v;
}

static __always_inline unsigned long canonical_kva(unsigned long v)
{
    unsigned long mask = (1UL << VA_BITS) - 1;
    return sign_extend64(v & mask, VA_BITS - 1);
}

static __always_inline bool is_percpu_va_canon(unsigned long va_canon)
{
    if (is_kernel_percpu_address(va_canon))
        return true;
#if IS_ENABLED(CONFIG_MODULES)
    /* 6.6: is_module_percpu_address takes unsigned long, not void * */
    if (is_module_percpu_address(va_canon))
        return true;
#endif

#ifdef CONFIG_ARM64
    if ( (va_canon & 0xFFFF000000000000UL) == 0xFFFF000000000000UL &&
         ((va_canon >> 32) & 0xFFFFUL) == 0xFDFFUL )
        return true;
#endif
    return false;
}

static __always_inline bool is_percpu_va(const void *p)
{
    unsigned long canon = canonical_kva((unsigned long)untag_ptr(p));
    return is_percpu_va_canon(canon);
}

static __always_inline void *canonicalize_kva(const void *p) {
    u64 v = (u64)p;
    v &= ~((u64)0xFF << 56);
#if defined(VA_BITS) && VA_BITS == 52
    v = sign_extend64(v, 51);
#else
    v = sign_extend64(v, 55);
#endif
    return (void *)v;
}
#include <linux/kallsyms.h>
#include <linux/string.h>

char* white_list[] = {
	"ip6_finish_output2+0x50"
};

static __always_inline bool caller_in_whitelist(unsigned long ip)
{
    char sym[KSYM_SYMBOL_LEN];
    int i;

    sprint_symbol(sym, ip);
    for (i = 0; i < ARRAY_SIZE(white_list); i++) {
        if (strstr(sym, white_list[i]))
            return true;
    }
    return false;
}

static void * noinline check_hakc_access(
			       const void *address,
			       const clique_access_tok_t access_tok)
{
	pac_salt_t salt;
	unsigned long result;
	const void *ctx_addr;
	claque_id_t addr_claque;
	clique_color_t addr_color;
	void *safe_addr;

	if (is_userspace_addr(address)) {
		return (void *)address;
	} else if (IS_ERR(address)) {
		return (void *)address;
	}


	safe_addr = (void*)HAKC_GET_SAFE_PTR(address);

	HAKC_INFO("access_tok = 0x%lx\taddress = 0x%lx\n", access_tok, address);
	addr_claque = get_hakc_address_claque(address);

	addr_color = _get_mte_tag(safe_addr);
	HAKC_INFO("0x%lx is colored %s and in claque %lu\n", address,
		  get_hakc_color_name(addr_color), addr_claque);

	ctx_addr = (const void *)((u64)address | CLAQUE_BIT_MASK_2);

	pac_salt_t obtain_cert = obtain_modifier_cert(addr_color, addr_claque);
	salt = obtain_cert & access_tok;

	if (HAKC_ALLOW) {
		result = (unsigned long)HAKC_GET_SAFE_PTR(address);
	} else {
		if (VALID_CLAQUE(addr_claque) && salt && salt == obtain_cert &&
		    (((u64)ctx_addr >> 48) & 0xFF) != 0xFF) {
			result = HAKC_CONTEXT_ADDR(ctx_addr);
			asm volatile("autia %0, %1"
				     : "+r"(result)
				     : "r"(obtain_cert));
			result |= (0x0000FFFFFFFFFFFF & (unsigned long)ctx_addr);
		} else {
			if (VALID_CLAQUE(addr_claque) && !salt) {
				pr_warn_ratelimited(
					"HAKC ENFORCE DENY: address=%016llx color=%s "
					"claque=%u access_tok=%016llx\n",
					(u64)address, get_hakc_color_name(addr_color),
					addr_claque, (u64)access_tok);
			}
			result = (unsigned long)HAKC_GET_SAFE_PTR(address);
		}
		HAKC_INFO("ctx_addr = %lx salt = %lx result = %lx\n",
			  ctx_addr, salt, result);
	}
	HAKC_INFO("result = %lx address = %lx\n", result, address);
	__s64 aa = (__s64)result;
	aa |= 0xFFFF000000000000;

	return (void *)aa;
}


static size_t
hakc_get_valid_target_index(const void *target,
			    const claque_entry_tok_t *valid_targets,
			    size_t n_targets)
{
	size_t i;
	size_t result = -1;
	clique_color_t target_color;
	pac_salt_t salt;
	u64 masked_color;

	target_color = get_hakc_address_color(target);
	masked_color = HAKC_MASK_COLOR(target_color);

	for (i = 0; i < n_targets; i++) {
		const claque_entry_tok_t entry_token = valid_targets[i];
		u64 auth_target;

		salt = create_pac_context(entry_token.claque_id,
					  masked_color &
						  entry_token.entry_token);
		auth_target = (u64)target;
		if (salt)
			asm volatile("autia %0, %1"
				     : "+r"(auth_target)
				     : "r"(salt));
		if (verify_and_set_auth_ptr(auth_target, NULL)) {
			result = i;
			break;
		}
	}

	return result;
}

void *check_hakc_data_access(const void *address,
			     const clique_access_tok_t access_tok)
{
	this_cpu_write(hakc_last_chk_caller, (unsigned long)_RET_IP_);
	HAKC_INFO("check_hakc_data_access called from %lx\n", _RET_IP_);
	return check_hakc_access(address, access_tok);
}

EXPORT_SYMBOL(check_hakc_data_access);

void *check_hakc_code_access(const void *address,
			     const clique_access_tok_t access_tok,
			     const claque_entry_tok_t *valid_targets,
			     size_t n_targets)
{
	bool result;
	void *authenticated_ptr = NULL;
	HAKC_INFO("Checking code access to %lx for %ld targets\n", address,
		  n_targets);
	authenticated_ptr =
		check_hakc_access(address, access_tok);
	if (addr_is_signed(authenticated_ptr) && n_targets > 0) {
		result = (hakc_get_valid_target_index(address, valid_targets,
						      n_targets) >= 0);
		HAKC_INFO("Code access to %lx is%s allowed\n", address,
			  result ? "" : " not");
		if (!result) {
			authenticated_ptr = (void *)address;
		} else {
			authenticated_ptr = (void *)HAKC_GET_SAFE_PTR(address);
		}
	}

	return authenticated_ptr;
}
EXPORT_SYMBOL(check_hakc_code_access);

static bool is_readonly(unsigned long addr)
{
	return (addr >= (unsigned long)__start_rodata &&
		addr <= (unsigned long)__end_rodata) ||
	       !pte_write(*virt_to_kpte(addr));
}

noinline void hakc_debug_breakpoint()
{
	dump_stack();
}
EXPORT_SYMBOL(hakc_debug_breakpoint);

void *hakc_sign_pointer(void *addr, claque_id_t claque_id, clique_color_t color,
			bool is_code)
{
#if !HAKC_SIGN_PTR
	void *orig_addr = addr;
#endif

	if (VALID_CLAQUE(claque_id)) {
		addr = HAKC_GET_SAFE_PTR(addr);
		if (is_code) {
			addr = (void *)compute_code_pac((void *)addr, color,
							claque_id);
		} else {
			addr = (void *)compute_data_pac((void *)addr, color,
							claque_id);
		}
#if 0//IS_ENABLED(CONFIG_PAC_MTE_EVAL_CODEGEN)
		addr = HAKC_GET_SAFE_PTR(addr);
#else
		addr = (void *)EMBED_CLAQUE_ID(claque_id, addr);
#endif
		HAKC_INFO("TRANSFER RESULT to %d %lx %d %lx\n", claque_id, addr,
			  get_hakc_address_claque((void *)addr),
			  (unsigned long)claque_id << CLAQUE_START_2);
	}

#if HAKC_SIGN_PTR
	return (void *)addr;
#else
	return orig_addr;
#endif
}
EXPORT_SYMBOL(hakc_sign_pointer);

void *hakc_sign_pointer_with_color(void *addr, claque_id_t claque_id,
				   bool is_code)
{
	struct percpu_info pcpu_info;

	if (!addr) {
		return addr;
	}
	pcpu_info.signed_addr = addr;

	if (get_percpu_info(&pcpu_info)) {
		void *result, *pcpu_ptr, *signed_ptr;
		unsigned int cpu;
		if (!pcpu_info.is_dynamic) {
			return hakc_sign_pointer(
				pcpu_info.percpu_addr, claque_id,
				get_hakc_address_color(pcpu_info.percpu_addr),
				is_code);
		}

		for_each_possible_cpu (cpu) {
			pcpu_ptr = per_cpu_ptr(pcpu_info.percpu_addr, cpu);
			HAKC_INFO("\tpcpu_ptr = %lx\n", pcpu_ptr);
			signed_ptr = hakc_sign_pointer(
				pcpu_ptr, claque_id,
				get_hakc_address_color(pcpu_ptr), is_code);
			HAKC_INFO("\tsigned_ptr = %lx\n", signed_ptr);
			if (cpu == get_boot_cpu_id()) {
				u64 offset = ((u64)pcpu_ptr -
					      (u64)pcpu_info.percpu_addr);
				HAKC_INFO("\toffset = %lx\n", offset);
				result = (void *)((u64)signed_ptr - offset);
			}
		}
		return result;
	}

	return hakc_sign_pointer(addr, claque_id, get_hakc_address_color(addr),
				 is_code);
}
EXPORT_SYMBOL(hakc_sign_pointer_with_color);

static void *color_and_sign(void *data_to_transfer, size_t size,
			    claque_id_t claque_id, clique_color_t color,
			    bool is_code)
{
	if (!is_userspace_addr(data_to_transfer) && size > 0) {
		unsigned long addr = (unsigned long)data_to_transfer;
		HAKC_INFO("Transferring %lu bytes at %lx to claque %d (%s)\n",
			  size, data_to_transfer, claque_id,
			  get_hakc_color_name(color));
		HAKC_INFO("Returning to %lx\n", _RET_IP_);

		if (addr_is_signed(data_to_transfer)) {
			addr = HAKC_GET_SAFE_PTR(addr);
		}

		if (claque_id != get_hakc_address_claque(data_to_transfer) &&
		    !is_code && !is_readonly(addr)) {
			hakc_color_address((void *)addr, color, size);
		} else {
			color = get_hakc_address_color(data_to_transfer);
			HAKC_INFO("%lx is read-only and colored %s\n", addr,
				  get_hakc_color_name(color));
		}

		return hakc_sign_pointer((void *)addr, claque_id, color,
					 is_code);
	} else {
		return data_to_transfer;
	}
}

void *mte_transfer_percpu(struct percpu_info *pcpu_info, size_t size,
			  claque_id_t claque_id, clique_color_t color,
			  bool is_code)
{
	void *pcpu_ptr;

	HAKC_INFO("Transferring percpu variable %lx with size %lx to %d and "
		  "color %s\n",
		  pcpu_info->signed_addr, size, claque_id,
		  get_hakc_color_name(color));

	/*
	 * 6.6: get_percpu_info() sets percpu_addr=NULL for dynamic percpu
	 * (alloc_percpu) because is_dynamic_percpu_address() was removed.
	 * Skip coloring in that case: hakc_pcpu_to_virt(NULL) computes an
	 * invalid VA that causes virt_to_kpte() to crash in is_readonly().
	 * TODO: restore full dynamic-percpu coloring via for_each_possible_cpu.
	 */
	if (!pcpu_info->percpu_addr)
		return pcpu_info->signed_addr;

	/*
	 * Dynamic (alloc_percpu) memory: percpu_addr is the allocator cookie.
	 * Each CPU's instance lives at a distinct VA (per_cpu_ptr(cookie,cpu)),
	 * so color *every* CPU's copy — otherwise the clique tag would only
	 * hold on whichever CPU we happened to resolve, leaving the data
	 * unprotected on all other CPUs. (Restores the 5.10 reference's
	 * dynamic-percpu coverage; the reference's sign path already iterates
	 * for_each_possible_cpu in hakc_sign_pointer_with_color.)
	 */
	if (pcpu_info->is_dynamic) {
		unsigned int cpu;

		if (is_code)
			return pcpu_info->signed_addr;
		for_each_possible_cpu (cpu) {
			void *p = per_cpu_ptr(pcpu_info->percpu_addr, cpu);

			if (!is_readonly((unsigned long)p) &&
			    claque_id != get_hakc_address_claque(p))
				hakc_color_address(p, color, size);
		}
		HAKC_INFO("Transferred dynamic percpu %lx on all CPUs\n",
			  pcpu_info->percpu_addr);
		return pcpu_info->signed_addr;
	}

	/*
	 * Static DEFINE_PER_CPU. Convert the per-CPU offset pointer to a
	 * regular virtual address so we can apply MTE color tags.
	 * 6.6: use hakc_pcpu_to_virt() instead of the removed pcpu_ptr_to_addr().
	 *
	 * hakc_pcpu_to_virt() adds (pcpu_base_addr - __per_cpu_start) and is
	 * only correct for an *offset* pointer, i.e. &percpu_var which lives in
	 * the static percpu image range [__per_cpu_start, __per_cpu_end].
	 * Callers like icmpv6_init() pass an ALREADY-RESOLVED per-CPU VA from
	 * per_cpu_ptr(&var, cpu); re-applying the offset there double-counts
	 * the base and yields a garbage VA that faults in is_readonly()'s
	 * page-table walk. Only re-offset genuine offset pointers.
	 */
	{
		unsigned long _pa = (unsigned long)pcpu_info->percpu_addr;
		if (_pa >= (unsigned long)__per_cpu_start &&
		    _pa < (unsigned long)__per_cpu_end)
			pcpu_ptr = hakc_pcpu_to_virt(pcpu_info->percpu_addr);
		else
			pcpu_ptr = pcpu_info->percpu_addr;
	}

	if (!is_code && !is_readonly((unsigned long)pcpu_ptr) &&
	    claque_id != get_hakc_address_claque(pcpu_ptr)) {
		hakc_color_address(pcpu_ptr, color, size);
	}

	HAKC_INFO("Transferred percpu variable %lx (color only, no PAC sign)\n",
		  pcpu_info->percpu_addr);

	return pcpu_info->signed_addr;
}

void *hakc_transfer_to_clique(void *data_to_transfer, size_t size,
			      claque_id_t claque_id, clique_color_t color,
			      bool is_code)
{
	if (!data_to_transfer || claque_id == 255 || mte_get_mem_tag(data_to_transfer) != 0xf0) {
		return data_to_transfer;
	}

	struct percpu_info pcpu_info;
	pcpu_info.signed_addr = data_to_transfer;
	if (!data_to_transfer) {
		return data_to_transfer;
	} else if (get_percpu_info(&pcpu_info)) {
		HAKC_INFO("Returning to %lx\n", _RET_IP_);
		return mte_transfer_percpu(&pcpu_info, size, claque_id, color,
					   is_code);
	}

	return color_and_sign(data_to_transfer, size, claque_id, color,
			      is_code);
}
EXPORT_SYMBOL(hakc_transfer_to_clique);

void *hakc_transfer_data_to_target(const void *target, void *data_to_transfer,
				   size_t transfer_size, bool is_code)
{
	if (IS_ENABLED(CONFIG_PAC_MTE_COMPART) && target && transfer_size > 0) {
		clique_color_t target_color;
		claque_id_t target_claque;

		if (core_kernel_text(
			    HAKC_GET_SAFE_PTR((unsigned long)target))) {
			return HAKC_GET_SAFE_PTR(data_to_transfer);
		}

		target_color = get_hakc_address_color(target);
		target_claque = get_hakc_address_claque(target);
		HAKC_INFO("Transferring %lx to %lx (%s %d)\n", data_to_transfer,
			  target, get_hakc_color_name(target_color),
			  target_claque);
		HAKC_INFO("Returning to %lx\n", _RET_IP_);
		return hakc_transfer_to_clique(data_to_transfer, transfer_size,
					       target_claque, target_color,
					       is_code);
	} else {
		return data_to_transfer;
	}
}
EXPORT_SYMBOL(hakc_transfer_data_to_target);

void *hakc_transfer_percpu_to_clique(void *original, size_t size,
				     claque_id_t claque_id,
				     clique_color_t color)
{
	struct percpu_info pcpu_info;

	pcpu_info.signed_addr = original;
	get_percpu_info(&pcpu_info);

	return mte_transfer_percpu(&pcpu_info, size, claque_id, color, false);
}

EXPORT_SYMBOL(hakc_transfer_percpu_to_clique);

void *hakc_transfer_string(void *str, claque_id_t claque_id, clique_color_t color)
{
 return hakc_transfer_to_clique(str, strlen(str) + 1, claque_id, color, false);
}
EXPORT_SYMBOL(hakc_transfer_string);

struct sk_buff *hakc_transfer_skb(struct sk_buff *skb, claque_id_t claque_id, clique_color_t color)
{
  size_t data_offset;
  skb = HAKC_GET_SAFE_PTR(skb);
  data_offset = HAKC_GET_SAFE_PTR(skb->data) - HAKC_GET_SAFE_PTR(skb->head);
  skb->head = hakc_transfer_to_clique(skb->head, skb->truesize - SKB_DATA_ALIGN(sizeof(struct sk_buff)),
                            claque_id, color, false);
  skb->data = skb->head + data_offset;
  skb = hakc_transfer_to_clique(skb, sizeof(*skb), claque_id, color, false);
  return skb;
}
EXPORT_SYMBOL(hakc_transfer_skb);

const struct nlattr * const *hakc_transfer_nla(const struct nlattr * const nla[], size_t size, claque_id_t claque_id, clique_color_t color)
{
  struct nlattr **new_nla = HAKC_GET_SAFE_PTR((struct nlattr **)nla);
  int i;
  for(i = 0; i < size; i++) {
    if(new_nla[i]) {
      new_nla[i] = hakc_transfer_to_clique(new_nla[i], HAKC_GET_SAFE_PTR(new_nla[i])->nla_len, claque_id, color, false);
    }
  }
  return hakc_transfer_to_clique(new_nla, sizeof(struct nlattr *) * size, claque_id, color, false);
}
EXPORT_SYMBOL(hakc_transfer_nla);
