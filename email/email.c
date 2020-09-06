/**
 * @file
 * Representation of an email
 *
 * @authors
 * Copyright (C) 1996-2009,2012 Michael R. Elkins <me@mutt.org>
 *
 * @copyright
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 2 of the License, or (at your option) any later
 * version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * @page email_email Representation of an email
 *
 * Representation of an email
 */

#include "config.h"
#include <stdbool.h>
#include "mutt/lib.h"
#include "email.h"
#include "body.h"
#include "envelope.h"
#include "tags.h"

void nm_edata_free(void **ptr);

/**
 * email_free - Free an Email
 * @param[out] ptr Email to free
 */
void email_free(struct Email **ptr)
{
  if (!ptr || !*ptr)
    return;

  struct Email *e = *ptr;

  if (e->edata && e->edata_free)
    e->edata_free(&e->edata);

  mutt_env_free(&e->env);
  mutt_body_free(&e->body);
  FREE(&e->tree);
  FREE(&e->path);
#ifdef MIXMASTER
  mutt_list_free(&e->chain);
#endif
#ifdef USE_NOTMUCH
  nm_edata_free(&e->nm_edata);
#endif
  driver_tags_free(&e->tags);

  FREE(ptr);
}

/**
 * email_new - Create a new Email
 * @retval ptr Newly created Email
 */
struct Email *email_new(void)
{
  struct Email *e = mutt_mem_calloc(1, sizeof(struct Email));
#ifdef MIXMASTER
  STAILQ_INIT(&e->chain);
#endif
  STAILQ_INIT(&e->tags);
  e->visible = true;
  return e;
}

/**
 * email_cmp_strict - Strictly compare message emails
 * @param e1 First Email
 * @param e2 Second Email
 * @retval true Emails are strictly identical
 */
bool email_cmp_strict(const struct Email *e1, const struct Email *e2)
{
  if (e1 && e2)
  {
    if ((e1->received != e2->received) || (e1->date_sent != e2->date_sent) ||
        (e1->body->length != e2->body->length) || (e1->lines != e2->lines) ||
        (e1->zhours != e2->zhours) || (e1->zminutes != e2->zminutes) ||
        (e1->zoccident != e2->zoccident) || (e1->mime != e2->mime) ||
        !mutt_env_cmp_strict(e1->env, e2->env) || !mutt_body_cmp_strict(e1->body, e2->body))
    {
      return false;
    }
    return true;
  }
  else
  {
    return (!e1 && !e2);
  }
}

/**
 * email_size - compute the size of an email
 * @param e Email
 * @retval num Size of the email, in bytes
 */
size_t email_size(const struct Email *e)
{
  if (!e || !e->body)
    return 0;
  return e->body->length + e->body->offset - e->body->hdr_offset;
}

/**
 * emaillist_clear - Drop a private list of Emails
 * @param el EmailList to empty
 *
 * The Emails are not freed.
 */
void emaillist_clear(struct EmailList *el)
{
  if (!el)
    return;

  struct EmailNode *en = NULL, *tmp = NULL;
  STAILQ_FOREACH_SAFE(en, el, entries, tmp)
  {
    STAILQ_REMOVE(el, en, EmailNode, entries);
    FREE(&en);
  }
  STAILQ_INIT(el);
}

/**
 * emaillist_add_email - Add an Email to a list
 * @param e  Email to add
 * @param el EmailList to add to
 * @retval  0 Success
 * @retval -1 Error
 */
int emaillist_add_email(struct EmailList *el, struct Email *e)
{
  if (!el || !e)
    return -1;

  struct EmailNode *en = mutt_mem_calloc(1, sizeof(*en));
  en->email = e;
  STAILQ_INSERT_TAIL(el, en, entries);

  return 0;
}

/**
 * find_header - Find a header, matching on its field, in a list of headers
 * @param hdrlist List of headers to search
 * @param header  The header to search for. Either of the form "X-Header:" or "X-Header: value"
 * @retval node   The node in the list matching the header
 * @retval NULL   If no matching header is found
 */
struct ListNode *find_header(const struct ListHead *hdrlist, const char *header) {
  struct ListNode *n = NULL;

  const int keylen = strchr(header, ':') - header + 1;

  STAILQ_FOREACH(n, hdrlist, entries) {
    if (mutt_istrn_equal(n->data, header, keylen))
      return n;
  }
  return n;
}

/**
 * add_header - Add a header to a list
 * @param hdrlist List of headers to search
 * @param buf     Buffer whose data to use to set the value of the header
 * @retval node   The created header
 */
struct ListNode *add_header(struct ListHead *hdrlist, const struct Buffer *buf)
{
  struct ListNode *n = mutt_list_insert_tail(hdrlist, NULL);
  n->data = mutt_buffer_strdup(buf);

  return n;
}

/**
 * update_header - Update an existing header
 * @param hdr     The header to update
 * @param buf     Buffer whose data to use to update the value of the header
 * @retval node   The updated header
 */
struct ListNode *update_header(struct ListNode *hdr, const struct Buffer *buf)
{
  FREE(&hdr->data);
  hdr->data = mutt_buffer_strdup(buf);

  return hdr;
}

/**
 * set_header - Set a header value in a list. If a header exists with the same field, update it, otherwise add a new header
 * @param hdrlist List of headers to search
 * @param buf     Buffer whose data to use to set the value of the header
 * @retval node   The updated or created header
 */
struct ListNode *set_header(struct ListHead *hdrlist, const struct Buffer *buf)
{
  struct ListNode *n = find_header(hdrlist, buf->data);

  return n == NULL ? add_header(hdrlist, buf) : update_header(n, buf);
}
