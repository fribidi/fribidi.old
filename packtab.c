/* PackTab - Pack a static table
 * Copyright (C) 2001 Behdad Esfahbod. 
 * 
 * This library is free software; you can redistribute it and/or 
 * modify it under the terms of the GNU Lesser General Public 
 * License as published by the Free Software Foundation; either 
 * version 2.1 of the License, or (at your option) any later version. 
 * 
 * This library is distributed in the hope that it will be useful, 
 * but WITHOUT ANY WARRANTY; without even the implied warranty of 
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU 
 * Lesser General Public License for more details. 
 * 
 * You should have received a copy of the GNU Lesser General Public License 
 * along with this library, in a file named COPYING; if not, write to the 
 * Free Software Foundation, Inc., 59 Temple Place, Suite 330, 
 * Boston, MA 02111-1307, USA  
 * 
 * For licensing issues, contact <fwpg@sharif.edu>. 
 */

/*
  8 <= N <= 2^21
  int key
  1 <= a, b, max_depth <= 21
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef int uni_table[1024 * 1024 * 2];
static int n, a, b, max_depth, N, digits, tab_width, per_row;
static uni_table temp, x, perm, *tab;
static int pow[22], cluster, cmpcluster;
static char **name, *key_type_name, *table_name, *macro_name;
static FILE *f;

static void
init (int *base)
{
  int i;
  pow[0] = 1;
  for (i = 1; i <= 21; i++)
    pow[i] = pow[i - 1] * 2;
  for (n = 21; pow[n] > N || N % pow[n]; n--)
    ;
  digits = (n + 3) / 4;
  for (i = 6; i; i--)
    if (pow[i] * (tab_width + 1) < 80)
      break;
  per_row = pow[i];
}

static int
compare (const void *a, const void *b)
{
  int i;
  for (i = 0; i < cmpcluster; i++)
    if (((int *) a)[i] != ((int *) b)[i])
      return ((int *) a)[i] - ((int *) b)[i];
  return 0;
}

static int lev, p[22], t[22], c[22], clusters[22], s, nn;
static int best_lev, best_p[22], best_t[22], best_c[22], best_cluster[22],
  best_s;

static void
found ()
{
  int i;

  if (s < best_s)
    {
      best_s = s;
      best_lev = lev;
      for (i = 0; i <= lev; i++)
	{
	  best_p[i] = p[i];
	  best_c[i] = c[i];
	  best_t[i] = t[i];
	  best_cluster[i] = clusters[i];
	}
    }
};

static void
bt (int node_size)
{
  int i, j, k, y, sbak;
  if (t[lev] == 1)
    {
      found ();
      return;
    }
  if (lev == max_depth)
    return;

  for (i = 1 - t[lev] % 2; i <= nn + (t[lev] >> nn) % 2; i++)
    {
      nn -= (p[lev] = i);
      clusters[lev] = cluster = (i && nn >= 0) ? pow[i] : t[lev];
      cmpcluster = cluster + 1;

      t[lev + 1] = (t[lev] - 1) / cluster + 1;
      for (j = 0; j < t[lev + 1]; j++)
	{
	  memmove (temp + j * cmpcluster, tab[lev] + j * cluster,
		   cluster * sizeof (tab[lev][0]));
	  temp[j * cmpcluster + cluster] = j;
	}
      qsort (temp, t[lev + 1], cmpcluster * sizeof (temp[0]), compare);
      for (j = 0; j < t[lev + 1]; j++)
	{
	  perm[j] = temp[j * cmpcluster + cluster];
	  temp[j * cmpcluster + cluster] = 0;
	}
      k = 1;
      y = 0;
      tab[lev + 1][perm[0]] = perm[0];
      for (j = 1; j < t[lev + 1]; j++)
	{
	  if (compare (temp + y, temp + y + cmpcluster))
	    {
	      k++;
	      tab[lev + 1][perm[j]] = perm[j];
	    }
	  else
	    tab[lev + 1][perm[j]] = tab[lev + 1][perm[j - 1]];
	  y += cmpcluster;
	}
      sbak = s;
      s += k * node_size * cluster;
      c[lev] = k;

      if (s >= best_s)
	{
	  s = sbak;
	  nn += i;
	  return;
	}

      lev++;
      bt (b);
      lev--;

      s = sbak;
      nn += i;
    }
}

static void
solve ()
{
  best_lev = max_depth + 2;
  best_s = N * a + 2 * b;
  lev = 0;
  s = 0;
  nn = n;
  t[0] = N;
  bt (a);
}

static void
write_in ()
{
}

static void
write_array ()
{
  int i, j, k, y, ii;
  if (best_t[lev] == 1)
    {
      write_in (f);
      return;
    }

  nn -= (i = best_p[lev]);
  cluster = best_cluster[lev];
  cmpcluster = cluster + 1;

  t[lev + 1] = best_t[lev + 1];
  for (j = 0; j < t[lev + 1]; j++)
    {
      memmove (temp + j * cmpcluster, tab[lev] + j * cluster,
	       cluster * sizeof (tab[lev][0]));
      temp[j * cmpcluster + cluster] = j;
    }
  qsort (temp, t[lev + 1], cmpcluster * sizeof (temp[0]), compare);
  for (j = 0; j < t[lev + 1]; j++)
    {
      perm[j] = temp[j * cmpcluster + cluster];
      temp[j * cmpcluster + cluster] = 0;
    }
  k = 1;
  y = 0;
  tab[lev + 1][perm[0]] = x[0] = perm[0];
  for (j = 1; j < t[lev + 1]; j++)
    {
      if (compare (temp + y, temp + y + cmpcluster))
	{
	  x[k] = perm[j];
	  tab[lev + 1][perm[j]] = x[k];
	  k++;
	}
      else
	tab[lev + 1][perm[j]] = tab[lev + 1][perm[j - 1]];
      y += cmpcluster;
    }

  i = 0;
  for (ii = 1; ii < k; ii++)
    if (x[ii] < x[i])
      i = ii;
  for (ii = 0; ii < k; ii++)
    {
      int kk, jj;
      fprintf (f, "%s ", key_type_name);
      for (j = 0; j < lev; j++)
	fprintf (f, "*");
      fprintf (f, "%s", table_name);
      if (best_t[lev + 1] != 1)
	fprintf (f, "Level%d_%0*X", best_lev - lev - 1, digits,
		 x[i] * pow[n - nn]);
      fprintf (f, "[%d] = {", cluster);
      kk = x[i] * cluster;
      if (!lev)
	if (name)
	  for (j = 0; j < cluster; j++)
	    {
	      if (!(j % per_row) && j != cluster - 1)
		fprintf (f, "\n  ");
	      fprintf (f, "%*s,", tab_width, name[tab[lev][kk++]]);
	    }
	else
	  for (j = 0; j < cluster; j++)
	    {
	      if (!(j % per_row) && j != cluster - 1)
		fprintf (f, "\n  ");
	      fprintf (f, "%*d,", tab_width, tab[lev][kk++]);
	    }
      else
	for (j = 0; j < cluster; j++, kk++)
	  fprintf (f, "\n  %sLevel%d_%0*X,  /* %0*X..%0*X */", table_name,
		   best_lev - lev, digits,
		   tab[lev][kk] * pow[n - nn - best_p[lev]], digits,
		   x[i] * pow[n - nn] + j * pow[n - nn - best_p[lev]], digits,
		   x[i] * pow[n - nn] + (j + 1) * pow[n - nn - best_p[lev]] -
		   1);
      fprintf (f, "\n};\n\n");
      jj = i;
      for (j = 0; j < k; j++)
	if (x[j] > x[i] && (x[j] < x[jj] || jj == i))
	  jj = j;
      i = jj;
    }
  lev++;
  write_array (f);
  lev--;
}

