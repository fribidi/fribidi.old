/* FriBidi - Library of BiDi algorithm
 * Copyright (C) 1999 Dov Grobgeld
 * Copyright (C) 2001 Behdad Esfahbod
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */
#define DEBUG

#include <glib.h>
#include "fribidi.h"
#ifdef DEBUG
#include <stdio.h>
#endif

#ifdef DEBUG
#define DBG(s) if (fribidi_debug) { fprintf(stderr, s); }
#else
#define DBG(s)
#endif

#ifdef DEBUG
// for easier test with the reference code only.
#define MAX_LEVEL 15
#else
// default value.
#define MAX_LEVEL 61
#endif
/*======================================================================
// Typedef for the run-length list.
//----------------------------------------------------------------------*/
typedef struct _TypeLink TypeLink;

struct _TypeLink {
  TypeLink *prev;
  TypeLink *next;
  FriBidiCharType type;
  gint pos;
  gint len;
  gint level;
};

typedef struct {
  FriBidiChar key;
  FriBidiChar value;
} key_value_t;

typedef struct {
  FriBidiCharType override; /* only L, R and N are valid */
  gint level;
} level_info;

#ifdef DEBUG
static gboolean fribidi_debug = FALSE;
#endif

int fribidi_set_debug(gboolean debug)
{
#ifdef DEBUG
  return fribidi_debug = debug;
#else
  return 0;
#endif
}

static gint bidi_string_strlen(FriBidiChar *str)
{
  gint len = 0;

  while(*str++)
    len++;
  
  return len;
}

static void bidi_string_reverse(FriBidiChar *str, gint len)
{
  gint i;
  for (i=0; i<len/2; i++)
    {
      FriBidiChar tmp = str[i];
      str[i] = str[len-1-i];
      str[len-1-i] = tmp;
    }
}

static void
int16_array_reverse(guint16 *arr, gint len)
{
  gint i;
  for (i=0; i<len/2; i++)
    {
      guint16 tmp = arr[i];
      arr[i] = arr[len-1-i];
      arr[len-1-i] = tmp;
    }
}

#ifndef USE_SIMPLE_MALLOC
static TypeLink *free_type_links = NULL;
#endif

static TypeLink *new_type_link(void)
{
  TypeLink *link;
  
#ifdef USE_SIMPLE_MALLOC
  link = g_malloc(sizeof(TypeLink));
#else
  if (free_type_links)
    {
      link = free_type_links;
      free_type_links = free_type_links->next;
    }
  else
    {
      static GMemChunk *mem_chunk = NULL;

      if (!mem_chunk)
       mem_chunk = g_mem_chunk_new ("TypeLinkList",
                                    sizeof (TypeLink),
                                    sizeof (TypeLink) * 128,
                                    G_ALLOC_ONLY);

      link = g_chunk_new (TypeLink, mem_chunk);
    }
#endif
  
  link->len = 0;
  link->pos = 0;
  link->level = 0;
  link->next = NULL;
  link->prev = NULL;
  return link;
}

static void free_type_link(TypeLink *link)
{
#ifdef USE_SIMPLE_MALLOC
  g_free(link);
#else
  link->next = free_type_links;
  free_type_links = link;
#endif
}

static TypeLink *run_length_encode_types(gint *char_type, gint type_len)
{
  TypeLink *list = NULL;
  TypeLink *last;
  TypeLink *link;
  FriBidiCharType type;
  gint len, pos, i;

  /* Add the starting link */
  list = new_type_link();
  list->type = FRIBIDI_TYPE_SOT;
  list->level = -1;
  list->len = 0;
  list->pos = 0;
  last = list;

  /* Sweep over the string_types */
  type = -1;
  len = 0;
  pos = -1;
  for (i=0; i<=type_len; i++)
    {
      if (i==type_len || char_type[i] != type)
        {
          if (pos>=0)
            {
              link = new_type_link();
              link->type = type;
              link->pos  = pos;
              link->len  = len;
              last->next = link;
              link->prev = last;
              last = last->next;
            }
          if (i==type_len)
            break;
          len = 0;
          pos = i;
        }
      type = char_type[i];
      len++;
    }

  /* Add the ending link */
  link = new_type_link();
  link->type = FRIBIDI_TYPE_EOT;
  link->level = -1;
  link->len = 0;
  link->pos = type_len;
  last->next = link;
  link->prev = last;

  return list;
}

/* explicits_list is a list like type_rl_list, that holds the explicit
   codes that are removed from rl_list, to reinsert them later by calling
   the override_list.
*/
static void *init_list(TypeLink **start, TypeLink **end)
{
  TypeLink *list = NULL;
  TypeLink *link;

  /* Add the starting link */
  list = new_type_link();
  list->type = FRIBIDI_TYPE_SOT;
  list->level = -1;
  list->len = 0;
  list->pos = 0;

  /* Add the ending link */
  link = new_type_link();
  link->type = FRIBIDI_TYPE_EOT;
  link->level = -1;
  link->len = 0;
  link->pos = 0;
  list->next = link;
  link->prev = list;

  *start = list;
  *end = link;
}

/* move an element before another element in a list, the list must have a
   previous element, used to update explicits_list.
   assuming that p have both prev and next or none of them.
*/
void move_element_before (TypeLink *p, TypeLink *list)
{
  if (p->prev) {
    p->prev->next = p->next;
    p->next->prev = p->prev;
  }
  p->prev = list->prev;
  list->prev->next = p;
  p->next = list;
  list->prev = p;
}

