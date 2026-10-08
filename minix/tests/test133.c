/* Test 133 - SQLite used by several threads at once.
 *
 * libsqlite3 is built thread-safe (serialized), as on NetBSD: its mutexes are
 * libc's __libc_mutex_* calls, no-ops in a program without threads and the
 * real ones of libpthread in a program with them.  Threads share one
 * connection, which SQLite must serialize; then each has a connection of its
 * own to a database in WAL mode, writers moving amounts between rows so that
 * the total never changes and readers checking the total meanwhile.
 */
#include <sys/types.h>
#include <pthread.h>
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "common.h"

int max_error = 3;

#define DB		"t133.db"
#define THREADS		4
#define UPDATES		300
#define ROWS		20
#define TOTAL		(ROWS * 1000)
#define MOVES		200

static sqlite3 *shared;
static volatile int failures;
static int writers_done;

static void
fail(const char *what, sqlite3 *db)
{

	printf("%s: %s\n", what, db ? sqlite3_errmsg(db) : "?");
	failures++;
}

static sqlite3 *
open_db(void)
{
	sqlite3 *db;

	if (sqlite3_open(DB, &db) != SQLITE_OK) e(90);
	sqlite3_busy_timeout(db, 10000);
	return db;
}

static void
exec(sqlite3 *db, const char *sql)
{
	char *msg = NULL;

	if (sqlite3_exec(db, sql, NULL, NULL, &msg) != SQLITE_OK) {
		printf("'%s': %s\n", sql, msg ? msg : "?");
		sqlite3_free(msg);
		e(91);
	}
}

static long
query_long(sqlite3 *db, const char *sql)
{
	sqlite3_stmt *st;
	long v = -1;

	if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) != SQLITE_OK) {
		fail("prepare", db);
		return -1;
	}
	if (sqlite3_step(st) == SQLITE_ROW)
		v = sqlite3_column_int64(st, 0);
	else
		fail("step", db);
	sqlite3_finalize(st);
	return v;
}

static void
make_table(sqlite3 *db, long value)
{
	char sql[96];
	int i;

	exec(db, "CREATE TABLE t (id INTEGER PRIMARY KEY, v INTEGER)");
	exec(db, "BEGIN");
	for (i = 0; i < ROWS; i++) {
		snprintf(sql, sizeof(sql), "INSERT INTO t VALUES (%d, %ld)", i,
		    value);
		exec(db, sql);
	}
	exec(db, "COMMIT");
}

static void *
bump(void *arg)
{
	sqlite3_stmt *st;
	int i, id = (int)(long)arg;

	/* Prepared statements and steps on one connection, from all threads. */
	for (i = 0; i < UPDATES; i++) {
		if (sqlite3_prepare_v2(shared,
		    "UPDATE t SET v = v + 1 WHERE id = ?", -1, &st,
		    NULL) != SQLITE_OK) {
			fail("prepare", shared);
			break;
		}
		sqlite3_bind_int(st, 1, (id + i) % ROWS);
		if (sqlite3_step(st) != SQLITE_DONE)
			fail("update", shared);
		sqlite3_finalize(st);
	}
	return NULL;
}

static void
test_shared_connection(void)
{
	pthread_t t[THREADS];
	long sum;
	int i;

	subtest = 2;
	(void)unlink(DB);
	if (sqlite3_open_v2(DB, &shared, SQLITE_OPEN_READWRITE |
	    SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, NULL) != SQLITE_OK)
		e(1);
	make_table(shared, 0);
	failures = 0;
	for (i = 0; i < THREADS; i++)
		if (pthread_create(&t[i], NULL, bump, (void *)(long)i) != 0)
			e(2);
	for (i = 0; i < THREADS; i++)
		if (pthread_join(t[i], NULL) != 0) e(3);
	if (failures != 0) e(4);
	sum = query_long(shared, "SELECT sum(v) FROM t");
	if (sum != THREADS * UPDATES) {
		printf("sum %ld, expected %d\n", sum, THREADS * UPDATES);
		e(5);
	}
	if (query_long(shared, "SELECT count(*) FROM t") != ROWS) e(6);
	sqlite3_close(shared);
}

static void *
writer(void *arg)
{
	sqlite3 *db = open_db();
	char sql[128];
	unsigned int seed = (unsigned int)(long)arg;
	int i, from, to, amount;

	for (i = 0; i < MOVES; i++) {
		from = rand_r(&seed) % ROWS;
		to = rand_r(&seed) % ROWS;
		amount = rand_r(&seed) % 100;
		snprintf(sql, sizeof(sql), "BEGIN IMMEDIATE; "
		    "UPDATE t SET v = v - %d WHERE id = %d; "
		    "UPDATE t SET v = v + %d WHERE id = %d; COMMIT",
		    amount, from, amount, to);
		if (sqlite3_exec(db, sql, NULL, NULL, NULL) != SQLITE_OK) {
			fail("move", db);
			(void)sqlite3_exec(db, "ROLLBACK", NULL, NULL, NULL);
		}
	}
	sqlite3_close(db);
	__atomic_fetch_add(&writers_done, 1, __ATOMIC_SEQ_CST);
	return NULL;
}

static void *
reader(void *arg)
{
	sqlite3 *db = open_db();
	long sum;
	int reads = 0;

	while (__atomic_load_n(&writers_done, __ATOMIC_SEQ_CST) < THREADS / 2 ||
	    reads < 10) {
		sum = query_long(db, "SELECT sum(v) FROM t");
		if (sum != TOTAL) {
			printf("reader saw total %ld\n", sum);
			failures++;
			break;
		}
		reads++;
	}
	sqlite3_close(db);
	return NULL;
}

static void
test_wal_threads(void)
{
	pthread_t t[THREADS];
	sqlite3 *db;
	int i;

	subtest = 3;
	(void)unlink(DB);
	(void)unlink(DB "-wal");
	(void)unlink(DB "-shm");
	db = open_db();
	exec(db, "PRAGMA journal_mode=WAL");
	make_table(db, 1000);
	failures = 0;
	writers_done = 0;
	for (i = 0; i < THREADS; i++)
		if (pthread_create(&t[i], NULL, (i % 2) ? reader : writer,
		    (void *)(long)(i + 1)) != 0)
			e(2);
	for (i = 0; i < THREADS; i++)
		if (pthread_join(t[i], NULL) != 0) e(3);
	if (failures != 0) e(4);
	if (query_long(db, "SELECT sum(v) FROM t") != TOTAL) e(5);
	sqlite3_close(db);
}

int
main(int argc, char **argv)
{

	start(133);

	subtest = 1;
	/* Built serialized: 1 (0 would be no locking at all). */
	if (sqlite3_threadsafe() != 1) {
		printf("sqlite3_threadsafe() = %d\n", sqlite3_threadsafe());
		e(1);
	}

	test_shared_connection();
	test_wal_threads();

	quit();
	return 0;
}
