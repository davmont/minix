/*	ffiprobe - verify libffi's ffi_call() ABI on this target
 *
 * libwayland dispatches every protocol request and event through
 * ffi_call(): wl_closure_invoke() builds an argument vector at runtime and
 * calls the handler with it.  If libffi's idea of the calling convention is
 * even slightly wrong, the failure does not look like a libffi bug -- it
 * looks like Wayland randomly corrupting object IDs.  So prove the ABI here,
 * on-target, before anything depends on it.
 *
 * The cases below are chosen for what wl_closure_invoke() actually does:
 * handlers take (data, resource, ...) with 32-bit ints, pointers and strings,
 * which routinely spills past the six SysV integer registers onto the stack.
 * Argument 7+ is where a broken ABI shows up first, so that case is explicit.
 *
 * Exits 0 only if every check passes.
 */

#include <sys/types.h>

#include <ffi.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int failures;

static void
check(const char *what, int ok, const char *detail)
{
	if (ok) {
		printf("%-46s -> OK\n", what);
	} else {
		printf("%-46s -> FAIL (%s)\n", what, detail);
		failures++;
	}
	fflush(stdout);
}

/* 1. The simplest possible call: two integers in registers. */
static int
add2(int a, int b)
{
	return a + b;
}

/* 2. A pointer-returning call, as wl_resource_create() would be. */
static void *
pick_ptr(void *a, void *b, int take_b)
{
	return take_b ? b : a;
}

/*
 * 3. The shape that matters: a Wayland-style handler.  Seven integer-class
 * arguments means the seventh is passed on the stack under SysV AMD64, so
 * this catches a register/stack boundary error -- the classic libffi porting
 * bug.  We fold every argument into the result so no argument can be silently
 * dropped or duplicated.
 */
static uint32_t
wl_style_handler(void *data, void *resource, uint32_t id, int32_t x,
    int32_t y, const char *title, int32_t fd)
{
	uint32_t acc = 0;

	if (data == NULL || resource == NULL || title == NULL)
		return 0;
	acc += *(uint32_t *)data;		/* 0x1000 */
	acc += *(uint32_t *)resource;		/* 0x2000 */
	acc += id;				/* 7 */
	acc += (uint32_t)x;			/* 100 */
	acc += (uint32_t)y;			/* 200 */
	acc += (uint32_t)strlen(title);		/* 5 ("hello") */
	acc += (uint32_t)fd;			/* 9 */
	return acc;
}

/*
 * 4. Deep stack spill: twelve integer args, six in registers and six on the
 * stack.  Sums to a value that changes if any single slot is misplaced.
 */
static int32_t
sum12(int32_t a1, int32_t a2, int32_t a3, int32_t a4, int32_t a5, int32_t a6,
    int32_t a7, int32_t a8, int32_t a9, int32_t a10, int32_t a11, int32_t a12)
{
	return a1 + a2 * 2 + a3 * 3 + a4 * 4 + a5 * 5 + a6 * 6 +
	    a7 * 7 + a8 * 8 + a9 * 9 + a10 * 10 + a11 * 11 + a12 * 12;
}

/*
 * 5. Doubles travel in SSE registers, a different classification path in
 * libffi's x86-64 back end.  Wayland does not use them, but a compositor's
 * wider dependencies (pixman, freetype) will, and getting this wrong now
 * would be a landmine.
 */
static double
scale(double v, double by)
{
	return v * by;
}

