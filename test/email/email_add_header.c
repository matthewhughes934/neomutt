#define TEST_NO_MAIN
#include "config.h"
#include "acutest.h"
#include <string.h>
#include "mutt/lib.h"
#include "email/lib.h"

void test_email_add_header(void)
{
  // struct ListNode *add_header(struct ListHead *hdrlist, const struct Buffer *buf)
  const char *header = "X-TestHeader: 123";

  struct ListHead hdrlist = STAILQ_HEAD_INITIALIZER(hdrlist);
  struct Buffer buf = { 0 };
  mutt_buffer_strcpy(&buf, header);

  {
    struct ListNode *n = add_header(&hdrlist, &buf);
    TEST_CHECK(strcmp(n->data, buf.data) == 0);       /* header stored in node */
    TEST_CHECK(n == find_header(&hdrlist, buf.data)); /* node added to list */
  }
}
