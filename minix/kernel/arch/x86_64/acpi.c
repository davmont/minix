
#include <string.h>

#include "acpi.h"
#include "arch_proto.h"

typedef int ((* acpi_read_t)(phys_bytes addr, void * buff, size_t size));

struct acpi_rsdp acpi_rsdp;

static acpi_read_t read_func;

#define MAX_RSDT	35 /* ACPI defines 35 signatures */
#define SLP_EN_CODE	(1 << 13) /* ACPI SLP_EN_CODE code */
#define AMI_PACKAGE_OP_CODE (0x12)
#define AMI_NAME_OP_CODE (0x8)
#define AMI_BYTE_PREFIX_CODE (0xA)
#define AMI_PACKAGE_LENGTH_ENCODING_BITS_MASK (0xC0)
#define AMI_PACKAGE_LENGTH_ENCODING_BITS_SHIFT (6)
#define AMI_MIN_PACKAGE_LENGTH (1)
#define AMI_NUM_ELEMENTS_LENGTH (1)
#define AMI_SLP_TYPA_SHIFT (10)
#define AMI_SLP_TYPB_SHIFT (10)
#define AMI_S5_NAME_OP_OFFSET_1 (-1)
#define AMI_S5_NAME_OP_OFFSET_2 (-2)
#define AMI_S5_PACKAGE_OP_OFFSET (4)
#define AMI_S5_PACKET_LENGTH_OFFSET (5)

static struct acpi_rsdt {
	struct acpi_sdt_header	hdr;
	u32_t			data[MAX_RSDT];
} rsdt;

static struct {
	char	signature [ACPI_SDT_SIGNATURE_LEN + 1];
	size_t	length;
} sdt_trans[MAX_RSDT];

static int sdt_count;
static u16_t pm1a_cnt_blk = 0;
static u16_t pm1b_cnt_blk = 0;
static u16_t slp_typa = 0;
static u16_t slp_typb = 0;

/*
 * Snapshot of the MADT ("APIC" table) taken during acpi_init(), while the
 * boot-time identity / physmap mapping still resolves to the real BIOS-supplied
 * bytes.  Later in boot, pg_map() and pg_alloc_page() can hand out the same
 * low-memory physical pages for kernel allocations (the multiboot e820 doesn't
 * always reserve QEMU's ACPI table area), which destroys the original contents.
 * By snapshotting into BSS we make acpi_get_lapic_next() / acpi_get_ioapic_next()
 * independent of whatever happens to the physical page after boot.
 */
#define MADT_SNAPSHOT_MAX	4096	/* MADT for many CPUs fits comfortably */
static u8_t madt_snapshot[MADT_SNAPSHOT_MAX];
static size_t madt_snapshot_len = 0;

static int acpi_check_csum(struct acpi_sdt_header * tb, size_t size)
{
	u8_t total = 0;
	int i;
	for (i = 0; i < size; i++)
		total += ((unsigned char *)tb)[i];
	return total == 0 ? 0 : -1;
}

static int acpi_check_signature(const char * orig, const char * match)
{
	return strncmp(orig, match, ACPI_SDT_SIGNATURE_LEN);
}

/*
 * Convert a physical address to a kernel-virtual address that stays
 * valid even after the BSP's PML4[0] identity map has been mutated by
 * pg_map() during user-space setup.  We route through the kernel
 * physmap at PML4[256] (KERN_PHYSMAP = 0xffff800000000000), which
 * pg_identity() sets up as a separate, never-mutated 4 GB mapping.
 *
 * Without this, ACPI reads done by acpi_init() (early, identity map
 * intact) succeed but reads done later by smp_init() →
 * acpi_get_lapic_next() return garbage from whatever phys page the
 * identity PD has since been redirected to.
 */
#define KERN_PHYSMAP	((phys_bytes)0xffff800000000000ULL)

static phys_bytes acpi_phys2vir(phys_bytes p)
{
	if(!vm_running) {
		phys_bytes va = p + KERN_PHYSMAP;
		DEBUGEXTRA(("acpi: phys 0x%lx -> physmap 0x%lx\n",
		    (unsigned long)p, (unsigned long)va));
		return va;
	}
	panic("acpi: can't get virtual address of arbitrary physical address");
}

