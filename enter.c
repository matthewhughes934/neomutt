/*
 * Copyright (C) 1996-2000 Michael R. Elkins <me@cs.hmc.edu>
 * Copyright (C) 2000 Edmund Grimley Evans <edmundo@rano.org>
 * 
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 * 
 *     This program is distributed in the hope that it will be useful,
 *     but WITHOUT ANY WARRANTY; without even the implied warranty of
 *     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *     GNU General Public License for more details.
 * 
 *     You should have received a copy of the GNU General Public License
 *     along with this program; if not, write to the Free Software
 *     Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111, USA.
 */ 

#include "mutt.h"
#include "mutt_menu.h"
#include "mutt_curses.h"
#include "keymap.h"
#include "history.h"

#include <string.h>

/* redraw flags for mutt_enter_string() */
enum
{
  M_REDRAW_INIT = 1,	/* go to end of line and redraw */
  M_REDRAW_LINE		/* redraw entire line */
};

/* FIXME: these functions should deal with unprintable characters */

static int my_wcwidth (wchar_t wc)
{
  return wcwidth (wc);
}

static int my_wcswidth (const wchar_t *s, size_t n)
{
  int w = 0;
  while (n--)
    w += my_wcwidth (*s++);
  return w;
}

static int my_addwch (wchar_t wc)
{
  return mutt_addwch (wc);
}

static size_t width_ceiling (const wchar_t *s, size_t n, int w1)
{
  const wchar_t *s0 = s;
  int w = 0;
  for (; n; s++, n--)
    if ((w += my_wcwidth (*s)) > w1)
      break;
  return s - s0;  
}

void my_wcstombs(char *dest, size_t dlen, const wchar_t *src, size_t slen)
{
  mbstate_t st;
  size_t  k;

  memset (&st, 0, sizeof (st));
  for (; slen && dlen >= 2 * MB_LEN_MAX; dest += k, dlen -= k, src++, slen--)
    if ((k = wcrtomb (dest, *src, &st)) == (size_t)(-1))
      break;
  wcrtomb (dest, 0, &st); /* FIXME */
}

size_t my_mbstowcs (wchar_t **pwbuf, size_t *pwbuflen, size_t i, char *buf)
{
  wchar_t wc;
  mbstate_t st;
  size_t k;
  wchar_t *wbuf;
  size_t wbuflen;

  wbuf = *pwbuf, wbuflen = *pwbuflen;
  memset (&st, 0, sizeof (st));
  for (; (k = mbrtowc (&wc, buf, MB_LEN_MAX, &st)) &&
	 k != (size_t)(-1) && k != (size_t)(-2); buf += k)
  {
    if (i >= wbuflen)
    {
      wbuflen = i + 20;
      safe_realloc ((void **) &wbuf, wbuflen * sizeof (*wbuf));
    }
    wbuf[i++] = wc;
  }
  *pwbuf = wbuf, *pwbuflen = wbuflen;
  return i;
}

/*
 * Replace part of the wchar_t buffer, from FROM to TO, by BUF.
 */

static void replace_part (wchar_t **pwbuf, size_t *pwbuflen,
			  size_t from, size_t *to, size_t *end, char *buf)
{
  /* Save the suffix */
  size_t savelen = *end - *to;
  wchar_t *savebuf = safe_malloc (savelen * sizeof (wchar_t));
  memcpy (savebuf, *pwbuf + *to, savelen * sizeof (wchar_t));

  /* Convert to wide characters */
  *to = my_mbstowcs (pwbuf, pwbuflen, from, buf);

  /* Make space for suffix */
  if (*to + savelen > *pwbuflen)
  {
    *pwbuflen = *to + savelen;
    safe_realloc ((void **) pwbuf, *pwbuflen * sizeof (wchar_t));
  }

  /* Restore suffix */
  memcpy (*pwbuf + *to, savebuf, savelen * sizeof (wchar_t));
  *end = *to + savelen;

  free (savebuf);
}

/*
 * Returns:
 *	1 need to redraw the screen and call me again
 *	0 if input was given
 * 	-1 if abort.
 */

int  mutt_enter_string(char *buf, size_t buflen, int y, int x, int flags)
{
  int rv;
  ENTER_STATE *es = mutt_new_enter_state ();
  rv = _mutt_enter_string (buf, buflen, y, x, flags, 0, NULL, NULL, es);
  mutt_free_enter_state (&es);
  return rv;
}

