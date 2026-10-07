/* Test 131 - SQLite in write-ahead logging (WAL) mode, across processes.
 *
 * WAL needs the -shm file of a database mapped shared and writable by all
 * the processes using the database: a test of shared file mappings as well.
 * One writer moves amounts between rows in transactions, so that the total
 * never changes; readers check the total in their own transactions meanwhile.
 * In WAL mode, readers are never blocked by the writer and always see a
 * consistent snapshot.
 */
#include <sys/types.h>
#include <sys/wait.h>
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "common.h"

int max_error = 3;

#define DB		"t131.db"
#define ROWS		50
#define TOTAL		(ROWS * 1000)
#define READERS		3
#define SECONDS		5

static sqlite3 *
open_db(void)
{
	sqlite3 *db;

	if (sqlite3_open(DB, &db) != SQLITE_OK) e(90);
	sqlite3_busy_timeout(db, 5000);
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
	long v;
	int r;

	if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) != SQLITE_OK) e(92);
	if ((r = sqlite3_step(st)) != SQLITE_ROW) {
		printf("'%s': step %d (%s)\n", sql, r, sqlite3_errmsg(db));
		e(93);
		v = -1;
	} else
		v = sqlite3_column_int64(st, 0);
	sqlite3_finalize(st);
	return v;
}

static void
setup(void)
{
	sqlite3 *db;
	char sql[128];
	int i;

	subtest = 1;
	(void)unlink(DB);
	db = open_db();
	{
		sqlite3_stmt *st;

		if (sqlite3_prepare_v2(db, "PRAGMA journal_mode=WAL", -1, &st,
		    NULL) != SQLITE_OK) e(2);
		if (sqlite3_step(st) != SQLITE_ROW) e(3);
		if (strcmp((const char *)sqlite3_column_text(st, 0), "wal") !=
		    0) {
			printf("journal_mode: %s\n",
			    sqlite3_column_text(st, 0));
			e(4);
		}
		sqlite3_finalize(st);
	}
	exec(db, "CREATE TABLE acct(id INTEGER PRIMARY KEY, bal INTEGER)");
	exec(db, "BEGIN");
	for (i = 0; i < ROWS; i++) {
		snprintf(sql, sizeof(sql), "INSERT INTO acct VALUES(%d, %d)",
		    i, TOTAL / ROWS);
		exec(db, sql);
	}
	exec(db, "COMMIT");
	if (query_long(db, "SELECT sum(bal) FROM acct") != TOTAL) e(5);
	/* The -shm file exists while the database is open in WAL mode. */
	if (access(DB "-shm", F_OK) != 0) e(6);
	sqlite3_close(db);
}

static void
reader(void)
{
	sqlite3 *db;
	time_t end = time(NULL) + SECONDS;
	long n = 0, total;

	errct = 0;
	db = open_db();
	while (time(NULL) < end) {
		/* A read transaction sees one snapshot: always the total. */
		exec(db, "BEGIN");
		total = query_long(db, "SELECT sum(bal) FROM acct");
		if (total != TOTAL) {
			printf("reader %d: total %ld\n", getpid(), total);
			e(10);
		}
		if (query_long(db, "SELECT count(*) FROM acct") != ROWS) e(11);
		exec(db, "COMMIT");
		n++;
	}
	sqlite3_close(db);
	if (n < 10) e(12);		/* readers were not held up */
	exit(errct);
}

static void
test_concurrent(void)
{
	sqlite3 *db;
	pid_t pids[READERS];
	time_t end;
	char sql[160];
	long moves = 0;
	int i, status, a, b, amount;

	subtest = 2;
	for (i = 0; i < READERS; i++) {
		if ((pids[i] = fork()) < 0) e(1);
		if (pids[i] == 0)
			reader();
	}

	db = open_db();
	srand48(getpid());
	end = time(NULL) + SECONDS;
	while (time(NULL) < end) {
		a = lrand48() % ROWS;
		b = lrand48() % ROWS;
		amount = lrand48() % 100;
		exec(db, "BEGIN IMMEDIATE");
		snprintf(sql, sizeof(sql),
		    "UPDATE acct SET bal = bal - %d WHERE id = %d", amount, a);
		exec(db, sql);
		snprintf(sql, sizeof(sql),
		    "UPDATE acct SET bal = bal + %d WHERE id = %d", amount, b);
		exec(db, sql);
		exec(db, "COMMIT");
		moves++;
	}

	for (i = 0; i < READERS; i++) {
		if (waitpid(pids[i], &status, 0) != pids[i]) e(2);
		if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) e(3);
	}
	if (moves < 10) e(4);

	subtest = 3;
	/* All there, consistent, and checkpointed into the database file. */
	if (query_long(db, "SELECT sum(bal) FROM acct") != TOTAL) e(1);
	{
		sqlite3_stmt *st;

		if (sqlite3_prepare_v2(db, "PRAGMA integrity_check", -1, &st,
		    NULL) != SQLITE_OK) e(2);
		if (sqlite3_step(st) != SQLITE_ROW ||
		    strcmp((const char *)sqlite3_column_text(st, 0), "ok") != 0)
			e(3);
		sqlite3_finalize(st);
	}
	exec(db, "PRAGMA wal_checkpoint(TRUNCATE)");
	sqlite3_close(db);

	/* Reopened, by now without the WAL. */
	db = open_db();
	if (query_long(db, "SELECT sum(bal) FROM acct") != TOTAL) e(4);
	sqlite3_close(db);
	(void)unlink(DB);
	(void)unlink(DB "-wal");
	(void)unlink(DB "-shm");
}

int
main(int argc, char **argv)
{

	start(131);

	setup();
	test_concurrent();

	quit();
	return 0;
}
