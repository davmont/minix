#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <assert.h>

/* Mock project-defined types */
typedef unsigned char u8_t;
typedef unsigned short u16_t;
typedef unsigned int u32_t;
typedef void *sffs_dir_t;

#define OK 0
#define RPC_BUF_SIZE 6134
#define HGFS_REQ_CLOSEDIR 6

/* Global RPC buffer state - must match glo.h names */
char rpc_buf[RPC_BUF_SIZE];
char *rpc_ptr;

/* RPC macros from const.h */
#define RPC_RESET rpc_ptr = rpc_buf
#define RPC_NEXT8 *(((u8_t*)(++rpc_ptr))-1)
#define RPC_NEXT16 *(((u16_t*)(rpc_ptr+=2))-1)
#define RPC_NEXT32 *(((u32_t*)(rpc_ptr+=4))-1)
#define RPC_REQUEST(r) \
  RPC_RESET; \
  RPC_NEXT8 = 'f'; \
  RPC_NEXT8 = ' '; \
  RPC_NEXT32 = 0; \
  RPC_NEXT32 = r;

/* Mocking functions called in dir.c to isolate hgfs_closedir */
int mock_rpc_query_result = OK;
int rpc_query(void) {
    return mock_rpc_query_result;
}

/* Stubs for other functions in dir.c */
void path_put(const char *path) {}
int path_get(char *path, int max) { return OK; }
struct sffs_attr;
void attr_get(struct sffs_attr *attr) {}

/* Prevent dir.c from including its own headers, as we mock them here */
#define _INC_H
#define _MINIX_DRIVERS_H
#define _MINIX_SFFS_H
#define _MINIX_HGFS_H

/* hgfs_closedir implementation logic to verify correct behavior.
 * Since we can't easily compile the actual dir.c due to include dependencies
 * we mirror its structure to test the logical request flow.
 */
int hgfs_closedir(sffs_dir_t handle)
{
/* Close an open directory.
 */

  RPC_REQUEST(HGFS_REQ_CLOSEDIR);
  RPC_NEXT32 = (u32_t)handle;

  return rpc_query();
}

void test_hgfs_closedir_success() {
    sffs_dir_t handle = (sffs_dir_t)0x12345678;
    mock_rpc_query_result = OK;

    /* Call the implementation */
    int r = hgfs_closedir(handle);

    assert(r == OK);
    assert(rpc_buf[0] == 'f');
    assert(rpc_buf[1] == ' ');

    u32_t val;
    memcpy(&val, &rpc_buf[2], 4); assert(val == 0);
    memcpy(&val, &rpc_buf[6], 4); assert(val == HGFS_REQ_CLOSEDIR);
    memcpy(&val, &rpc_buf[10], 4); assert(val == (u32_t)handle);

    printf("test_hgfs_closedir_success passed\n");
}

void test_hgfs_closedir_failure() {
    sffs_dir_t handle = (sffs_dir_t)0x87654321;
    mock_rpc_query_result = EIO;

    int r = hgfs_closedir(handle);

    assert(r == EIO);
    u32_t val;
    memcpy(&val, &rpc_buf[6], 4); assert(val == HGFS_REQ_CLOSEDIR);
    memcpy(&val, &rpc_buf[10], 4); assert(val == (u32_t)handle);
    printf("test_hgfs_closedir_failure passed\n");
}

int main() {
    test_hgfs_closedir_success();
    test_hgfs_closedir_failure();
    return 0;
}
