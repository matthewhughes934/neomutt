#define TEST_NO_MAIN
#include "config.h"
#include "acutest.h"
#include "mutt/lib.h"
#include "email/lib.h"

void test_email_find_header(void)
{
  // struct ListNode *find_header(struct ListHead *hdrlist, const char *header)
  char *header = "X-TestHeader: 123";

  struct ListHead hdrlist = STAILQ_HEAD_INITIALIZER(hdrlist);
  struct ListNode *n = mutt_mem_calloc(1, sizeof(struct ListNode));
  n->data = header;
  STAILQ_INSERT_TAIL(&hdrlist, n, entries);

  {
    struct ListNode *found = find_header(&hdrlist, header);
    TEST_CHECK(found == n);
  }

  {
    const char *not_found = "X-NotIncluded: foo";
    struct ListNode *found = find_header(&hdrlist, not_found);
    TEST_CHECK(found == NULL);
  }

  {
    const char *field_only = "X-TestHeader:";
    TEST_CHECK(find_header(&hdrlist, field_only) == n);
  }

  {
    const char *invalid_header = "Not a header";
    TEST_CHECK(find_header(&hdrlist, invalid_header) == NULL);
  }
}