int
main(void)
{
	ffi_cif cif;
	ffi_type *args[12];
	void *vals[12];

	printf("ffiprobe: libffi ffi_call() ABI\n\n");
	printf("libffi built for: %s\n\n",
#if defined(__x86_64__) || defined(__amd64__)
	    "x86_64 (SysV AMD64)"
#else
	    "x86 (SysV i386)"
#endif
	    );

	/* 1. add2(17, 25) == 42 */
	{
		int a = 17, b = 25;
		ffi_arg rc = 0;

		args[0] = &ffi_type_sint;
		args[1] = &ffi_type_sint;
		vals[0] = &a;
		vals[1] = &b;

		check("ffi_prep_cif: int (int, int)",
		    ffi_prep_cif(&cif, FFI_DEFAULT_ABI, 2, &ffi_type_sint,
		    args) == FFI_OK, "prep_cif failed");

		ffi_call(&cif, FFI_FN(add2), &rc, vals);
		check("ffi_call: add2(17, 25) == 42", (int)rc == 42,
		    "wrong result");
	}

	/* 2. Pointer arguments and a pointer return. */
	{
		int one = 1, two = 2;
		void *pa = &one, *pb = &two;
		int take_b = 1;
		void *rp = NULL;

		args[0] = &ffi_type_pointer;
		args[1] = &ffi_type_pointer;
		args[2] = &ffi_type_sint;
		vals[0] = &pa;
		vals[1] = &pb;
		vals[2] = &take_b;

		check("ffi_prep_cif: void *(void *, void *, int)",
		    ffi_prep_cif(&cif, FFI_DEFAULT_ABI, 3, &ffi_type_pointer,
		    args) == FFI_OK, "prep_cif failed");

		ffi_call(&cif, FFI_FN(pick_ptr), &rp, vals);
		check("ffi_call: pointer return selects arg b", rp == pb,
		    "wrong pointer returned");
	}

	/*
	 * 3. The Wayland handler shape.  Expected:
	 *    0x1000 + 0x2000 + 7 + 100 + 200 + 5 + 9 == 0x3000 + 321
	 */
	{
		uint32_t data = 0x1000, resource = 0x2000;
		void *pdata = &data, *presource = &resource;
		uint32_t id = 7;
		int32_t x = 100, y = 200, fd = 9;
		const char *title = "hello";
		ffi_arg rc = 0;
		uint32_t want = 0x1000 + 0x2000 + 7 + 100 + 200 + 5 + 9;

		args[0] = &ffi_type_pointer;
		args[1] = &ffi_type_pointer;
		args[2] = &ffi_type_uint32;
		args[3] = &ffi_type_sint32;
		args[4] = &ffi_type_sint32;
		args[5] = &ffi_type_pointer;
		args[6] = &ffi_type_sint32;	/* 7th: passed on the stack */
		vals[0] = &pdata;
		vals[1] = &presource;
		vals[2] = &id;
		vals[3] = &x;
		vals[4] = &y;
		vals[5] = &title;
		vals[6] = &fd;

		check("ffi_prep_cif: wl_closure_invoke shape (7 args)",
		    ffi_prep_cif(&cif, FFI_DEFAULT_ABI, 7, &ffi_type_uint32,
		    args) == FFI_OK, "prep_cif failed");

		ffi_call(&cif, FFI_FN(wl_style_handler), &rc, vals);
		check("ffi_call: 7th arg crosses reg->stack boundary",
		    (uint32_t)rc == want, "argument misplaced on the stack");
	}

	/* 4. Twelve integers: six spill to the stack. */
	{
		int32_t a[12];
		ffi_arg rc = 0;
		int32_t want = 0;
		int i;

		for (i = 0; i < 12; i++) {
			a[i] = i + 1;
			args[i] = &ffi_type_sint32;
			vals[i] = &a[i];
			want += a[i] * (i + 1);
		}

		check("ffi_prep_cif: int32 (12 x int32)",
		    ffi_prep_cif(&cif, FFI_DEFAULT_ABI, 12, &ffi_type_sint32,
		    args) == FFI_OK, "prep_cif failed");

		ffi_call(&cif, FFI_FN(sum12), &rc, vals);
		check("ffi_call: 12 args, 6 spilled to stack",
		    (int32_t)rc == want, "stack argument misplaced");
	}

	/* 5. SSE class: double in, double out. */
	{
		double v = 2.5, by = 4.0, rd = 0.0;

		args[0] = &ffi_type_double;
		args[1] = &ffi_type_double;
		vals[0] = &v;
		vals[1] = &by;

		check("ffi_prep_cif: double (double, double)",
		    ffi_prep_cif(&cif, FFI_DEFAULT_ABI, 2, &ffi_type_double,
		    args) == FFI_OK, "prep_cif failed");

		ffi_call(&cif, FFI_FN(scale), &rd, vals);
		check("ffi_call: scale(2.5, 4.0) == 10.0", rd == 10.0,
		    "SSE argument class mishandled");
	}

	printf("\nffiprobe: %s\n", failures == 0 ? "ALL PASS" : "FAILURES PRESENT");
	return failures == 0 ? 0 : 1;
}
