#ifndef _IPC_CONST_H
#define _IPC_CONST_H

#include <machine/ipcconst.h>

 /* System call numbers that are passed when trapping to the kernel. */
#define SEND		   1	/* blocking send */
#define RECEIVE		   2	/* blocking receive */
#define SENDREC	 	   3  	/* SEND + RECEIVE */
#define NOTIFY		   4	/* asynchronous notify */
#define SENDNB             5    /* nonblocking send */
#define MINIX_KERNINFO     6    /* request kernel info structure */
#define SENDA		   16	/* asynchronous send */
#define IPCNO_HIGHEST	SENDA
/* IPC message payload size. On i386/arm the payload is 56 bytes.
 * On amd64 (LP64), pointers and longs are 8 bytes, so structs are larger;
 * the maximum payload size is 80 bytes. */
#if defined(__x86_64__) || defined(__amd64__)
#define _IPC_MSG_SIZE	96
#else
#define _IPC_MSG_SIZE	56
#endif

/* Check that the message payload type doesn't grow past the maximum IPC payload size.
 * This is a compile time check. On i386/arm all structs must be exactly 56 bytes;
 * on amd64 they may be anywhere from 56..80 bytes due to LP64 type size differences. */
#if defined(__x86_64__) || defined(__amd64__)
#define _ASSERT_MSG_SIZE(msg_type) \
    typedef int _ASSERT_##msg_type[/* CONSTCOND */sizeof(msg_type) <= _IPC_MSG_SIZE ? 1 : -1]
#else
#define _ASSERT_MSG_SIZE(msg_type) \
    typedef int _ASSERT_##msg_type[/* CONSTCOND */sizeof(msg_type) == _IPC_MSG_SIZE ? 1 : -1]
#endif

/* Macros for IPC status code manipulation. */
#define IPC_STATUS_CALL_SHIFT	0
#define IPC_STATUS_CALL_MASK	0x3F
#define IPC_STATUS_CALL(status)	\
	(((status) >> IPC_STATUS_CALL_SHIFT) & IPC_STATUS_CALL_MASK)
#define IPC_STATUS_CALL_TO(call) \
	(((call) & IPC_STATUS_CALL_MASK) << IPC_STATUS_CALL_SHIFT)

#define IPC_FLG_MSG_FROM_KERNEL	1 /* this message originated in the kernel on
				     behalf of a process, this is a trusted
				     message, never reply to the sender
				 */
#define IPC_STATUS_FLAGS_SHIFT	16
#define IPC_STATUS_FLAGS(flgs)	((flgs) << IPC_STATUS_FLAGS_SHIFT)
#define IPC_STATUS_FLAGS_TEST(status, flgs)	\
		(((status) >> IPC_STATUS_FLAGS_SHIFT) & (flgs))
#endif /* IPC_CONST_H */
