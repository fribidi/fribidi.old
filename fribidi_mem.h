/* FriBidi - Library of BiDi algorithm
 * Copyright (C) 1999,2000 Dov FriBidirobgeld, and
 * Copyright (C) 2001 Behdad Esfahbod.
 * 
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser FriBidieneral Public  
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 * 
 * This library is distributed in the hope that it will be useful,  
 * but WITHOUT ANY WARRANTY; without even the implied warranty of   
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser FriBidieneral Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser FriBidieneral Public License  
 * along with this library, in a file named COPYING; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place, Suite 330,
 * Boston, MA 02111-1307, USA
 * 
 * For licensing issues, contact <dov@imagic.weizmann.ac.il> and
 * <fwpg@sharif.edu>.
 */

#ifndef _FRIBIDI_MEM_H
#define _FRIBIDI_MEM_H

#include <stdlib.h>
#include <limits.h>

#include "fribidi_config.h"

#define INT8 char
#define INT16 short
#define INT32 long

typedef int boolean;

typedef signed INT8 int8;
typedef unsigned INT8 uint8;
typedef signed INT16 int16;
typedef unsigned INT16 uint16;
typedef signed INT32 int32;
typedef unsigned INT32 uint32;

#define TRUE 1
#define FALSE 0

typedef struct _FriBidiList FriBidiList;
struct _FriBidiList
{
  void *data;
  FriBidiList *next;
  FriBidiList *prev;
};

FriBidiList *fribidi_list_append (FriBidiList *list, void *data);

typedef struct _FriBidiMemChunk FriBidiMemChunk;

#define FRIBIDI_ALLOC_ONLY      1
#define FRIBIDI_ALLOC_AND_FREE  2

FriBidiMemChunk *fribidi_mem_chunk_new (char *name,
					int atom_size,
					unsigned long area_size, int type);
void fribidi_mem_chunk_destroy (FriBidiMemChunk *mem_chunk);
void *fribidi_mem_chunk_alloc (FriBidiMemChunk *mem_chunk);
void *fribidi_mem_chunk_alloc0 (FriBidiMemChunk *mem_chunk);
void fribidi_mem_chunk_free (FriBidiMemChunk *mem_chunk, void *mem);

#define fribidi_mem_chunk_create(type, pre_alloc, alloc_type) ( \
  fribidi_mem_chunk_new (#type " mem chunks (" #pre_alloc ")", \
                   sizeof (type), \
                   sizeof (type) * (pre_alloc), \
                   (alloc_type)) \
)
#define fribidi_chunk_new(type, chunk)        ( \
  (type *) fribidi_mem_chunk_alloc (chunk) \
)

int fribidi_strcasecmp (const char *s1, const char *s2);

#endif /* _FRIBIDI_MEM_H */