static void
write_source ()
{
  int i, j;

  lev = 0;
  s = 0;
  nn = n;
  t[0] = N;
  fprintf (f, "\n*/\n\n" "/* *INDENT-OFF* */\n\n");
  write_array (f);
  fprintf (f, "/* *INDENT-ON* */\n\n");

  fprintf (f, "#define %s(x) \\\n" "        %s", macro_name, table_name);
  j = 1;
  for (i = 0; i < best_lev; i++)
    j *= best_cluster[i];
  for (i = 0; i < best_lev; i++)
    {
      j /= best_cluster[best_lev - 1 - i];
      fprintf (f, "[(x)");
      if (i < best_lev - 1)
	fprintf (f, "/%d", j);
      if (i)
	fprintf (f, "%%%d", pow[best_p[best_lev - 1 - i]]);
      fprintf (f, "]");
    }
  fprintf (f, "\n\n");
}

static void
write_out ()
{
  int i;
  fprintf (f, "/*\n"
	   "  Automatically generated by packtab.c\n\n"
	   "  just use %s(key)\n\n"
	   "  assumed sizeof(pointer) == %d\n"
	   "  assumed sizeof(%s) == %d\n"
	   "  required memory: %d\n"
	   "  lookups: %d\n"
	   "  partition shape: %s", macro_name, b, key_type_name, a, best_s,
	   best_lev, table_name);
  for (i = best_lev - 1; i >= 0; i--)
    fprintf (f, "[%d]", best_cluster[i]);
  fprintf (f, "\n" "  different table entries:");
  for (i = best_lev - 1; i >= 0; i--)
    fprintf (f, " %d", best_c[i]);
  write_source ();
}

int
pack_table (int *base, int key_num, int key_size, int ptr_size,
	    int p_max_depth, int p_tab_width, char **p_name,
	    char *p_key_type_name, char *p_table_name, char *p_macro_name,
	    FILE * out)
{
  N = key_num;
  a = key_size;
  b = ptr_size;
  max_depth = p_max_depth;
  tab_width = p_tab_width;
  name = p_name;
  key_type_name = p_key_type_name;
  table_name = p_table_name;
  macro_name = p_macro_name;
  f = out;
  init (base);
  if (!(tab = malloc ((n + 1) * sizeof (tab[0]))))
    return 0;
  memmove (tab[0], base, key_num * sizeof (int));
  solve ();
  write_out ();
  free (tab);
  return 1;
}