static int acpi_phys_copy(phys_bytes phys, void *target, size_t len)
{
	if(!vm_running) {
		memcpy(target, (void *)(uintptr_t)(phys + KERN_PHYSMAP), len);
		return 0;
	}
	panic("can't acpi_phys_copy with vm");
}

static int acpi_read_sdt_at(phys_bytes addr,
				struct acpi_sdt_header * tb,
				size_t size,
				const char * name)
{
	struct acpi_sdt_header hdr;

	/* if NULL is supplied, we only return the size of the table */
	if (tb == NULL) {
		if (read_func(addr, &hdr, sizeof(struct acpi_sdt_header))) {
			printf("ERROR acpi cannot read %s header\n", name);
			return -1;
		}

		return hdr.length;
	}

	if (read_func(addr, tb, sizeof(struct acpi_sdt_header))) {
		printf("ERROR acpi cannot read %s header\n", name);
		return -1;
	}

	if (acpi_check_signature(tb->signature, name)) {
		printf("ERROR acpi %s signature does not match\n", name);
		return -1;
	}

	if (size < tb->length) {
		printf("ERROR acpi buffer too small for %s\n", name);
		return -1;
	}

	if (read_func(addr, tb, size)) {
		printf("ERROR acpi cannot read %s\n", name);
		return -1;
	}

	if (acpi_check_csum(tb, tb->length)) {
		printf("ERROR acpi %s checksum does not match\n", name);
		return -1;
	}

	return tb->length;
}

phys_bytes acpi_get_table_base(const char * name)
{
	int i;

	for(i = 0; i < sdt_count; i++) {
		if (strncmp(name, sdt_trans[i].signature,
					ACPI_SDT_SIGNATURE_LEN) == 0)
			return (phys_bytes) rsdt.data[i];
	}

	return (phys_bytes) NULL;
}

/*===========================================================================*
 *			   acpi_reserve_tables				     *
 *===========================================================================*/
/* Cut the physical pages occupied by ACPI tables (RSDT + every subtable
 * we discovered) out of the kernel's memmap, so that the page allocator
 * doesn't hand them out to user processes and clobber their contents
 * before the userspace ACPI service runs.
 *
 * Must be called AFTER acpi_init() and BEFORE the rest of boot starts
 * carving the memmap for process allocations.
 */
void acpi_reserve_tables(void)
{
	extern void cut_memmap(kinfo_t *cbi, phys_bytes start, phys_bytes end);
	extern kinfo_t kinfo;
	int i;

	/* Reserve RSDT itself (header + entries). */
	{
		phys_bytes rsdt_start = acpi_rsdp.rsdt_addr;
		phys_bytes rsdt_end = rsdt_start +
		    sizeof(struct acpi_sdt_header) + sdt_count * sizeof(u32_t);
		/* Round to page boundaries. */
		rsdt_start &= ~((phys_bytes)0xFFF);
		rsdt_end = (rsdt_end + 0xFFF) & ~((phys_bytes)0xFFF);
		printf("acpi: reserve RSDT phys [0x%lx, 0x%lx)\n",
		    (unsigned long)rsdt_start, (unsigned long)rsdt_end);
		cut_memmap(&kinfo, rsdt_start, rsdt_end);
	}

	/* Reserve each subtable. */
	for (i = 0; i < sdt_count; i++) {
		phys_bytes start = (phys_bytes)rsdt.data[i];
		phys_bytes end = start + sdt_trans[i].length;
		/* Round to page boundaries. */
		start &= ~((phys_bytes)0xFFF);
		end = (end + 0xFFF) & ~((phys_bytes)0xFFF);
		printf("acpi: reserve %.4s phys [0x%lx, 0x%lx)\n",
		    sdt_trans[i].signature, (unsigned long)start,
		    (unsigned long)end);
		cut_memmap(&kinfo, start, end);
	}
}