int _mutt_enter_string (char *buf, size_t buflen, int y, int x,
			int flags, int multiple, char ***files, int *numfiles,
			ENTER_STATE *state)
{
  int width = COLS - x - 1;
  int redraw;
  int pass = (flags & M_PASS);
  int first = 1;
  int tabs = 0; /* number of *consecutive* TABs */
  int ch, w, r;
  size_t i;
  wchar_t *tempbuf = 0;
  size_t templen = 0;
  history_class_t hclass;

  int rv = 0;
  
  if (state->wbuf)
  {
    /* Coming back after return 1 */
    redraw = M_REDRAW_LINE;
  }
  else
  {
    /* Initialise wbuf from buf */
    state->wbuflen = 0;
    state->wbufn = my_mbstowcs (&state->wbuf, &state->wbuflen, 0, buf);
    redraw = M_REDRAW_INIT;
  }

  if (flags & (M_FILE | M_EFILE))
    hclass = HC_FILE;
  else if (flags & M_CMD)
    hclass = HC_CMD;
  else if (flags & M_ALIAS)
    hclass = HC_ALIAS;
  else if (flags & M_COMMAND)
    hclass = HC_COMMAND;
  else if (flags & M_PATTERN)
    hclass = HC_PATTERN;
  else 
    hclass = HC_OTHER;
    
  for (;;)
  {
    if (redraw && !pass)
    {
      if (redraw == M_REDRAW_INIT)
      {
	/* Go to end of line */
	state->curpos = state->wbufn;
	state->begin = width_ceiling (state->wbuf, state->wbufn, my_wcswidth (state->wbuf, state->wbufn) - width + 1);
      } 
      if (state->curpos < state->begin ||
	  my_wcswidth (state->wbuf + state->begin, state->curpos - state->begin) >= width)
	state->begin = width_ceiling (state->wbuf, state->wbufn, my_wcswidth (state->wbuf, state->curpos) - width / 2);
      move (y, x);
      w = 0;
      for (i = state->begin; i < state->wbufn; i++)
      {
	w += my_wcwidth (state->wbuf[i]);
	if (w > width)
	  break;
	my_addwch (state->wbuf[i]);
      }
      clrtoeol ();
      move (y, x + my_wcswidth (state->wbuf + state->begin, state->curpos - state->begin));
    }
    mutt_refresh ();

    if ((ch = km_dokey (MENU_EDITOR)) == -1)
    {
      rv = -1; 
      goto bye;
    }

    if (ch != OP_NULL)
    {
      first = 0;
      if (ch != OP_EDITOR_COMPLETE)
	tabs = 0;
      redraw = M_REDRAW_LINE;
      switch (ch)
      {
	case OP_EDITOR_HISTORY_UP:
	  state->curpos = state->wbufn;
	  replace_part (&state->wbuf, &state->wbuflen, 0, &state->curpos, &state->wbufn,
			mutt_history_prev (hclass));
	  redraw = M_REDRAW_INIT;
	  break;

	case OP_EDITOR_HISTORY_DOWN:
	  state->curpos = state->wbufn;
	  replace_part (&state->wbuf, &state->wbuflen, 0, &state->curpos, &state->wbufn,
			mutt_history_prev (hclass));
	  redraw = M_REDRAW_INIT;
	  break;

	case OP_EDITOR_BACKSPACE:
	  if (state->curpos == 0)
	    BEEP ();
	  else
	  {
	    i = state->curpos;
	    while (i && !wcwidth (state->wbuf[i - 1]))
	      --i;
	    if (i)
	      --i;
	    memmove(state->wbuf + i, state->wbuf + state->curpos, (state->wbufn - state->curpos) * sizeof (*state->wbuf));
	    state->wbufn -= state->curpos - i;
	    state->curpos = i;
	  }
	  break;

	case OP_EDITOR_BOL:
	  state->curpos = 0;
	  break;

	case OP_EDITOR_EOL:
	  redraw= M_REDRAW_INIT;
	  break;

	case OP_EDITOR_KILL_LINE:
	  state->curpos = state->wbufn = 0;
	  break;

	case OP_EDITOR_KILL_EOL:
	  state->wbufn = state->curpos;
	  break;

	case OP_EDITOR_BACKWARD_CHAR:
	  if (state->curpos == 0)
	    BEEP ();
	  else
	  {
	    while (state->curpos && !wcwidth (state->wbuf[state->curpos - 1]))
	      state->curpos--;
	    if (state->curpos)
	      state->curpos--;
	  }
	  break;

	case OP_EDITOR_FORWARD_CHAR:
	  if (state->curpos == state->wbufn)
	    BEEP ();
	  else
	  {
	    ++state->curpos;
	    while (state->curpos < state->wbufn && !wcwidth (state->wbuf[state->curpos]))
	      ++state->curpos;
	  }
	  break;

	case OP_EDITOR_BACKWARD_WORD:
	  if (state->curpos == 0)
	    BEEP ();
	  else
	  {
	    while (state->curpos && iswspace (state->wbuf[state->curpos - 1]))
	      state->curpos--;
	    while (state->curpos && !iswspace (state->wbuf[state->curpos - 1]))
	      state->curpos--;
	  }
	  break;

	case OP_EDITOR_FORWARD_WORD:
	  if (state->curpos == state->wbufn)
	    BEEP ();
	  else
	  {
	    while (state->curpos < state->wbufn && iswspace (state->wbuf[state->curpos]))
	      ++state->curpos;
	    while (state->curpos < state->wbufn && !iswspace (state->wbuf[state->curpos]))
	      ++state->curpos;
	  }
	  break;

	case OP_EDITOR_CAPITALIZE_WORD:
	case OP_EDITOR_UPCASE_WORD:
	case OP_EDITOR_DOWNCASE_WORD:
	  if (state->curpos == state->wbufn)
	  {
	    BEEP ();
	    break;
	  }
	  while (state->curpos && !iswspace (state->wbuf[state->curpos]))
	    state->curpos--;
	  while (state->curpos < state->wbufn && iswspace (state->wbuf[state->curpos]))
	    state->curpos--;
	  while (state->curpos < state->wbufn && !iswspace (state->wbuf[state->curpos]))
	  {
	    if (ch == OP_EDITOR_DOWNCASE_WORD)
	      state->wbuf[state->curpos] = towlower (state->wbuf[state->curpos]);
	    else
	    {
	      state->wbuf[state->curpos] = towupper (state->wbuf[state->curpos]);
	      if (ch == OP_EDITOR_CAPITALIZE_WORD)
		ch = OP_EDITOR_DOWNCASE_WORD;
	    }
	    state->curpos++;
	  }
	  break;

	case OP_EDITOR_DELETE_CHAR:
	  if (state->curpos == state->wbufn)
	    BEEP ();
	  else
	  {
	    i = state->curpos;
	    while (i < state->wbufn && !wcwidth (state->wbuf[i]))
	      ++i;
	    if (i < state->wbufn)
	      ++i;
	    while (i < state->wbufn && !wcwidth (state->wbuf[i]))
	      ++i;
	    memmove(state->wbuf + state->curpos, state->wbuf + i, (state->wbufn - i) * sizeof (*state->wbuf));
	    state->wbufn -= i - state->curpos;
	  }
	  break;

	case OP_EDITOR_BUFFY_CYCLE:
	  if (flags & M_EFILE)
	  {
	    first = 1; /* clear input if user types a real key later */
	    my_wcstombs (buf, buflen, state->wbuf, state->curpos);
	    mutt_buffy (buf);
	    state->curpos = state->wbufn = my_mbstowcs (&state->wbuf, &state->wbuflen, 0, buf);
	    break;
	  }
	  else if (!(flags & M_FILE))
	    goto self_insert;
	  /* fall through to completion routine (M_FILE) */

	case OP_EDITOR_COMPLETE:
	  tabs++;
	  if (flags & M_CMD)
	  {
	    for (i = state->curpos; i && state->wbuf[i-1] != ' '; i--)
	      ;
	    my_wcstombs (buf, buflen, state->wbuf + i, state->curpos - i);
	    if (tempbuf && templen == state->wbufn - i &&
		!memcmp (tempbuf, state->wbuf + i, (state->wbufn - i) * sizeof (*state->wbuf)))
	    {
	      mutt_select_file (buf, buflen, 0);
	      set_option (OPTNEEDREDRAW);
	      if (*buf)
		replace_part (&state->wbuf, &state->wbuflen, i, &state->curpos, &state->wbufn, buf);
	      rv = 1; 
	      goto bye;
	    }
	    if (!mutt_complete (buf, buflen))
	    {
	      templen = state->wbufn - i;
	      safe_realloc ((void **) &tempbuf, templen * sizeof (*state->wbuf));
	    }
	    else
	      BEEP ();

	    replace_part (&state->wbuf, &state->wbuflen, i, &state->curpos, &state->wbufn, buf);
	  }
	  else if (flags & M_ALIAS)
	  {
	    /* invoke the alias-menu to get more addresses */
	    for (i = state->curpos; i && state->wbuf[i-1] != ','; i--)
	      ;
	    for (; i < state->wbufn && state->wbuf[i] == ' '; i++)
	      ;
	    my_wcstombs (buf, buflen, state->wbuf + i, state->curpos - i);
	    r = mutt_alias_complete (buf, buflen);
	    replace_part (&state->wbuf, &state->wbuflen, i, &state->curpos, &state->wbufn, buf);
	    if (!r)
	    {
	      rv = 1;
	      goto bye;
	    }
	    break;
	  }
	  else if (flags & M_COMMAND)
	  {
	    my_wcstombs (buf, buflen, state->wbuf, state->curpos);
	    i = strlen (buf);
	    if (buf[i - 1] == '=' &&
		mutt_var_value_complete (buf, buflen, i))
	      tabs = 0;
	    else if (!mutt_command_complete (buf, buflen, i, tabs))
	      BEEP ();
	    replace_part (&state->wbuf, &state->wbuflen, 0, &state->curpos, &state->wbufn, buf);
	  }
	  else if (flags & (M_FILE | M_EFILE))
	  {
	    my_wcstombs (buf, buflen, state->wbuf, state->curpos);

	    /* see if the path has changed from the last time */
	    if (tempbuf && templen == state->wbufn &&
		!memcmp (tempbuf, state->wbuf, state->wbufn * sizeof (*state->wbuf)))
	    {
	      _mutt_select_file (buf, buflen, 0, multiple, files, numfiles);
	      set_option (OPTNEEDREDRAW);
	      if (*buf)
	      {
		mutt_pretty_mailbox (buf);
		if (!pass)
		  mutt_history_add (hclass, buf);
		rv = 0;
		goto bye;
	      }

	      /* file selection cancelled */
	      rv = 1;
	      goto bye;
	    }

	    if (!mutt_complete (buf, buflen))
	    {
	      templen = state->wbufn;
	      safe_realloc ((void **) &tempbuf, templen * sizeof (*state->wbuf));
	      memcpy (tempbuf, state->wbuf, templen * sizeof (*state->wbuf));
	    }
	    else
	      BEEP (); /* let the user know that nothing matched */
	    replace_part (&state->wbuf, &state->wbuflen, 0, &state->curpos, &state->wbufn, buf);
	  }
	  else
	    goto self_insert;
	  break;

	case OP_EDITOR_COMPLETE_QUERY:
	  if (flags & M_ALIAS)
	  {
	    /* invoke the query-menu to get more addresses */
	    if (state->curpos)
	    {
	      for (i = state->curpos; i && buf[i - 1] != ','; i--)
		;
	      for (; i < state->curpos && buf[i] == ' '; i++)
		;
	      my_wcstombs (buf, buflen, state->wbuf + i, state->curpos - i);
	      mutt_query_complete (buf, buflen);
	      replace_part (&state->wbuf, &state->wbuflen, i, &state->curpos, &state->wbufn, buf);
	    }
	    else
	    {
	      my_wcstombs (buf, buflen, state->wbuf, state->curpos);
	      mutt_query_menu (buf, buflen);
	      replace_part (&state->wbuf, &state->wbuflen, 0, &state->curpos, &state->wbufn, buf);
	    }
	    rv = 1; 
	    goto bye;
	  }
	  else
	    goto self_insert;

	case OP_EDITOR_QUOTE_CHAR:
	  {
	    event_t event;
	    /*ADDCH (LastKey);*/
	    event = mutt_getch ();
	    if (event.ch != -1)
	    {
	      LastKey = event.ch;
	      goto self_insert;
	    }
	  }

	default:
	  BEEP ();
      }
    }
    else
    {
      
self_insert:

      tabs = 0;
      /* use the raw keypress */
      ch = LastKey;

      if (first && (flags & M_CLEAR))
      {
	first = 0;
	if (IsWPrint (ch)) /* why? */
	  state->curpos = state->wbufn = 0;
      }

      if (CI_is_return (ch))
      {
	/* Convert from wide characters */
	my_wcstombs (buf, buflen, state->wbuf, state->wbufn);
	if (!pass)
	  mutt_history_add (hclass, buf);

	if (multiple)
	{
	  char **tfiles;
	  *numfiles = 1;
	  tfiles = safe_malloc (*numfiles * sizeof (char *));
	  mutt_expand_path (buf, buflen);
	  tfiles[0] = safe_strdup (buf);
	  *files = tfiles;
	}
	rv = 0; 
	goto bye;
      }
      else if ((ch < ' ' || IsWPrint (ch))) /* why? */
      {
	if (state->wbufn >= state->wbuflen)
	{
	  state->wbuflen = state->wbufn + 20;
	  safe_realloc ((void **) &state->wbuf, state->wbuflen * sizeof (*state->wbuf));
	}
	memmove (state->wbuf + state->curpos + 1, state->wbuf + state->curpos, (state->wbufn - state->curpos) * sizeof (*state->wbuf));
	state->wbuf[state->curpos++] = ch;
	state->wbufn++;
      }
      else
      {
	mutt_flushinp ();
	BEEP ();
      }
    }
  }
  
  bye:
  
  safe_free ((void **) &tempbuf);
  return rv;
}

void mutt_free_enter_state (ENTER_STATE **esp)
{
  if (!esp) return;
  
  safe_free ((void **) &(*esp)->wbuf);
  safe_free ((void **) esp);
}

/*
 * TODO:
 * very narrow screen might crash it
 * sort out the input side
 * unprintable chars
 * config tests for iswspace, towupper, towlower
 * OP_EDITOR_KILL_WORD
 * OP_EDITOR_KILL_EOW
 * OP_EDITOR_TRANSPOSE_CHARS
 */