/* override the rl_list base with the elements in over list, used to reinsert
   the removed explicit codes from explicits_list into the type_rl_list and
   also to reset the embedding level of some chars in L1.
   i assume that the pos of the first element in base list is not more than
   the pos of the first element of the over list, and the pos of the last
   element of the base list is not less than the pos of the last element of
   the over list, they are always satisfied for the two purposes already said.
*/
void override_list (TypeLink *base, TypeLink *over)
{
  TypeLink *p = base, *q, *r, *s, *t;
  gint pos = 0, pos2;
  if (!base) {
    base = over;
    return;
  }  
  if (!over)
    return;
  for (q = over; q; /* move forward q on list at the end of the loop. */) {
    if (!q->len || q->pos < pos) {
      t = q;
      q = q->next;
      free_type_link(t);    
      continue;
    }    
    pos = q->pos;
    while (p->next && p->next->pos <= pos)
      p = p->next;
    /* now p is the element that q must be inserted 'in'. */
    pos2 = pos + q->len;
    r = p;
    while (r->next && r->next->pos < pos2)
      r = r->next;
    /* now r is the last element that q affects. */
    if (p == r) {
      /* split p into at most 3 interval, and insert q in the place of
         the second interval, set r to be the third part. */
      /* third part needed? */
      if (p->next && p->next->pos == pos2) {
        if (r->next)
          r = r->next;
        else {
          r = new_type_link();
          *r = *p;
          r->len = 0;
          r->pos = pos2;
        }
      } else {
        r = new_type_link();
        *r = *p;
        if (r->next)
          r->next->prev = r;
        r->len -= pos2 - r->pos;
        r->pos = pos2;
      }
      /* first part needed? */
      if (p->pos == pos) {
        if (p->prev) {
          t = p;
          p = p->prev;
          free_type_link(t);
        } else
         p->len = 0;
      } else {
        p->len = pos - p->pos;
      }
    } else {
      /* cut the end of p. */
      p->len = pos - p->pos;
      /* if all of p is cut, remove it. */
      if (!p->len && p->prev)
        p = p->prev;
      /* remove the elements between p and r. */
      if (p != r)
        for (s = p->next; s != r;) {
          t = s;
          s = s->next;
          free_type_link(t);
        }
      /* cut the begining of r. */
      r->pos = pos2;
      if (r->next)
        r->len = r->next->pos - pos2; 
      /* if all of r is cut, remove it. */
      if (!r->len && r->next) {
        t = r;
        r = r->next;
        free_type_link(t);    
      }
    }
    /* before updating the next and prev links to point to the inserted q,
       we must remember the next element of q in the over list.
    */
    t = q;
    q = q->next;
    p->next = t;
    t->prev = p;
    t->next = r;
    r->prev = t;
  }
}

/* Some convenience macros */
#define RL_TYPE(list) (list)->type
#define RL_LEN(list) (list)->len
#define RL_POS(list) (list)->pos
#define RL_LEVEL(list) (list)->level

static void compact_list(TypeLink *list)
{
  while(list)
    {
      if (list->prev &&
          RL_TYPE(list->prev) == RL_TYPE(list) &&
          RL_LEVEL(list->prev) == RL_LEVEL(list) &&
          RL_POS(list->prev) + RL_LEN(list->prev) == RL_POS(list))
        {
          TypeLink *next = list->next;
          list->prev->next = list->next;
          list->next->prev = list->prev;
          RL_LEN(list->prev) = RL_LEN(list->prev) + RL_LEN(list);
          free_type_link(list);
          list = next;
      }
      else
        list = list->next;
    }
}

/* Define a rule macro */

