/* FriBidi - Library of BiDi algorithm
 * Copyright (C) 1999 Dov Grobgeld
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */
#include <stdio.h>
#include <stdlib.h>
#include "fribidi.h"
#include "fribidi_tables.i"

/*======================================================================
//  fribidi_get_type() returns the bidi type of a character.
//----------------------------------------------------------------------*/
FriBidiCharType fribidi_get_type(FriBidiChar uch)
{
  int i = uch % 256, j = uch / 256;
  guchar *block = FriBidiPropertyBlocks[j];
  if (block)
    return block[i];
  else {
    switch (j) {
      case 0x05: if (i >= 0x90) return FRIBIDI_TYPE_R; else break;

      case 0xFB: if (i >= 0x50) return FRIBIDI_TYPE_AL; else
                 if (i >= 0x1D) return FRIBIDI_TYPE_R; else break;
      case 0x06: 
      case 0xFC: 
      case 0xFD: return FRIBIDI_TYPE_AL;
      case 0x07: if (i <= 0xBF) return FRIBIDI_TYPE_AL; else break;
      case 0xFE: if (i >= 0x70) return FRIBIDI_TYPE_AL; else break;
    }    
    return FRIBIDI_TYPE_L;      
  }
}

gboolean
fribidi_get_mirror_char(/* Input */
		       FriBidiChar ch,
		       /* Output */
		       FriBidiChar *mirrored_ch)
{
  int pos, step;
  gboolean found = FALSE;

  pos = step = (nFriBidiMirroredChars/2)+1;

  while(step > 1)
    {
      FriBidiChar cmp_ch = FriBidiMirroredChars[pos].ch;
      step = (step+1)/2;
      
      if (cmp_ch < ch)
	{
	  pos += step;
	  if (pos>nFriBidiMirroredChars-1)
	    pos = nFriBidiMirroredChars-1;
	}
      else if (cmp_ch > ch)
	{
	  pos -= step;
	  if (pos<0)
	    pos=0;
	}
      else
	break;
    }
  if (FriBidiMirroredChars[pos].ch == ch)
    {
      *mirrored_ch = FriBidiMirroredChars[pos].mirrored_ch;
      found = TRUE;
    }
  return found;
}

