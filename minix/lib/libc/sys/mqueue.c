/*	POSIX message queues - mq_open(3) and co.				*/
/*
 * The IPC server keeps the queues (minix/servers/ipc/mqueue.c).  As for
 * shm_open(), a queue's name is a token file under /var/mqueue: opening it
 * gives the name its permissions and the queue its identity (the token's dev
 * and ino), and its file descriptor is the mqd_t, which is therefore
 * inherited across fork, closed on exec, and carries the access mode and
 * O_NONBLOCK of the open.
 */

#include <sys/cdefs.h>
#include <lib.h>

#include <minix/rs.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <mqueue.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define MQ_DIR		"/var/mqueue/"

static int
ipc_endpt(endpoint_t *pt)
{

	if (minix_rs_lookup("ipc", pt) != 0) {
		errno = ENOSYS;
		return -1;
	}
	return 0;
}

/*
 * Map a queue name to its token file.  POSIX leaves names without a leading
 * slash, and names with further slashes, to the implementation: require the
 * former and refuse the latter, as NetBSD does.
 */
static int
mq_path(const char *name, char *buf, size_t bufsz)
{

	if (name == NULL || name[0] != '/' || name[1] == '\0' ||
	    strchr(name + 1, '/') != NULL) {
		errno = EINVAL;
		return -1;
	}
	if (strlen(name + 1) > NAME_MAX) {
		errno = ENAMETOOLONG;
		return -1;
	}
	if ((size_t)snprintf(buf, bufsz, "%s%s", MQ_DIR, name + 1) >= bufsz) {
		errno = ENAMETOOLONG;
		return -1;
	}
	return 0;
}

/*
 * Prepare a request about the queue open as 'mqd': its key, and the open
 * file's flags in '*flp'.  'need' is the access the call needs (O_RDONLY for
 * receiving, O_WRONLY for sending, -1 for none).
 */
static int
mq_prepare(mqd_t mqd, message *m, int need, int *flp, endpoint_t *pt)
{
	struct stat st;
	int fl, acc;

	if ((fl = fcntl(mqd, F_GETFL)) == -1)
		return -1;			/* EBADF */
	acc = fl & O_ACCMODE;
	if ((need == O_RDONLY && acc == O_WRONLY) ||
	    (need == O_WRONLY && acc == O_RDONLY)) {
		errno = EBADF;
		return -1;
	}
	if (fstat(mqd, &st) != 0)
		return -1;
	if (ipc_endpt(pt) != 0)
		return -1;
	memset(m, 0, sizeof(*m));
	m->m_lc_ipc_mqio.dev = st.st_dev;
	m->m_lc_ipc_mqio.ino = st.st_ino;
	if (flp != NULL)
		*flp = fl;
	return 0;
}

