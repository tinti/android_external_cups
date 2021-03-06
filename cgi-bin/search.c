/*
 * "$Id: search.c 9793 2011-05-20 03:49:49Z mike $"
 *
 *   Search routines for CUPS.
 *
 *   Copyright 2007-2011 by Apple Inc.
 *   Copyright 1997-2006 by Easy Software Products.
 *
 *   These coded instructions, statements, and computer programs are the
 *   property of Apple Inc. and are protected by Federal copyright
 *   law.  Distribution and use rights are outlined in the file "LICENSE.txt"
 *   which should have been included with this file.  If this file is
 *   file is missing or damaged, see the license at "http://www.cups.org/".
 *
 * Contents:
 *
 *   cgiCompileSearch() - Compile a search string.
 *   cgiDoSearch()      - Do a search of some text.
 *   cgiFreeSearch()    - Free a compiled search context.
 */

/*
 * Include necessary headers...
 */

#include "cgi-private.h"
#include <regex.h>


/*
 * 'cgiCompileSearch()' - Compile a search string.
 */

void *					/* O - Search context */
cgiCompileSearch(const char *query)	/* I - Query string */
{
  regex_t	*re;			/* Regular expression */
  char		*s,			/* Regular expression string */
		*sptr,			/* Pointer into RE string */
		*sword;			/* Pointer to start of word */
  int		slen;			/* Allocated size of RE string */
  const char	*qptr,			/* Pointer into query string */
		*qend;			/* End of current word */
  const char	*prefix;		/* Prefix to add to next word */
  int		quoted;			/* Word is quoted */
  int		wlen;			/* Word length */
  char		*lword;			/* Last word in query */


  DEBUG_printf(("cgiCompileSearch(query=\"%s\")\n", query));

 /*
  * Range check input...
  */

  if (!query)
    return (NULL);

 /*
  * Allocate a regular expression storage structure...
  */

  if ((re = (regex_t *)calloc(1, sizeof(regex_t))) == NULL)
    return (NULL);

 /*
  * Allocate a buffer to hold the regular expression string, starting
  * at 1024 bytes or 3 times the length of the query string, whichever
  * is greater.  We'll expand the string as needed...
  */

  slen = strlen(query) * 3;
  if (slen < 1024)
    slen = 1024;

  if ((s = (char *)malloc(slen)) == NULL)
  {
    free(re);
    return (NULL);
  }

 /*
  * Copy the query string to the regular expression, handling basic
  * AND and OR logic...
  */

  prefix = ".*";
  qptr   = query;
  sptr   = s;
  lword  = NULL;

  while (*qptr)
  {
   /*
    * Skip leading whitespace...
    */

    while (isspace(*qptr & 255))
      qptr ++;

    if (!*qptr)
      break;

   /*
    * Find the end of the current word...
    */

    if (*qptr == '\"' || *qptr == '\'')
    {
     /*
      * Scan quoted string...
      */

      quoted = *qptr ++;
      for (qend = qptr; *qend && *qend != quoted; qend ++);

      if (!*qend)
      {
       /*
        * No closing quote, error out!
	*/

	free(s);
	free(re);

	if (lword)
          free(lword);

	return (NULL);
      }
    }
    else
    {
     /*
      * Scan whitespace-delimited string...
      */

      quoted = 0;
      for (qend = qptr + 1; *qend && !isspace(*qend); qend ++);
    }

    wlen = qend - qptr;

   /*
    * Look for logic words: AND, OR
    */

    if (wlen == 3 && !_cups_strncasecmp(qptr, "AND", 3))
    {
     /*
      * Logical AND with the following text...
      */

      if (sptr > s)
        prefix = ".*";

      qptr = qend;
    }
    else if (wlen == 2 && !_cups_strncasecmp(qptr, "OR", 2))
    {
     /*
      * Logical OR with the following text...
      */

      if (sptr > s)
        prefix = ".*|.*";

      qptr = qend;
    }
    else
    {
     /*
      * Add a search word, making sure we have enough room for the
      * string + RE overhead...
      */

      wlen = (sptr - s) + 2 * 4 * wlen + 2 * strlen(prefix) + 11;
      if (lword)
        wlen += strlen(lword);

      if (wlen > slen)
      {
       /*
        * Expand the RE string buffer...
	*/

        char *temp;			/* Temporary string pointer */


	slen = wlen + 128;
        temp = (char *)realloc(s, slen);
	if (!temp)
	{
	  free(s);
	  free(re);

	  if (lword)
            free(lword);

	  return (NULL);
	}

        sptr = temp + (sptr - s);
	s    = temp;
      }

     /*
      * Add the prefix string...
      */

      strcpy(sptr, prefix);
      sptr += strlen(sptr);

     /*
      * Then quote the remaining word characters as needed for the
      * RE...
      */

      sword = sptr;

      while (qptr < qend)
      {
       /*
        * Quote: ^ . [ $ ( ) | * + ? { \
	*/

        if (strchr("^.[$()|*+?{\\", *qptr))
	  *sptr++ = '\\';

	*sptr++ = *qptr++;
      }

      *sptr = '\0';

     /*
      * For "word1 AND word2", add reciprocal "word2 AND word1"...
      */

      if (!strcmp(prefix, ".*") && lword)
      {
        char *lword2;			/* New "last word" */


        if ((lword2 = strdup(sword)) == NULL)
	{
	  free(lword);
	  free(s);
	  free(re);
	  return (NULL);
	}

        strcpy(sptr, ".*|.*");
	sptr += 5;

	strcpy(sptr, lword2);
	sptr += strlen(sptr);

        strcpy(sptr, ".*");
	sptr += 2;

	strcpy(sptr, lword);
	sptr += strlen(sptr);

        free(lword);
	lword = lword2;
      }
      else
      {
	if (lword)
          free(lword);

	lword = strdup(sword);
      }

      prefix = ".*|.*";
    }

   /*
    * Advance to the next string...
    */

    if (quoted)
      qptr ++;
  }

  if (lword)
    free(lword);

  if (sptr > s)
    strcpy(sptr, ".*");
  else
  {
   /*
    * No query data, return NULL...
    */

    free(s);
    free(re);

    return (NULL);
  }

 /*
  * Compile the regular expression...
  */

  DEBUG_printf(("    s=\"%s\"\n", s));

  if (regcomp(re, s, REG_EXTENDED | REG_ICASE))
  {
    free(re);
    free(s);

    return (NULL);
  }

 /*
  * Free the RE string and return the new regular expression we compiled...
  */

  free(s);

  return ((void *)re);
}


/*
 * 'cgiDoSearch()' - Do a search of some text.
 */

int					/* O - Number of matches */
cgiDoSearch(void       *search,		/* I - Search context */
            const char *text)		/* I - Text to search */
{
  int		i;			/* Looping var */
  regmatch_t	matches[100];		/* RE matches */


 /*
  * Range check...
  */

  if (!search || !text)
    return (0);

 /*
  * Do a lookup...
  */

  if (!regexec((regex_t *)search, text, sizeof(matches) / sizeof(matches[0]),
               matches, 0))
  {
   /*
    * Figure out the number of matches in the string...
    */

    for (i = 0; i < (int)(sizeof(matches) / sizeof(matches[0])); i ++)
      if (matches[i].rm_so < 0)
	break;

    return (i);
  }
  else
    return (0);
}


/*
 * 'cgiFreeSearch()' - Free a compiled search context.
 */

void
cgiFreeSearch(void *search)		/* I - Search context */
{
  regfree((regex_t *)search);
}


/*
 * End of "$Id: search.c 9793 2011-05-20 03:49:49Z mike $".
 */
