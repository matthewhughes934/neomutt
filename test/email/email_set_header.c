#define TEST_NO_MAIN
#include "config.h"
#include "acutest.h"
#include <string.h>
#include "mutt/lib.h"
#include "email/lib.h"

void test_email_set_header(void)
{
  // struct ListNode *set_header(struct ListHead *hdrlist, const struct Buffer *buf)
  const char *starting_value = "X-TestHeader: 0.57";
  const char *updated_value = "X-TestHeader: 6.28";

  struct ListHead hdrlist = STAILQ_HEAD_INITIALIZER(hdrlist);
  struct Buffer starting_buf = { 0 };
  mutt_buffer_strcpy(&starting_buf, starting_value);
  struct Buffer updated_buf = { 0 };
  mutt_buffer_strcpy(&updated_buf, updated_value);

  {
    /* Set value for first time */
    struct ListNode *got = set_header(&hdrlist, &starting_buf);
    TEST_CHECK(strcmp(got->data, starting_buf.data) == 0); /* value set */
    TEST_CHECK(got == STAILQ_FIRST(&hdrlist)); /* header was added to list */
  }

  {
    /* Update value */
    struct ListNode *got = set_header(&hdrlist, &updated_buf);
    TEST_CHECK(strcmp(got->data, updated_buf.data) == 0); /* value set*/
    TEST_CHECK(got == STAILQ_FIRST(&hdrlist)); /* no new header added*/
  }
}