mqd_t
mq_open(const char *name, int oflag, ...)
{
	char path[PATH_MAX];
	struct mq_attr *attr = NULL;
	struct stat st;
	message m;
	endpoint_t pt;
	mode_t mode = 0;
	va_list ap;
	int fd, err, fresh = 0, acc;

	if (mq_path(name, path, sizeof(path)) < 0)
		return (mqd_t)-1;
	if (oflag & O_CREAT) {
		va_start(ap, oflag);
		mode = (mode_t)va_arg(ap, int);
		attr = va_arg(ap, struct mq_attr *);
		va_end(ap);
		if (attr != NULL &&
		    (attr->mq_maxmsg <= 0 || attr->mq_msgsize <= 0)) {
			errno = EINVAL;
			return (mqd_t)-1;
		}
	}
	acc = oflag & O_ACCMODE;
	if (acc != O_RDONLY && acc != O_WRONLY && acc != O_RDWR) {
		errno = EINVAL;
		return (mqd_t)-1;
	}

	oflag &= (O_ACCMODE | O_CREAT | O_EXCL | O_NONBLOCK);
	oflag |= O_CLOEXEC;	/* message queues are closed on exec */
	fd = -1;
	if (oflag & O_CREAT) {
		/* Know whether we created the token: a new file may have the
		 * inode number of an unlinked queue's.
		 */
		fd = open(path, oflag | O_EXCL, mode);
		if (fd >= 0)
			fresh = 1;
		else if (errno != EEXIST || (oflag & O_EXCL))
			return (mqd_t)-1;
	}
	if (fd < 0 && (fd = open(path, oflag & ~(O_CREAT | O_EXCL))) < 0)
		return (mqd_t)-1;
	if (fstat(fd, &st) != 0 || ipc_endpt(&pt) != 0)
		goto fail;

	memset(&m, 0, sizeof(m));
	m.m_lc_ipc_mqopen.dev = st.st_dev;
	m.m_lc_ipc_mqopen.ino = st.st_ino;
	if (attr != NULL) {
		m.m_lc_ipc_mqopen.maxmsg = attr->mq_maxmsg;
		m.m_lc_ipc_mqopen.msgsize = attr->mq_msgsize;
	}
	m.m_lc_ipc_mqopen.flags = (fresh ? IPC_MQ_FRESH : 0) |
	    ((oflag & O_CREAT) ? IPC_MQ_CREAT : 0);
	if (_syscall(pt, IPC_MQ_OPEN, &m) < 0)
		goto fail;
	return fd;

fail:
	err = errno;
	close(fd);
	if (fresh)
		(void)unlink(path);
	errno = err;
	return (mqd_t)-1;
}

int
mq_close(mqd_t mqd)
{
	message m;
	endpoint_t pt;

	/* POSIX: our notification registration on the queue goes too. */
	if (mq_prepare(mqd, &m, -1, NULL, &pt) == 0)
		(void)_syscall(pt, IPC_MQ_NOTIFY, &m);
	return close(mqd);
}

int
mq_unlink(const char *name)
{
	char path[PATH_MAX];
	struct stat st;
	message m;
	endpoint_t pt;

	if (mq_path(name, path, sizeof(path)) < 0)
		return -1;
	if (stat(path, &st) != 0)
		return -1;			/* ENOENT, EACCES */
	if (unlink(path) != 0)
		return -1;
	if (ipc_endpt(&pt) == 0) {
		memset(&m, 0, sizeof(m));
		m.m_lc_ipc_mqio.dev = st.st_dev;
		m.m_lc_ipc_mqio.ino = st.st_ino;
		(void)_syscall(pt, IPC_MQ_UNLINK, &m);
	}
	return 0;
}

static int
mq_send_common(mqd_t mqd, const char *msg, size_t len, unsigned prio,
	const struct timespec *abs_timeout)
{
	message m;
	endpoint_t pt;
	int fl;

	if (mq_prepare(mqd, &m, O_WRONLY, &fl, &pt) != 0)
		return -1;
	m.m_lc_ipc_mqio.buf = (vir_bytes)msg;
	m.m_lc_ipc_mqio.len = len;
	m.m_lc_ipc_mqio.prio = prio;
	if (fl & O_NONBLOCK)
		m.m_lc_ipc_mqio.flags |= IPC_MQ_NONBLOCK;
	if (abs_timeout != NULL) {
		m.m_lc_ipc_mqio.flags |= IPC_MQ_TIMED;
		m.m_lc_ipc_mqio.tv_sec = abs_timeout->tv_sec;
		m.m_lc_ipc_mqio.tv_nsec = abs_timeout->tv_nsec;
	}
	return _syscall(pt, IPC_MQ_SEND, &m) < 0 ? -1 : 0;
}

static ssize_t
mq_receive_common(mqd_t mqd, char *msg, size_t len, unsigned *priop,
	const struct timespec *abs_timeout)
{
	message m;
	endpoint_t pt;
	int fl;

	if (mq_prepare(mqd, &m, O_RDONLY, &fl, &pt) != 0)
		return -1;
	m.m_lc_ipc_mqio.buf = (vir_bytes)msg;
	m.m_lc_ipc_mqio.len = len;
	if (fl & O_NONBLOCK)
		m.m_lc_ipc_mqio.flags |= IPC_MQ_NONBLOCK;
	if (abs_timeout != NULL) {
		m.m_lc_ipc_mqio.flags |= IPC_MQ_TIMED;
		m.m_lc_ipc_mqio.tv_sec = abs_timeout->tv_sec;
		m.m_lc_ipc_mqio.tv_nsec = abs_timeout->tv_nsec;
	}
	if (_syscall(pt, IPC_MQ_RECEIVE, &m) < 0)
		return -1;
	if (priop != NULL)
		*priop = m.m_ipc_lc_mq.prio;
	return m.m_ipc_lc_mq.len;
}