size_t acpi_get_table_length(const char * name)
{
	int i;

	for(i = 0; i < sdt_count; i++) {
		if (strncmp(name, sdt_trans[i].signature,
					ACPI_SDT_SIGNATURE_LEN) == 0)
			return sdt_trans[i].length;
	}

	return 0;
}

static void * acpi_madt_get_typed_item(struct acpi_madt_hdr * hdr,
					unsigned char type,
					unsigned idx)
{
	u8_t * t, * end;
	int i;

	BOOT_VERBOSE({
		u8_t *bp = (u8_t *)hdr;
		printf("madt_walk: sizeof(madt_hdr)=%lu sizeof(sdt_hdr)=%lu\n",
		    (unsigned long)sizeof(struct acpi_madt_hdr),
		    (unsigned long)sizeof(struct acpi_sdt_header));
		printf("madt_walk: bytes @ hdr: %02x %02x %02x %02x | "
		    "%02x %02x %02x %02x | %02x %02x %02x %02x | "
		    "%02x %02x %02x %02x\n",
		    bp[0], bp[1], bp[2], bp[3],
		    bp[4], bp[5], bp[6], bp[7],
		    bp[8], bp[9], bp[10], bp[11],
		    bp[12], bp[13], bp[14], bp[15]);
	});

	t = (u8_t *) hdr + sizeof(struct acpi_madt_hdr);
	end = (u8_t *) hdr + hdr->hdr.length;

	BOOT_VERBOSE(printf(
	    "madt_walk: hdr=%p hdr_len=%u t=%p end=%p want_type=%u idx=%u\n",
	    hdr, (unsigned)hdr->hdr.length, t, end, (unsigned)type, idx));

	i = 0;
	while(t < end) {
		struct acpi_madt_item_hdr *ih = (struct acpi_madt_item_hdr *)t;
		BOOT_VERBOSE(printf("madt_walk: t=%p type=%u len=%u (i=%d)\n",
		    t, (unsigned)ih->type, (unsigned)ih->length, i));
		if (ih->length == 0) {
			/* Always-on: real corruption / spec violation. */
			printf("acpi: MADT BAD zero-length entry at %p, aborting walk\n", t);
			return NULL;
		}
		if (type == ih->type) {
			if (i == idx)
				return t;
			else
				i++;
		}
		t += ih->length;
	}

	return NULL;
}

static int acpi_rsdp_test(void * buff)
{
	struct acpi_rsdp * rsdp = (struct acpi_rsdp *) buff;

	if (!platform_tbl_checksum_ok(buff, 20))
		return 0;
	if (strncmp(rsdp->signature, "RSD PTR ", 8))
		return 0;

	return 1;
}

static int get_acpi_rsdp(void)
{
	u16_t ebda;
	/*
	 * Read 40:0Eh - to find the starting address of the EBDA.
	 */
	acpi_phys_copy (0x40E, &ebda, sizeof(ebda));
	if (ebda) {
		ebda <<= 4;
		if(platform_tbl_ptr(ebda, ebda + 0x400, 16, &acpi_rsdp,
					sizeof(acpi_rsdp), &machine.acpi_rsdp,
					acpi_rsdp_test))
			return 1;
	}

	/* try BIOS read only mem space */
	if(platform_tbl_ptr(0xE0000, 0x100000, 16, &acpi_rsdp,
				sizeof(acpi_rsdp), &machine.acpi_rsdp,
				acpi_rsdp_test))
		return 1;

	machine.acpi_rsdp = 0; /* RSDP cannot be found at this address therefore
				  it is a valid negative value */
	return 0;
}

