/*	cxxprobe - is C++ actually usable on MINIX?
 *
 * Nothing in the tree had ever built a C++ program for the target.  libc++ was
 * compiled as a library and left there; no executable ever linked against it,
 * so whether a C++ program could be built and *run* on MINIX was simply never
 * established either way.
 *
 * It matters now because it is the gate for Qt, and so for anything above Qt.
 * A toolkit does not merely use the STL: it throws, it catches, it relies on
 * RTTI for its meta-object casts, and it runs destructors while an exception
 * unwinds the stack.  Any one of those failing makes the whole tier impossible,
 * and each fails in a different place -- so each is checked separately here
 * rather than inferred from a program that merely started.
 *
 * The pieces do exist: libc++ carries its own C++ ABI (__cxa_throw and friends
 * are defined in it) and the unwinder (_Unwind_RaiseException) lives in libc.
 * They just had to be wired together; see this directory's Makefile for the
 * flags that do it.
 *
 * Exits 0 only if every check passes.
 */

#include <algorithm>
#include <exception>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <typeinfo>
#include <vector>

#include <cstdio>
#include <cstring>

static int failures;

static void
check(const char *what, bool ok, const char *detail = "")
{
	if (ok) {
		std::printf("%-46s -> OK\n", what);
	} else {
		std::printf("%-46s -> FAIL (%s)\n", what, detail);
		failures++;
	}
	std::fflush(stdout);
}

/* A destructor that records that it ran: proof the stack really unwound. */
struct Unwound {
	bool *flag;
	explicit Unwound(bool *f) : flag(f) {}
	~Unwound() { *flag = true; }
};

struct Base {
	virtual ~Base() = default;
	virtual int id() const { return 1; }
};

struct Derived : Base {
	int id() const override { return 2; }
	int extra = 42;
};

int
main()
{
	std::printf("cxxprobe: C++17 on MINIX\n\n");

	/* 1. The STL, and with it operator new/delete and libc++'s guts. */
	{
		std::vector<std::string> v{"minix", "wayland", "qt"};
		std::sort(v.begin(), v.end());
		check("STL: vector<string> + sort",
		    v.size() == 3 && v[0] == "minix" && v[2] == "wayland",
		    "wrong contents");

		std::map<std::string, int> m{{"a", 1}, {"b", 2}};
		m["c"] = 3;
		check("STL: map + operator[]", m.size() == 3 && m["b"] == 2,
		    "wrong contents");
	}

	/* 2. Smart pointers: templates, move semantics, and delete on scope exit. */
	{
		auto p = std::make_unique<Derived>();
		std::shared_ptr<Base> sp = std::make_shared<Derived>();
		check("smart pointers: unique_ptr + shared_ptr",
		    p != nullptr && sp != nullptr && sp.use_count() == 1,
		    "null or wrong refcount");
	}

	/*
	 * 3. RTTI.  Qt's object model leans on dynamic_cast and typeid; if the
	 * type info emitted by the compiler does not line up with what libc++
	 * expects at run time, this returns null while everything still links.
	 */
	{
		std::unique_ptr<Base> owner = std::make_unique<Derived>();
		/* A plain pointer: typeid() on a smart-pointer dereference is a
		 * function call, which the compiler rightly objects to. */
		Base *b = owner.get();
		Derived *d = dynamic_cast<Derived *>(b);

		check("RTTI: dynamic_cast to the derived type",
		    d != nullptr && d->extra == 42, "cast failed");
		check("RTTI: virtual dispatch", b->id() == 2, "wrong override");
		check("RTTI: typeid names the dynamic type",
		    std::strstr(typeid(*b).name(), "Derived") != nullptr,
		    typeid(*b).name());
	}

	/*
	 * 4. Exceptions.  This is the one that needs the unwinder in libc to
	 * agree with the ABI in libc++.  Throwing is not enough to prove it:
	 * the stack has to actually unwind, so a destructor between the throw
	 * and the catch must have run by the time we arrive.
	 */
	{
		bool unwound = false;
		bool caught = false;
		std::string msg;

		try {
			Unwound guard(&unwound);
			throw std::runtime_error("thrown");
		} catch (const std::runtime_error &e) {
			caught = true;
			msg = e.what();
		}

		check("exceptions: throw and catch by type", caught,
		    "not caught");
		check("exceptions: what() survives the throw", msg == "thrown",
		    msg.c_str());
		check("exceptions: the stack really unwound "
		    "(destructor ran)", unwound, "destructor did NOT run");
	}

	/* 5. Catching by base class, across a library boundary (std::exception). */
	{
		bool caught_as_base = false;

		try {
			throw std::out_of_range("range");
		} catch (const std::exception &e) {
			caught_as_base = (std::strcmp(e.what(), "range") == 0);
		}
		check("exceptions: caught as std::exception &",
		    caught_as_base, "base-class catch failed");
	}

	/* 6. A throw from a nested call, unwinding several frames. */
	{
		bool ok = false;

		try {
			std::vector<int> v{1, 2, 3};
			(void)v.at(10);		/* throws out_of_range */
		} catch (const std::out_of_range &) {
			ok = true;
		}
		check("exceptions: thrown from inside libc++ (vector::at)", ok,
		    "not caught");
	}

	std::printf("\ncxxprobe: %s\n",
	    failures == 0 ? "ALL PASS" : "FAILURES PRESENT");
	return failures == 0 ? 0 : 1;
}