/* Rules for overriding current type */
#define TYPE_RULE1(old_this,            \
                   new_this)             \
     if (this_type == TYPE_ ## old_this)      \
         RL_TYPE(pp) =       FRIBIDI_TYPE_ ## new_this; \

/* Rules for current and previous type */
#define TYPE_RULE2(old_prev, old_this,            \
                   new_prev, new_this)             \
     if (    prev_type == FRIBIDI_TYPE_ ## old_prev       \
          && this_type == FRIBIDI_TYPE_ ## old_this)      \
       {                                          \
           RL_TYPE(ppprev) = FRIBIDI_TYPE_ ## new_prev; \
           RL_TYPE(pp) =       FRIBIDI_TYPE_ ## new_this; \
           continue;                              \
       }

/* A full rule that assigns all three types */
#define TYPE_RULE(old_prev, old_this, old_next,   \
                  new_prev, new_this, new_next)   \
     if (    prev_type == FRIBIDI_TYPE_ ## old_prev       \
          && this_type == FRIBIDI_TYPE_ ## old_this       \
          && next_type == FRIBIDI_TYPE_ ## old_next)      \
       {                                          \
           RL_TYPE(ppprev) = FRIBIDI_TYPE_ ## new_prev; \
           RL_TYPE(pp) =       FRIBIDI_TYPE_ ## new_this; \
           RL_TYPE(ppnext) = FRIBIDI_TYPE_ ## new_next; \
           continue;                              \
       }

/* For optimization the following macro only assigns the center type */
#define TYPE_RULE_C(old_prev, old_this, old_next,   \
                    new_this)   \
     if (    prev_type == FRIBIDI_TYPE_ ## old_prev       \
          && this_type == FRIBIDI_TYPE_ ## old_this       \
          && next_type == FRIBIDI_TYPE_ ## old_next)      \
       {                                          \
           RL_TYPE(pp) =       FRIBIDI_TYPE_ ## new_this; \
           continue;                              \
       }

/*=======================================================
// define macros for push and pop the status in the stack
//-------------------------------------------------------*/

/* There's some little points in pushing and poping into the status stack:
   1. when the embedding level is not valid (more than MAX_LEVEL=61),
   you must reject it, and not to push into the stack, but when you see a
   PDF, you must find the matching code, and if it was pushed in the stack,
   pop it, it means you must pop if and only if you have pushed the
   matching code, the over_pushed var counts the number of rejected codes yet.
   2. there's a more confusing point too, when the embedding level is exactly
   MAX_LEVEL-1=60, a LRO or LRE must be rejected because the new level would
   be MAX_LEVEL+1=62, that is invalid, but a RLO or RLE must be accepted
   because the new level is MAX_LEVEL=61, that is valid, so the rejected
   codes may be not continuous in the logical order, in fact there is at
   most two continuous intervals of codes, with a RLO or RLE between them.
   to support the case, the first_interval var counts the number of rejected
   codes in the first interval, when it is 0, means that there is only one
   interval yet.

/* a. If this new level would be valid, then this embedding code is valid.
   Remember (push) the current embedding level and override status.
   Reset current level to this new level, and reset the override status to
   new_override.
   b. If the new level would not be valid, then this code is invalid. Don't
   change the current level or override status.
*/
#define PUSH_STATUS(new_override) \
    { \
      if (new_level <= MAX_LEVEL) \
        { \
          if (level == MAX_LEVEL - 1) \
            first_interval = over_pushed; \
          status_stack->level = level; \
          status_stack->override = override; \
	  status_stack++; \
	  stack_size++; \
          level = new_level; \
          override = new_override; \
        } else \
	  over_pushed++; \
    }

/* If there was a valid matching code, restore (pop) the last remembered
   (pushed) embedding level and directional override.
*/
#define POP_STATUS \
    { \
      if (over_pushed + stack_size) \
      { \
        if (over_pushed > first_interval) \
	    over_pushed--; \
	else \
          { \
            if (over_pushed == first_interval) \
              first_interval = 0; \
	    status_stack--; \
	    stack_size--; \
            level = status_stack->level; \
            override = status_stack->override; \
          } \
      } \
    }

/*==========================================================================
// There was no support for sor and eor in the absence of Explicit Embedding
// Levels, so define macros, to support them, with as less change as needed.
//--------------------------------------------------------------------------*/

/* Return the direction of the level number, ie. even is FRIBIDI_TYPE_L and
   odd is FRIBIDI_TYPE_R.
*/
#define LEVEL_DIR(lev) \
    (lev & 1 ? FRIBIDI_TYPE_R : FRIBIDI_TYPE_L)

/* Return the type of previous char or the sor, if already at the start of
   a run level.
*/
#define PREV_TYPE_OR_SOR \
    (RL_LEVEL(ppprev)==RL_LEVEL(pp) ? RL_TYPE(ppprev) : LEVEL_DIR( \
      (RL_LEVEL(ppprev)>RL_LEVEL(pp) ? RL_LEVEL(ppprev) : RL_LEVEL(pp)) \
    ))

/* Return the type of next char or the eor, if already at the end of
   a run level.
*/
#define NEXT_TYPE_OR_EOR \
    (!ppnext ? LEVEL_DIR(RL_LEVEL(pp)) : \
    (RL_LEVEL(ppnext)==RL_LEVEL(pp) ? RL_TYPE(ppnext) : LEVEL_DIR( \
      (RL_LEVEL(ppnext)>RL_LEVEL(pp) ? RL_LEVEL(ppnext) : RL_LEVEL(pp)) \
    )))

#ifdef DEBUG
/*======================================================================
//  For debugging, define some macros for printing the types and the
//  levels.
//----------------------------------------------------------------------*/

static char char_from_level[] = {
  'e', /* -2, internal error, this level shouldn't be viewed.  */
  '_', /* -1, indicating start of string and end of string. */
  /* 0-9,A-F are the only valid levels in debug mode and before resolving
     implicits. after that the levels X, Y, Z may be appear too. */
  '0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
  'A', 'B', 'C', 'D', 'E', 'F',
  'X', 'Y', 'Z', /* only must appear after resolving implicits. */
  'o', 'o', 'o' /* overflows, this levels and higher levels show a bug!. */
};

static void print_types_re(TypeLink *pp)
{
  fprintf(stderr, "Run types  : ");
  while(pp)
    {
      fprintf(stderr, "%d:%d(%d)[%c] ", 
        RL_POS(pp), 
	RL_TYPE(pp),
	RL_LEN(pp), 
	char_from_level[RL_LEVEL(pp) + 2]);
      pp = pp->next;
    }
  fprintf(stderr, "\n");
}

static void print_resolved_levels(TypeLink *pp)
{
  fprintf(stderr, "Res. levels: ");
  while(pp)
    {
      gint i;
      for (i=0; i<RL_LEN(pp); i++)
        fprintf(stderr, "%c", char_from_level[RL_LEVEL(pp) + 2]);
      pp = pp->next;
    }
  fprintf(stderr, "\n");
}

static void print_resolved_types(TypeLink *pp)
{
  fprintf(stderr, "Res. types : ");
  while(pp) {
    gint i;
    for (i=0; i<RL_LEN(pp); i++)
      fprintf(stderr, "%c", char_from_type[pp->type]);
    pp = pp->next;
  }
  fprintf(stderr, "\n");
}

static void print_bidi_string(FriBidiChar *str)
{
  gint i;
  fprintf(stderr, "Org. types : ");
  for (i=0; i<bidi_string_strlen(str); i++)
    fprintf(stderr, "%c", char_from_type[fribidi_get_type(str[i])]);
  fprintf(stderr, "\n");
}
#endif

/*======================================================================
//  search_rl_for strong searches the run length list in the direction
//  indicated by dir for a strong directional. It returns a pointer to
//  the found character or NULL if none is found. */
//----------------------------------------------------------------------*/
static TypeLink *
search_rl_for_strong(TypeLink *pos,
                     gint  dir)
{
  TypeLink *pp = pos;

  if (dir == -1)
    {
      for (pp = pos; pp; pp=pp->prev)
        {
          FriBidiCharType char_type = RL_TYPE(pp);
          if (char_type == FRIBIDI_TYPE_R || char_type == FRIBIDI_TYPE_L ||
	      char_type == FRIBIDI_TYPE_RLO || char_type == FRIBIDI_TYPE_LRO)
            return pp;
        }
    }
  else
    {
      for (pp = pos; pp; pp=pp->next)
        {
          FriBidiCharType char_type = RL_TYPE(pp);
          if (char_type == FRIBIDI_TYPE_R || char_type == FRIBIDI_TYPE_L ||
	      char_type == FRIBIDI_TYPE_RLO || char_type == FRIBIDI_TYPE_LRO)
            return pp;
        }
    }
  return NULL;
}

/*======================================================================
//  This function should follow the Unicode specification closely!
//----------------------------------------------------------------------*/
static void
fribidi_analyse_string(/* input */
                       FriBidiChar *str,
                       gint len,
                       FriBidiCharType *pbase_dir,
                       /* output */
                       TypeLink **ptype_rl_list,
                       gint *pmax_level)
{
  gint base_level, base_dir;
  gint max_level;
  gint i;
  gint *char_type;
  gint prev_last_strong, last_strong;
  TypeLink *type_rl_list, *explicits_list, *explicits_list_end,
           *pp, *ppprev, *ppnext;

  /* Determinate character types */
  char_type = g_new(gint, len);
  for (i=0; i<len; i++)
    char_type[i] = fribidi_get_type(str[i]);

  /* Run length encode the character types */
  type_rl_list = run_length_encode_types(char_type, len);
  g_free(char_type);
  init_list(&explicits_list, &explicits_list_end);

  /* Find the base level */
  if (*pbase_dir == FRIBIDI_TYPE_L)
      base_level = 0;
  else if (*pbase_dir == FRIBIDI_TYPE_R)
      base_level = 1;

  /* P2. Search for first strong character and use its direction as base
     direction */
  else
    {
      base_level = 0; /* Default */
      base_dir = FRIBIDI_TYPE_N;
      for (pp = type_rl_list; pp; pp = pp->next)
	{
	  if (RL_TYPE(pp) == FRIBIDI_TYPE_R ||
	      RL_TYPE(pp) == FRIBIDI_TYPE_AL)
	    {
	      base_level = 1;
	      base_dir = FRIBIDI_TYPE_R;
	      break;
	    }
	  else if (RL_TYPE(pp) == FRIBIDI_TYPE_L)
	    {
	      base_level = 0;
	      base_dir = FRIBIDI_TYPE_L;
	      break;
	    }
	}
    
      /* If no strong base_dir was found, resort to the weak direction
       * that was passed on input.
       */
      if (base_dir == FRIBIDI_TYPE_N)
	if (*pbase_dir == FRIBIDI_TYPE_WR)
	  base_level = 1;
	else
	  base_level = 0;
    }
  base_dir = LEVEL_DIR(base_level);

  /* Explicit Levels and Directions */
  DBG("Explicit Levels and Directions.\n");
  {
    /* X1. Begin by setting the current embedding level to the paragraph
       embedding level. Set the directional override status to neutral.
       Process each character iteratively, applying rules X2 through X9.
       Only embedding levels from 0 to 61 are valid in this phase.
    */
    gint level = base_level;
    gint override = FRIBIDI_TYPE_N;
    gint new_level;
    /* stack */
    gint stack_size = 0;
    gint over_pushed = 0;
    gint first_interval = 0;
    level_info *status_stack = g_new(level_info, MAX_LEVEL + 2);
    TypeLink *dummy = new_type_link();

    gint i;

    for (pp = type_rl_list->next; pp->next; pp = pp->next)
      {
        gint this_type = RL_TYPE(pp);
      /* 1. Explicit Embeddings */
        /* X2. With each RLE, compute the least greater odd embedding level. */
        if (this_type == FRIBIDI_TYPE_RLE)
	  for (i = 0; i < RL_LEN(pp); i++)
          {
            new_level = (level + 1) | 1;
            PUSH_STATUS(FRIBIDI_TYPE_N);
          }
        /* X3. With each LRE, compute the least greater even embedding level. */
        else if (this_type == FRIBIDI_TYPE_LRE)
	  for (i = 0; i < RL_LEN(pp); i++)
          {
            new_level = (level + 2) & ~1;
            PUSH_STATUS(FRIBIDI_TYPE_N);
          }
      /* 2. Explicit Overrides */
        /* X4. With each RLO, compute the least greater odd embedding level. */
        if (this_type == FRIBIDI_TYPE_RLO)
	  for (i = 0; i < RL_LEN(pp); i++)
          {
            new_level = (level + 1) | 1;
            PUSH_STATUS(FRIBIDI_TYPE_R);
          }
        /* X5. With each LRO, compute the least greater even embedding level. */
        else if (this_type == FRIBIDI_TYPE_LRO)
	  for (i = 0; i < RL_LEN(pp); i++)
          {
            new_level = (level + 2) & ~1;
            PUSH_STATUS(FRIBIDI_TYPE_L);
          }
        
        /* X6. For all typed besides RLE, LRE, RLO, LRO, and PDF:
           a. Set the level of the current character to the current
           embedding level.
           b. Whenever the directional override status is not neutral,
           reset the current character type to the directional override
           status.
        */
        else if (this_type != FRIBIDI_TYPE_PDF)
          {
            RL_LEVEL(pp) = level;
            if (override != FRIBIDI_TYPE_N &&
	        RL_TYPE(pp) != FRIBIDI_TYPE_BN)
              RL_TYPE(pp) = override;
          }
      /* 3. Terminating Embeddings and overrides */
        /* X7. With each PDF, determine the matching embedding or
           ovveride code.
        */
        else /* now: this_type == FRIBIDI_TYPE_PDF */
	  for (i = 0; i < RL_LEN(pp); i++)
            POP_STATUS;
        /* X8. All explicit directional embeddings and overrides are
           completely terminated at the end of each paragraph. Paragraph
           separators are not included in the embedding.
        */
        /*
           This function is running on a single paragraph, so we can do
           X8 after all the input is processed.
        */
        /* X9. Remove all RLE, LRE, RLO, LRO, PDF, and BN codes.
        */
        if (this_type == FRIBIDI_TYPE_RLE ||
            this_type == FRIBIDI_TYPE_LRE ||
            this_type == FRIBIDI_TYPE_RLO ||
            this_type == FRIBIDI_TYPE_LRO ||
            this_type == FRIBIDI_TYPE_PDF ||
            this_type == FRIBIDI_TYPE_BN)
          {
            /* Remove element and add it to explicits_list */
            dummy->next = pp->next;
            pp->level = -2;
            move_element_before(pp, explicits_list_end);
            pp = dummy;            
          }
      }

    /* Implementing X8. It has no effect on a single paragraph! */
    level = base_level;
    override = FRIBIDI_TYPE_N;
    status_stack -= stack_size;
    stack_size = 0;

    free_type_link(dummy);
    g_free(status_stack);
  }
  /* X10. The remaining rules are applied to each run of characters at the
     same level. For each run, determine the start-of-level-run (sor) and
     end-of-level-run (eor) type, either L or R. This depends on the
     higher of the two levels on either side of the boundary (at the start
     or end of the paragraph, the level of the 'other' run is the base
     embedding level). If the higher level is odd, the type is R, otherwise
     it is L.
  */
  /* Resolving Implicit Levels can be done out of X10 loop, so only change
     of Resolving Weak Types and Resolving Neutral Types is needed.
  */

  compact_list(type_rl_list);

#ifdef DEBUG
  if (fribidi_debug) {
    print_bidi_string(str);
    print_resolved_levels(type_rl_list);
    print_resolved_types(type_rl_list);
  }
#endif

  /* 4. Resolving weak types */
  DBG("Resolving weak types.\n");
  {
    gint last_strong = base_dir;
    gint prev_type_new;
    gint prev_type_old = base_dir;

    for (ppnext= (pp=(ppprev=type_rl_list)->next)->next;ppnext;
         ppprev=pp, pp=ppnext, ppnext=ppnext->next) {
      /* When we are at the start of a level run, prev_type is sor. */ 
      gint prev_type;
      gint this_type = RL_TYPE(pp);
      /* When we are at the end of a level run, prev_type is eor. */ 
      gint next_type = NEXT_TYPE_OR_EOR;
      
      prev_type_new = RL_TYPE(ppprev);
      RL_TYPE(ppprev) = prev_type_old;
      prev_type_old = this_type;
      prev_type = PREV_TYPE_OR_SOR;

      /* Remember the last strong character
         It's very important to do it here, not at the end of the loop
	 because the types may change, it affects rule 3.
      */
      if (RL_LEVEL(ppprev) != RL_LEVEL(pp))
        last_strong = prev_type/*sor*/;
      else
        if (prev_type == FRIBIDI_TYPE_AL ||
            prev_type == FRIBIDI_TYPE_R ||
            prev_type == FRIBIDI_TYPE_L)
          last_strong = prev_type;

      /* W1. NSM
         Examine each non-spacing mark (NSM) in the level run, and change
         the type of the NSM to the type of the previous character. If the
         NSM is at the start of the level run, it will get the type of sor.
      */
      if (this_type == FRIBIDI_TYPE_NSM)
          RL_TYPE(pp) = prev_type;

      /* W2: European numbers.
       */
      if (this_type == FRIBIDI_TYPE_EN && last_strong == FRIBIDI_TYPE_AL)
        RL_TYPE(pp) = FRIBIDI_TYPE_AN;

      /* W3: Change ALs to R.
      */
      if (this_type == FRIBIDI_TYPE_AL)
        RL_TYPE(pp) = FRIBIDI_TYPE_R;

      RL_TYPE(ppprev) = prev_type_new;

    }

    for (ppnext= (pp=(ppprev=type_rl_list)->next)->next;ppnext;
         ppprev=pp, pp=ppnext, ppnext=ppnext->next) {
      /* When we are at the start of a level run, prev_type is sor. */ 
      gint prev_type = PREV_TYPE_OR_SOR;
      gint this_type = RL_TYPE(pp);
      /* When we are at the end of a level run, prev_type is eor. */ 
      gint next_type = NEXT_TYPE_OR_EOR;

      /* W4. A single european separator changes to a european number.
         A single common separator between two numbers of the same type
         changes to that type.
      */
      if (RL_LEN(pp) == 1) 
        {
          TYPE_RULE_C(EN,ES,EN,   EN);
          TYPE_RULE_C(EN,CS,EN,   EN);
          TYPE_RULE_C(AN,CS,AN,   AN);
        }
    }

    for (ppnext= (pp=(ppprev=type_rl_list)->next)->next;ppnext;
         ppprev=pp, pp=ppnext, ppnext=ppnext->next) {
      /* When we are at the start of a level run, prev_type is sor. */ 
      gint prev_type = PREV_TYPE_OR_SOR;
      gint this_type = RL_TYPE(pp);
      /* When we are at the end of a level run, prev_type is eor. */ 
      gint next_type = NEXT_TYPE_OR_EOR;
      
      /* Remember the last strong character
         It's very important to do it here, not at the end of the loop
	 because the types may change, it affects rule 3.
      */
      if (RL_LEVEL(ppprev) != RL_LEVEL(pp))
        last_strong = prev_type/*sor*/;
      else
        if (prev_type == FRIBIDI_TYPE_AL ||
            prev_type == FRIBIDI_TYPE_R ||
            prev_type == FRIBIDI_TYPE_L)
          last_strong = prev_type;
      /* W5. A sequence of European terminators adjacent to European
         numbers changes to All European numbers.
      */
      if (this_type == FRIBIDI_TYPE_ET &&
         (prev_type == FRIBIDI_TYPE_EN || next_type == FRIBIDI_TYPE_EN))
        RL_TYPE(pp) = FRIBIDI_TYPE_EN;

      /* This type may have been overriden. */
      this_type = RL_TYPE(pp);
      
      /* W6. Otherwise change separators and terminators to other neutral.
      */
      if (this_type == FRIBIDI_TYPE_ET ||
          this_type == FRIBIDI_TYPE_CS ||
	  this_type == FRIBIDI_TYPE_ES)
        RL_TYPE(pp) = FRIBIDI_TYPE_ON;

      /* W7. Change european numbers to L. 
      */
      if (this_type == FRIBIDI_TYPE_EN && last_strong == FRIBIDI_TYPE_L)
        RL_TYPE(pp) = FRIBIDI_TYPE_L;
    }

  }

  compact_list(type_rl_list);
  
#ifdef DEBUG
  if (fribidi_debug) {
    print_types_re(type_rl_list);
    print_resolved_levels(type_rl_list);
    print_resolved_types(type_rl_list);
  }  
#endif

  /* 5. Resolving Neutral Types */
  DBG("Resolving neutral types.\n");
  {
  TypeLink *ppprev, *ppnext/* prev and next non neutral */;
  gint prev_type, next_type;
  
  /* We can now collapse all separators and other neutral types to
     plain neutrals. */
  for (pp = type_rl_list->next; pp->next; pp = pp->next)
    {
      gint this_type = RL_TYPE(pp);

      if (this_type == FRIBIDI_TYPE_WS ||
          this_type == FRIBIDI_TYPE_ON ||
          this_type == FRIBIDI_TYPE_ES ||
          this_type == FRIBIDI_TYPE_ET ||
          this_type == FRIBIDI_TYPE_BN ||
          this_type == FRIBIDI_TYPE_BS ||
          this_type == FRIBIDI_TYPE_SS ||
          this_type == FRIBIDI_TYPE_CS)
        RL_TYPE(pp) = FRIBIDI_TYPE_N;
    }
    
  compact_list(type_rl_list);
  
  /* N1. and N2.
     For each neutral, resolve it.
  */ 
  for (ppnext=pp=(ppprev=type_rl_list)->next; pp->next; pp=pp->next)
    {
      gint this_type = RL_TYPE(pp);
      /* "European and arabic numbers are treated as though they were R" */
      if (this_type == FRIBIDI_TYPE_EN || this_type == FRIBIDI_TYPE_AN)
          this_type = FRIBIDI_TYPE_R;

      /* Find prev_type from ppprev. */
      prev_type = PREV_TYPE_OR_SOR;
      /* "European and arabic numbers are treated as though they were R" */
      if (prev_type == FRIBIDI_TYPE_EN || prev_type == FRIBIDI_TYPE_AN)
        prev_type = FRIBIDI_TYPE_R;

      /* Update ppnext if needed. */
      if (RL_LEVEL(pp) == RL_LEVEL(ppnext)) {
        /* Find next non-neutral. */
        for (ppnext = pp->next; 
             RL_TYPE(ppnext) == FRIBIDI_TYPE_N &&
             RL_LEVEL(ppnext) == RL_LEVEL(ppnext->prev); ppnext = ppnext->next)
          /* Nothing! */;
        next_type = NEXT_TYPE_OR_EOR;

        /* "European and arabic numbers are treated as though they were R" */
        if (next_type == FRIBIDI_TYPE_EN || next_type == FRIBIDI_TYPE_AN)
          next_type = FRIBIDI_TYPE_R;
      }

      if (this_type == FRIBIDI_TYPE_N)
        RL_TYPE(pp) = (prev_type == next_type) ?
                      /* N1. */ prev_type :
                      /* N2. */ /*FRIBIDI_TYPE_E*/LEVEL_DIR(RL_LEVEL(pp));

      /* Update ppprev if needed. */
      if (this_type != FRIBIDI_TYPE_N ||
          RL_LEVEL(pp) != RL_LEVEL(pp->next))
        ppprev = pp;
    }
  }
  
  compact_list(type_rl_list);
#ifdef DEBUG
  if (fribidi_debug) {
    print_types_re(type_rl_list);
    print_resolved_levels(type_rl_list);
    print_resolved_types(type_rl_list);
  }
#endif
   
  /* 6. Resolving implicit levels */
  DBG("Resolving implicit levels.\n");
  {
    max_level = base_level;
    
    for (pp = type_rl_list->next; pp->next; pp = pp->next)
      {
        gint this_type = RL_TYPE(pp);
        gint level = RL_LEVEL(pp);

        /* This code should be expanded to handle explicit directions! */

        /* I1. Even */
        if (level % 2 == 0)
          {
            if (this_type == FRIBIDI_TYPE_R)
              RL_LEVEL(pp)++;
            else if (this_type == FRIBIDI_TYPE_AN ||
                     this_type == FRIBIDI_TYPE_EN)
              RL_LEVEL(pp) += 2;
          }
        /* I2. Odd */
        else
            if (this_type == FRIBIDI_TYPE_L  ||
                this_type == FRIBIDI_TYPE_AN ||
                this_type == FRIBIDI_TYPE_EN)
              RL_LEVEL(pp)++;

        if (RL_LEVEL(pp) > max_level)
          max_level = RL_LEVEL(pp);
      }
  }
  
  compact_list(type_rl_list);
  
#ifdef DEBUG
  if (fribidi_debug) {
    print_bidi_string(str);
    print_resolved_levels(type_rl_list);
    print_resolved_types(type_rl_list);
  }
#endif

/* Reinsert the explicit codes & bn's that already removed, from the
   explicits_list to type_rl_list. */
  DBG("Reinserting explicit codes.\n");
  {
    TypeLink *p;

    override_list(type_rl_list, explicits_list);
    p = type_rl_list->next;
    if (p->level < 0)
      p->level = base_level;
    for (; p->next; p = p->next)
      if (p->level < 0)
        p->level = p->prev->level;
  }

#ifdef DEBUG
  if (fribidi_debug) {
    print_types_re(type_rl_list);
    print_resolved_levels(type_rl_list);
    print_resolved_types(type_rl_list);
  }
#endif

  *ptype_rl_list = type_rl_list;
  *pmax_level = max_level;
  *pbase_dir = base_dir;
}

/*======================================================================
//  Frees up the rl_list, must be called after each call to
    fribidi_analyse_string(), after the list is not needed anymore.
//----------------------------------------------------------------------*/
void free_rl_list(TypeLink *type_rl_list) {

  TypeLink *p, *pp;
  if (!pp)
    return;
#ifdef USE_SIMPLE_MALLOC
  for (pp = type_rl_list; pp; pp = pp->next) {
    p = pp->next;
    g_free(pp);
  };
#else
  for (pp = type_rl_list->next; pp->next; pp=pp->next)
    /*Nothing*/;
  pp->next = free_type_links;
  free_type_links = type_rl_list;
  type_rl_list = NULL;
#endif                          

}
/*======================================================================
//  Here starts the exposed front end functions.
//----------------------------------------------------------------------*/

/*======================================================================
//  fribidi_log2vis() calls the function_analyse_string() and then
//  does reordering and fills in the output strings.
//----------------------------------------------------------------------*/
void fribidi_log2vis(/* input */
                     FriBidiChar *str,
                     gint len,
                     FriBidiCharType *pbase_dir,
                     /* output */
                     FriBidiChar *visual_str,
                     guint16     *position_L_to_V_list,
                     guint16     *position_V_to_L_list,
                     guint8      *embedding_level_list
                     )
{
  TypeLink *type_rl_list, *pp = NULL;
  gint max_level;
  gboolean private_V_to_L = FALSE;

  if (len == 0)
    return;
  
  /* If l2v is to be calculated we must have l2v as well. If it is not
     given by the caller, we have to make a private instance of it. */
  if (position_L_to_V_list && !position_V_to_L_list)
    {
      private_V_to_L = TRUE;
      position_V_to_L_list = g_new(guint16, len+1);
    }

  if (len > FRIBIDI_MAX_STRING_LENGTH)
    {
#ifdef DEBUG
      fprintf(stderr, "Fribidi is set to not handle strings > %d chars!\n",
        FRIBIDI_MAX_STRING_LENGTH);
#endif
      return;
    }
  fribidi_analyse_string(str, len, pbase_dir,
                         /* output */
                         &type_rl_list,
                         &max_level);

  /* 7. Reordering resolved levels */
  DBG("Reordering resolved levels.\n");

  {
    gint level_idx;
    gint i, j, k, state, pos;
    TypeLink *p, *q, *list, *list_end;

    /* L1. Reset the embedding levels of some chars.
    */
    init_list(&list, &list_end);
    q = list_end;
    state = 1;
    pos = len - 1;
    for (j=len-1; j>=0; j--) {
      k = fribidi_get_type(str[j]);
      if (!state && (k == FRIBIDI_TYPE_BS  || k == FRIBIDI_TYPE_SS)) {
        state = 1;
        pos = j;
      } else
      if (state && (!j || !(
          k == FRIBIDI_TYPE_WS  ||
          k == FRIBIDI_TYPE_BS  ||
          k == FRIBIDI_TYPE_SS  ||
          k == FRIBIDI_TYPE_BN  ||
          k == FRIBIDI_TYPE_LRO ||
          k == FRIBIDI_TYPE_LRE ||
          k == FRIBIDI_TYPE_RLO ||
          k == FRIBIDI_TYPE_RLE ||
          k == FRIBIDI_TYPE_PDF))) {
        /* if state is on at the very first of string, do this too. */
        if (!j)
          j--;
        state = 0;
        p = new_type_link();
        p->prev = p->next = 0;
        p->pos = j+1;
        p->len = pos - j;
        p->type = *pbase_dir;
        p->level = p->type == FRIBIDI_TYPE_R ? 1 : 0;
        move_element_before(p, q);
        q = p;
      }
    }
    override_list(type_rl_list, list);

#ifdef DEBUG
  if (fribidi_debug) {
    DBG("Reset the embedding levels.\n");
    print_types_re(type_rl_list);
    print_resolved_levels(type_rl_list);
    print_resolved_types(type_rl_list);
  }
#endif

    /* TBD: L3 */

    /* Set up the ordering array to sorted order and copy the logical
       string to the visual */
    if (position_L_to_V_list)
      for (i=0; i<len+1; i++)
        position_L_to_V_list[i]=i;
    
    if (visual_str)
      for (i=0; i<len+1; i++)
        visual_str[i] = str[i];

    /* Assign the embedding level array */
    if (embedding_level_list)
      for (pp = type_rl_list->next; pp->next; pp = pp->next)
        {
          gint i;
          gint pos = RL_POS(pp);
          gint len = RL_LEN(pp);
          gint level = RL_LEVEL(pp);
          for (i=0; i<len; i++)
            embedding_level_list[pos + i] = level;
      }

    /* Reorder both the outstring and the order array*/
    if (visual_str || position_V_to_L_list)
      {

        if (visual_str)
          /* L4. Mirror all characters that are in odd levels and have mirrors.
          */
          for (pp = type_rl_list->next; pp->next; pp = pp->next)
            {
              if (RL_LEVEL(pp) & 1)
                {
                  gint i;
                  for (i=RL_POS(pp); i<RL_POS(pp)+RL_LEN(pp); i++)
                    {
                      FriBidiChar mirrored_ch;
                      if (fribidi_get_mirror_char(visual_str[i], &mirrored_ch))
                        visual_str[i] = mirrored_ch;
                    }
                }
            }

        /* L2. Reorder.
        */
        for (level_idx = max_level; level_idx>0; level_idx--)
          {
            for (pp = type_rl_list->next; pp->next; pp = pp->next)
              {
                if (RL_LEVEL(pp) >= level_idx)
                  {
                    /* Find all stretches that are >= level_idx */
                    gint len = RL_LEN(pp);
                    gint pos = RL_POS(pp);
                    TypeLink *pp1 = pp->next;
                    while(pp1->next && RL_LEVEL(pp1) >= level_idx)
                      {
                        len+= RL_LEN(pp1);
                        pp1 = pp1->next;
                      }
                    
                    pp = pp1->prev;
                    if (visual_str)
                      bidi_string_reverse(visual_str+pos, len);
                    if (position_V_to_L_list)
                      int16_array_reverse(position_V_to_L_list+pos, len);

                  }
              }
          }
      }

    /* Convert the l2v list to v2l */
    if (position_V_to_L_list && position_L_to_V_list)
      for (i=0; i<len; i++)
        position_V_to_L_list[position_L_to_V_list[i]] = i;
  }

  free_rl_list(type_rl_list);
  
}

/*======================================================================
//  fribidi_embedding_levels() is used in order to just get the
//  embedding levels.
//----------------------------------------------------------------------*/
void fribidi_log2vis_get_embedding_levels(
                     /* input */
                     FriBidiChar *str,
                     gint len,
                     FriBidiCharType *pbase_dir,
                     /* output */
                     guint8 *embedding_level_list
                     )
{
  TypeLink *type_rl_list, *pp;
  gint max_level;

  if (len = 0)
    return;
  
  fribidi_analyse_string(str, len, pbase_dir,
                         /* output */
                         &type_rl_list,
                         &max_level);

  for (pp = type_rl_list->next; pp->next; pp = pp->next)
    {
      gint i;
      gint pos = RL_POS(pp);
      gint len = RL_LEN(pp);
      gint level = RL_LEVEL(pp);
      for (i=0; i<len; i++)
        embedding_level_list[pos + i] = level;
    }
  
  free_rl_list(type_rl_list);
}