static void acpi_init_poweroff(void)
{
	u8_t *ptr = NULL;
	u8_t *start = NULL;
	u8_t *end = NULL;
	struct acpi_fadt_header *fadt_header = NULL;
	struct acpi_rsdt * dsdt_header = NULL;
	char *msg = NULL;

	/* Everything used here existed since ACPI spec 1.0 */
	/* So we can safely use them */
	fadt_header = (struct acpi_fadt_header *)
		acpi_phys2vir(acpi_get_table_base("FACP"));
	if (fadt_header == NULL) {
		msg = "Could not load FACP";
		goto exit;
	}

	dsdt_header = (struct acpi_rsdt *)
		acpi_phys2vir((phys_bytes) fadt_header->dsdt);
	if (dsdt_header == NULL) {
		msg = "Could not load DSDT";
		goto exit;
	}

	pm1a_cnt_blk = fadt_header->pm1a_cnt_blk;
	pm1b_cnt_blk = fadt_header->pm1b_cnt_blk;

	ptr = start = (u8_t *) dsdt_header->data;
	end = start + dsdt_header->hdr.length - 4;

	/* See http://forum.osdev.org/viewtopic.php?t=16990 */
	/* for layout of \_S5 */
	while (ptr < end && memcmp(ptr, "_S5_", 4) != 0)
		ptr++;

	msg = "Could not read S5 data. Use default SLP_TYPa and SLP_TYPb";
	if (ptr >= end || ptr == start)
		goto exit;

	/* validate AML structure */
	if (*(ptr + AMI_S5_PACKAGE_OP_OFFSET) != AMI_PACKAGE_OP_CODE)
		goto exit;

	if ((ptr < start + (-AMI_S5_NAME_OP_OFFSET_2) ||
		(*(ptr + AMI_S5_NAME_OP_OFFSET_2) != AMI_NAME_OP_CODE ||
		 *(ptr + AMI_S5_NAME_OP_OFFSET_2 + 1) != '\\')) &&
		*(ptr + AMI_S5_NAME_OP_OFFSET_1) != AMI_NAME_OP_CODE)
		goto exit;

	ptr += AMI_S5_PACKET_LENGTH_OFFSET;
	if (ptr >= end)
		goto exit;

	/* package length */
	ptr += ((*ptr & AMI_PACKAGE_LENGTH_ENCODING_BITS_MASK) >>
		AMI_PACKAGE_LENGTH_ENCODING_BITS_SHIFT) +
		AMI_MIN_PACKAGE_LENGTH + AMI_NUM_ELEMENTS_LENGTH;
	if (ptr >= end)
		goto exit;

	if (*ptr == AMI_BYTE_PREFIX_CODE)
		ptr++; /* skip byte prefix */

	slp_typa = (*ptr) << AMI_SLP_TYPA_SHIFT;

	ptr++; /* move to SLP_TYPb */
	if (*ptr == AMI_BYTE_PREFIX_CODE)
		ptr++; /* skip byte prefix */

	slp_typb = (*ptr) << AMI_SLP_TYPB_SHIFT;

	msg = "poweroff initialized";

exit:
	if (msg) {
		DEBUGBASIC(("acpi: %s\n", msg));
	}
}