int
mq_send(mqd_t mqd, const char *msg, size_t len, unsigned prio)
{

	return mq_send_common(mqd, msg, len, prio, NULL);
}

ssize_t
mq_receive(mqd_t mqd, char *msg, size_t len, unsigned *priop)
{

	return mq_receive_common(mqd, msg, len, priop, NULL);
}

int
__mq_timedsend50(mqd_t mqd, const char *msg, size_t len, unsigned prio,
	const struct timespec *abs_timeout)
{

	if (abs_timeout == NULL) {
		errno = EINVAL;
		return -1;
	}
	return mq_send_common(mqd, msg, len, prio, abs_timeout);
}

ssize_t
__mq_timedreceive50(mqd_t mqd, char * __restrict msg, size_t len,
	unsigned * __restrict priop, const struct timespec * __restrict abs_timeout)
{

	if (abs_timeout == NULL) {
		errno = EINVAL;
		return -1;
	}
	return mq_receive_common(mqd, msg, len, priop, abs_timeout);
}

int
mq_getattr(mqd_t mqd, struct mq_attr *attr)
{
	message m;
	endpoint_t pt;
	int fl;

	if (mq_prepare(mqd, &m, -1, &fl, &pt) != 0)
		return -1;
	if (_syscall(pt, IPC_MQ_GETATTR, &m) < 0)
		return -1;
	memset(attr, 0, sizeof(*attr));
	attr->mq_flags = fl & O_NONBLOCK;
	attr->mq_maxmsg = m.m_ipc_lc_mq.maxmsg;
	attr->mq_msgsize = m.m_ipc_lc_mq.msgsize;
	attr->mq_curmsgs = m.m_ipc_lc_mq.curmsgs;
	return 0;
}

int
mq_setattr(mqd_t mqd, const struct mq_attr * __restrict attr,
	struct mq_attr * __restrict oattr)
{
	struct mq_attr old;
	int fl;

	/* Only O_NONBLOCK can change, and it belongs to the open file. */
	if (mq_getattr(mqd, &old) != 0)
		return -1;
	if ((fl = fcntl(mqd, F_GETFL)) == -1)
		return -1;
	fl = (fl & ~O_NONBLOCK) | (attr->mq_flags & O_NONBLOCK);
	if (fcntl(mqd, F_SETFL, fl) == -1)
		return -1;
	if (oattr != NULL)
		*oattr = old;
	return 0;
}

int
mq_notify(mqd_t mqd, const struct sigevent *notification)
{
	message m;
	endpoint_t pt;

	if (mq_prepare(mqd, &m, -1, NULL, &pt) != 0)
		return -1;
	if (notification != NULL) {
		switch (notification->sigev_notify) {
		case SIGEV_NONE:
			break;
		case SIGEV_SIGNAL:
			if (notification->sigev_signo <= 0 ||
			    notification->sigev_signo >= _NSIG) {
				errno = EINVAL;
				return -1;
			}
			m.m_lc_ipc_mqio.prio = notification->sigev_signo;
			m.m_lc_ipc_mqio.buf =
			    (vir_bytes)notification->sigev_value.sival_ptr;
			break;
		default:			/* SIGEV_THREAD: as on NetBSD */
			errno = EINVAL;
			return -1;
		}
		m.m_lc_ipc_mqio.flags = IPC_MQ_REGISTER;
		m.m_lc_ipc_mqio.len = (size_t)getpid();
	}
	return _syscall(pt, IPC_MQ_NOTIFY, &m) < 0 ? -1 : 0;
}