void acpi_init(void)
{
	int s, i;
	read_func = acpi_phys_copy;

	if (!get_acpi_rsdp()) {
		printf("WARNING : Cannot configure ACPI\n");
		return;
	}

	/* Diagnostic: dump the first 32 bytes the KERNEL sees at the rsdt
	 * physical address.  Used to validate whether userspace ACPI's
	 * vm_map_phys is mapping the same bytes the kernel does. */
	{
		unsigned char kbuf[32];
		int j;
		if (read_func(acpi_rsdp.rsdt_addr, kbuf, sizeof(kbuf)) == 0) {
			printf("acpi(kern): bytes at phys 0x%lx:",
			    (unsigned long)acpi_rsdp.rsdt_addr);
			for (j = 0; j < 32; j++)
				printf(" %02x", kbuf[j]);
			printf("\n");
		}
	}

	s = acpi_read_sdt_at(acpi_rsdp.rsdt_addr, (struct acpi_sdt_header *) &rsdt,
			sizeof(struct acpi_rsdt), ACPI_SDT_SIGNATURE(RSDT));

	sdt_count = (s - sizeof(struct acpi_sdt_header)) / sizeof(u32_t);

	for (i = 0; i < sdt_count; i++) {
		struct acpi_sdt_header hdr;
		int j;
		if (read_func(rsdt.data[i], &hdr, sizeof(struct acpi_sdt_header))) {
			printf("ERROR acpi cannot read header at 0x%x\n",
								rsdt.data[i]);
			return;
		}

		for (j = 0 ; j < ACPI_SDT_SIGNATURE_LEN; j++)
			sdt_trans[i].signature[j] = hdr.signature[j];
		sdt_trans[i].signature[ACPI_SDT_SIGNATURE_LEN] = '\0';
		sdt_trans[i].length = hdr.length;
	}

	BOOT_VERBOSE(printf("acpi: rsdt_addr=0x%lx sdt_count=%d\n",
	    (unsigned long)acpi_rsdp.rsdt_addr, sdt_count));
	BOOT_VERBOSE({
		int k;
		for (k = 0; k < sdt_count; k++)
			printf("acpi: sdt_trans[%d] signature='%s' addr=0x%lx len=%lu\n",
			    k, sdt_trans[k].signature,
			    (unsigned long)rsdt.data[k],
			    (unsigned long)sdt_trans[k].length);
	});

	acpi_init_poweroff();

	/*
	 * Snapshot the MADT now while the physical pages are still intact.
	 * After acpi_init() returns, the kernel proceeds to set up boot
	 * processes via libexec_pg_alloc()/pg_map(), and the e820-based memmap
	 * doesn't always exclude QEMU's low-memory ACPI table pages — so by
	 * the time smp_init() runs, the original MADT bytes may have been
	 * overwritten with kernel data.  All later MADT walks read from this
	 * snapshot instead of going back to physical memory.
	 */
	{
		phys_bytes apic_phys = acpi_get_table_base("APIC");
		size_t apic_len = acpi_get_table_length("APIC");
		if (apic_phys && apic_len && apic_len <= sizeof(madt_snapshot)) {
			if (read_func(apic_phys, madt_snapshot, apic_len) == 0) {
				madt_snapshot_len = apic_len;
				DEBUGBASIC(("acpi: snapshotted MADT (%lu bytes) "
				    "from phys 0x%lx\n",
				    (unsigned long)apic_len,
				    (unsigned long)apic_phys));
			} else {
				printf("acpi: WARNING MADT snapshot read failed\n");
			}
		} else if (apic_len > sizeof(madt_snapshot)) {
			printf("acpi: WARNING MADT too large for snapshot "
			    "(%lu > %lu); SMP discovery will fail\n",
			    (unsigned long)apic_len,
			    (unsigned long)sizeof(madt_snapshot));
		}
	}
}

struct acpi_madt_ioapic * acpi_get_ioapic_next(void)
{
	static unsigned idx = 0;
	struct acpi_madt_hdr *madt_hdr;
	struct acpi_madt_ioapic * ret;

	if (madt_snapshot_len == 0)
		return NULL;
	madt_hdr = (struct acpi_madt_hdr *)madt_snapshot;

	ret = (struct acpi_madt_ioapic *)
		acpi_madt_get_typed_item(madt_hdr, ACPI_MADT_TYPE_IOAPIC, idx);
	if (ret)
		idx++;

	return ret;
}

struct acpi_madt_lapic * acpi_get_lapic_next(void)
{
	static unsigned idx = 0;
	struct acpi_madt_hdr *madt_hdr;
	struct acpi_madt_lapic * ret;

	if (madt_snapshot_len == 0)
		return NULL;
	madt_hdr = (struct acpi_madt_hdr *)madt_snapshot;

	for (;;) {
		ret = (struct acpi_madt_lapic *)
			acpi_madt_get_typed_item(madt_hdr,
					ACPI_MADT_TYPE_LAPIC, idx);
		if (!ret)
			break;

		idx++;

		/* report only usable CPUs */
		if (ret->flags & 1)
			break;
	}

	return ret;
}

void __k_unpaged_acpi_poweroff(void)
{
	/* NO OP poweroff symbol*/
}

void acpi_poweroff(void)
{
	if (pm1a_cnt_blk == 0) {
		return;
	}
	outw(pm1a_cnt_blk, slp_typa | SLP_EN_CODE);
	if (pm1b_cnt_blk != 0) {
		outw(pm1b_cnt_blk, slp_typb | SLP_EN_CODE);
	}
}
